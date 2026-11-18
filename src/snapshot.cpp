// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "snapshot.h"

#include "bootstrap.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/merkle.h"
#include "crypto/sha256.h"
#include "fs.h"
#include "governance/governance.h"
#include "gunzip.h"
#include "hash.h"
#include "init.h"
#include "primitives/block.h"
#include "streams.h"
#include "sync.h"
#include "tokens/tokendb.h"
#include "txdb.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "validation.h"

#include <univalue.h>

#include <boost/thread.hpp>

#include <algorithm>
#include <map>
#include <thread>
#include <vector>

#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

const char* MANIFEST_NAME = "snapshot.json";
// records of the block tree database
const std::string DB_SNAPSHOT_HEIGHT = "snapshotheight";    // the node started from a snapshot at this height
const std::string FLAG_PAST_BLOCKS_MISSING = "pastblocksmissing";
const std::string DB_PAST_BLOCKS_STORED = "pastblocksstored";  // the past blocks up to this height are on disk
const std::string FLAG_RESCAN_PENDING = "snapshotrescan";    // a wallet waits for the past blocks to be scanned
// In the data directory from the moment a snapshot is unpacked until it has passed the check.
const char* MARKER_NAME = "snapshot-unverified";
// In the data directory while the node runs from a snapshot without the past blocks.
const char* NO_PAST_BLOCKS_NAME = "snapshot-without-past-blocks";
// what a snapshot may bring into the data directory
const char* const SNAPSHOT_DIRS[] = {"chainstate", "blocks/index", "tokens", "governance"};
// Where a snapshot is unpacked. It takes the place of the chain data when it is all there.
const char* const STAGING_NAME = "snapshot-unpack";
const uint64_t MAX_SNAPSHOT_SIZE = 64ULL * 1024 * 1024 * 1024;

std::atomic<int> nSnapshotHeight(-1);
std::atomic<bool> fPastBlocksMissing(false);
std::atomic<bool> fFetching(false);
std::atomic<uint64_t> nFetchBytesDone(0);
std::atomic<uint64_t> nFetchBytesTotal(0);
std::atomic<int> nFetchBlocksStored(0);
CCriticalSection cs_snapshot;
std::string strFetchError;                  // guarded by cs_snapshot
bool fInstalledThisStart = false;
bool fSnapshotNotRemoved = false;           // RemoveInstalledSnapshot() could not remove all of it
std::atomic<bool> fRescanPending(false);
std::function<void()> rescanHook;
boost::thread_group* pthreads = nullptr;

/** A key as the bytes that a database iterator returned. */
struct RawKey {
    const std::vector<unsigned char>& bytes;
    explicit RawKey(const std::vector<unsigned char>& _bytes) : bytes(_bytes) {}
    template <typename Stream> void Serialize(Stream& s) const
    {
        if (!bytes.empty())
            s.write((const char*)bytes.data(), bytes.size());
    }
};

void SyncDirectory(const fs::path& dir);   // (below)

bool MarkerExists()
{
    boost::system::error_code ec;
    return fs::exists(GetDataDir() / MARKER_NAME, ec);
}

/** Reads whatever is left of a key or a value. */
struct RawRecord {
    std::vector<unsigned char> bytes;
    template <typename Stream> void Unserialize(Stream& s)
    {
        bytes.resize(s.size());
        if (!bytes.empty())
            s.read((char*)bytes.data(), bytes.size());
    }
};

bool IsHash(const std::string& hex)
{
    return hex.size() == 64 && IsHex(hex);
}

std::string Megabytes(uint64_t bytes)
{
    return strprintf("%d MB", bytes / 1000000);
}

/** "about 3 min left" from what was done in the time so far. */
std::string TimeLeft(int64_t startMillis, uint64_t done, uint64_t total)
{
    const int64_t elapsed = GetTimeMillis() - startMillis;
    if (elapsed < 3000 || done == 0 || done >= total)
        return "";
    const int64_t left = (int64_t)((double)elapsed * (total - done) / done / 1000);
    if (left < 60)
        return ", " + _("less than a minute left");
    return ", " + strprintf(_("about %d min left"), (left + 30) / 60);
}

} // namespace

// ---------------------------------------------------------------- hashes of the state

bool ComputeStateHashes(StateHashes& hashes, std::string& error)
{
    FlushStateToDisk();
    const CBlockIndex* tip;
    {
        LOCK(cs_main);
        tip = chainActive.Tip();
    }
    if (!tip) {
        error = "no chain";
        return false;
    }

    // The unspent outputs. One hash covers everything the chainstate stores for an output: the
    // outpoint, the height, the coinbase and coinstake marks, the time, the value and the script.
    // The other is the hash that gettxoutsetinfo reports, to compare with nodes that do not have
    // this function; it leaves the time and the coinstake mark out.
    {
        std::unique_ptr<CCoinsViewCursor> cursor(pcoinsdbview->Cursor());
        if (!cursor || cursor->GetBestBlock() != tip->GetBlockHash()) {
            error = "the chainstate is not at the chain tip";
            return false;
        }
        CHashWriter full(SER_GETHASH, 0);
        CHashWriter legacy(SER_GETHASH, PROTOCOL_VERSION);
        full << tip->GetBlockHash();
        legacy << tip->GetBlockHash();
        uint256 previous;
        std::map<uint32_t, Coin> outputs;
        auto apply = [&legacy](const uint256& txid, const std::map<uint32_t, Coin>& group) {
            legacy << txid;
            legacy << VARINT(group.begin()->second.nHeight * 2 + group.begin()->second.fCoinBase);
            for (const auto& output : group) {
                legacy << VARINT(output.first + 1);
                legacy << output.second.out.scriptPubKey;
                legacy << VARINT(output.second.out.nValue);
            }
            legacy << VARINT(0);
        };
        uint64_t count = 0;
        while (cursor->Valid()) {
            if ((count & 0xfff) == 0 && ShutdownRequested()) {
                error = "interrupted";
                return false;
            }
            COutPoint key;
            Coin coin;
            if (!cursor->GetKey(key) || !cursor->GetValue(coin)) {
                error = "cannot read the chainstate";
                return false;
            }
            full << key << coin;
            if (!outputs.empty() && key.hash != previous) {
                apply(previous, outputs);
                outputs.clear();
            }
            previous = key.hash;
            outputs[key.n] = std::move(coin);
            ++count;
            cursor->Next();
        }
        if (!outputs.empty())
            apply(previous, outputs);
        hashes.utxo = full.GetHash();
        hashes.utxoLegacy = legacy.GetHash();
        hashes.coins = count;
    }

    // The token database: every record but the undo records of blocks ('U'), which exist for the
    // blocks of abandoned forks too and so differ between nodes with the same state. (A node that
    // accepts a snapshot deletes the undo records that came with it.)
    {
        CHashWriter ss(SER_GETHASH, 0);
        std::unique_ptr<CDBIterator> it(ptokensdb->NewIterator());
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            RawRecord key, value;
            if (!it->GetKey(key) || !it->GetValue(value) || key.bytes.empty()) {
                error = "cannot read the token database";
                return false;
            }
            if ((char)key.bytes[0] != 'U')
                ss << key.bytes << value.bytes;
        }
        hashes.tokens = ss.GetHash();
    }

    hashes.governance = governance->ContentHash();
    if (hashes.governance.IsNull()) {
        error = "cannot read the governance database";
        return false;
    }

    // The block index of the active chain: for every block what later validation reads from it.
    {
        LOCK(cs_main);
        CHashWriter ss(SER_GETHASH, 0);
        for (const CBlockIndex* pindex = chainActive.Genesis(); pindex; pindex = chainActive.Next(pindex)) {
            ss << pindex->GetBlockHash() << pindex->nHeight << pindex->nVersion << pindex->hashMerkleRoot << pindex->nTime
               << pindex->nBits << pindex->nNonce << pindex->nStakeModifier << pindex->nTx << pindex->nStatus;
            if (pindex == tip)
                break;
        }
        hashes.index = ss.GetHash();
    }
    hashes.height = tip->nHeight;
    hashes.block = tip->GetBlockHash();
    return true;
}

// ---------------------------------------------------------------- state of a snapshot node

bool PastBlocksMissing()
{
    return fPastBlocksMissing;
}

int SnapshotHeight()
{
    return nSnapshotHeight;
}

bool IsPastBlockMissing(const CBlockIndex* pindex)
{
    return fPastBlocksMissing && pindex && pindex->nHeight <= nSnapshotHeight;
}

std::string PastBlocksMessage()
{
    return "This needs the past blocks, which this node does not have: it started from a chain state snapshot. "
           "Download them (about 1.4 GB) with the RPC \"fetchblocks\", the option -fetchblocks=1, or in the wallet under Settings.";
}

PastBlocksInfo GetPastBlocksInfo()
{
    PastBlocksInfo info;
    info.missing = fPastBlocksMissing;
    info.fetching = fFetching;
    info.snapshotHeight = nSnapshotHeight;
    info.bytesTotal = nFetchBytesTotal;
    info.bytesDone = nFetchBytesDone;
    info.blocksStored = nFetchBlocksStored;
    LOCK(cs_snapshot);
    info.error = strFetchError;
    return info;
}

void MarkSnapshotAccepted()
{
    const int height = Params().Snapshot().height;
    // The undo records of the token database are not covered by the hashes. They are of use only
    // to disconnect blocks up to the snapshot height, which this node never does.
    {
        CDBBatch batch(*ptokensdb);
        std::unique_ptr<CDBIterator> it(ptokensdb->NewIterator());
        int erased = 0;
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            RawRecord key;
            // (not the block that the database stands for, which is kept among them)
            if (it->GetKey(key) && !key.bytes.empty() && (char)key.bytes[0] == 'U' && !CTokensDB::IsStateBlockKey(key.bytes)) {
                batch.Erase(RawKey(key.bytes));
                ++erased;
            }
        }
        it.reset();
        ptokensdb->WriteBatch(batch, true);
        LogPrintf("Snapshot: %d token undo records removed\n", erased);
    }
    // The order, each step on disk before the next: the file of a node without the past
    // blocks, the record of the acceptance, the removal of the marker. A stop in between
    // leaves the marker, and the next start checks the snapshot again, in full: the record
    // that this function wrote is the one thing beside a snapshot's own records that the
    // check lets pass (VerifyInstalledSnapshot), and it proves nothing there.
    if (FILE* note = fsbridge::fopen(GetDataDir() / NO_PAST_BLOCKS_NAME, "wb")) {
        fprintf(note, "This node started from a chain state snapshot at height %d and does not have the blocks up to that height.\n", height);
        FileCommit(note);
        fclose(note);
    }
    SyncDirectory(GetDataDir());
    pblocktree->Write(DB_SNAPSHOT_HEIGHT, height);
    pblocktree->WriteFlag(FLAG_PAST_BLOCKS_MISSING, true);
    pblocktree->WriteFlag("txindex", fTxIndex);
    pblocktree->Sync();
    TestCrashPoint("snapshot-accepted");
    boost::system::error_code ec;
    fs::remove(GetDataDir() / MARKER_NAME, ec);
    SyncDirectory(GetDataDir());
    nSnapshotHeight = height;
    fPastBlocksMissing = true;
    fInstalledThisStart = false;
    LogPrintf("Snapshot: accepted at height %d. The past blocks are not on disk.\n", height);
}

void LoadSnapshotState(boost::thread_group& threadGroup)
{
    pthreads = &threadGroup;
    int height = -1;
    bool missing = false, rescan = false;
    nSnapshotHeight = -1;
    fPastBlocksMissing = false;
    fRescanPending = false;
    // A snapshot that is not checked yet says nothing about this node: records that came with
    // it are not read here, and no file is written from them. (MarkSnapshotAccepted() sets
    // these values when the check has passed.)
    if (fInstalledThisStart)
        return;
    if (pblocktree->Read(DB_SNAPSHOT_HEIGHT, height)) {
        pblocktree->ReadFlag(FLAG_PAST_BLOCKS_MISSING, missing);
        pblocktree->ReadFlag(FLAG_RESCAN_PENDING, rescan);
        nSnapshotHeight = height;
        fPastBlocksMissing = missing;
        fRescanPending = rescan;
        LogPrintf("Snapshot: this node started from a snapshot at height %d; past blocks %s\n", height, missing ? "not downloaded" : "present");
        // The file in the data directory says the same as the flag in the database (it is
        // what is asked when the database cannot be read). The flag is written first when the
        // past blocks are fetched: a stop right after it left the file, and a node with all
        // its blocks would have started over from the snapshot instead of building its
        // databases again from them.
        boost::system::error_code ec;
        const fs::path note = GetDataDir() / NO_PAST_BLOCKS_NAME;
        if (!missing && fs::exists(note, ec)) {
            fs::remove(note, ec);
        } else if (missing && !fs::exists(note, ec)) {
            if (FILE* file = fsbridge::fopen(note, "wb")) {
                fprintf(file, "This node started from a chain state snapshot at height %d and does not have the blocks up to that height.\n", height);
                fclose(file);
            }
        }
    }
}

bool SnapshotNodeWithoutPastBlocks()
{
    boost::system::error_code ec;
    return fs::exists(GetDataDir() / NO_PAST_BLOCKS_NAME, ec);
}

bool SnapshotBlockFilesMayBeMissing()
{
    bool missing = false;
    return MarkerExists() || (pblocktree && pblocktree->ReadFlag(FLAG_PAST_BLOCKS_MISSING, missing) && missing);
}

bool WalletRescanPending()
{
    return fRescanPending;
}

void SetWalletRescanPending(bool pending)
{
    if (fRescanPending.exchange(pending) != pending) {
        pblocktree->WriteFlag(FLAG_RESCAN_PENDING, pending);
        pblocktree->Sync();
    }
}

void SetPastBlocksRescanHook(std::function<void()> hook)
{
    LOCK(cs_snapshot);
    rescanHook = hook;
}

// ---------------------------------------------------------------- the past blocks

namespace {

/** Store one past block at the place that the block index records for it. */
bool StorePastBlock(int height, const std::vector<char>& raw, std::string& error)
{
    CBlock block;
    try {
        CDataStream stream(raw.data(), raw.data() + raw.size(), SER_DISK, CLIENT_VERSION);
        stream >> block;
    } catch (const std::exception&) {
        error = strprintf("the block at height %d cannot be read", height);
        return false;
    }
    const uint256 hash = block.GetBlockHash();
    CDiskBlockPos pos;
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = chainActive[height];
        if (!pindex || pindex->GetBlockHash() != hash) {
            error = strprintf("the block at height %d of the archives is not the block of the chain", height);
            return false;
        }
        pos = pindex->GetBlockPos();
        // The positions came with the snapshot and are not covered by its hashes: a block must
        // end before the place of the next one, or it would overwrite it.
        const CBlockIndex* next = chainActive[height + 1];
        if (next && next->nFile == pindex->nFile && (uint64_t)pos.nPos + raw.size() + 8 > next->nDataPos) {
            error = strprintf("the block at height %d does not fit the place that the block index has for it", height);
            return false;
        }
    }
    // The hash covers the header; the merkle root ties the transactions to it.
    bool mutated = false;
    if (BlockMerkleRoot(block, &mutated) != block.hashMerkleRoot || mutated) {
        error = strprintf("the transactions of the block at height %d do not fit its header", height);
        return false;
    }
    if (pos.IsNull() || pos.nPos < 8) {
        error = strprintf("the block index has no place for the block at height %d", height);
        return false;
    }
    // the transaction index, as ConnectBlock() writes it (the genesis block has none)
    std::vector<std::pair<uint256, CDiskTxPos> > txPositions;
    if (fTxIndex && height > 0) {
        CDiskTxPos txPos(pos, GetSizeOfCompactSize(block.vtx.size()));
        txPositions.reserve(block.vtx.size());
        for (const CTransactionRef& tx : block.vtx) {
            txPositions.push_back(std::make_pair(tx->GetHash(), txPos));
            txPos.nTxOffset += ::GetSerializeSize(*tx, SER_DISK, CLIENT_VERSION);
        }
    }
    FILE* file = OpenBlockFile(CDiskBlockPos(pos.nFile, pos.nPos - 8));
    if (!file) {
        error = "cannot open the block file";
        return false;
    }
    unsigned char header[8];
    memcpy(header, Params().MessageStart(), 4);
    WriteLE32(header + 4, (uint32_t)raw.size());
    const bool ok = fwrite(header, 1, 8, file) == 8 && fwrite(raw.data(), 1, raw.size(), file) == raw.size();
    fclose(file);
    if (!ok) {
        error = "cannot write the block file (disk full?)";
        return false;
    }
    if (!txPositions.empty() && !pblocktree->WriteTxIndex(txPositions)) {
        error = "cannot write the transaction index";
        return false;
    }
    ++nFetchBlocksStored;
    return true;
}

bool FetchPastBlocksWork(std::string& error)
{
    const int height = nSnapshotHeight;
    // an earlier download that was interrupted got this far (the end of a block archive)
    int stored = -1;
    pblocktree->Read(DB_PAST_BLOCKS_STORED, stored);
    if (stored < 0 || stored > height)
        stored = -1;
    nFetchBytesDone = 0;
    nFetchBytesTotal = 0;
    nFetchBlocksStored = stored + 1;
    const int64_t start = GetTimeMillis();
    LogPrintf("Snapshot: fetching the past blocks %d to %d\n", stored + 1, height);
    // The block files are written to the disk before the progress is recorded: a record must
    // never be ahead of the blocks.
    auto commit = [](int lastHeight, std::string& error) {
        int lastFile;
        {
            LOCK(cs_main);
            lastFile = chainActive[lastHeight]->nFile;
        }
        for (int n = 0; n <= lastFile; ++n) {
            FILE* file = OpenBlockFile(CDiskBlockPos(n, 0));
            if (!file) {
                error = "cannot open the block file";
                return false;
            }
            FileCommit(file);
            fclose(file);
        }
        return pblocktree->Write(DB_PAST_BLOCKS_STORED, lastHeight) && pblocktree->Sync();
    };
    if (stored < 0) {
        // the genesis block is not in the archives
        CDataStream stream(SER_DISK, CLIENT_VERSION);
        stream << Params().GenesisBlock();
        const std::vector<char> raw(stream.begin(), stream.end());
        if (!StorePastBlock(0, raw, error))
            return false;
    }
    if (!BootstrapVisitBlocks(stored + 1, height, StorePastBlock, commit, &nFetchBytesDone, &nFetchBytesTotal, error))
        return false;
    if (nFetchBlocksStored != height + 1) {
        error = strprintf("%d of %d past blocks were stored", nFetchBlocksStored.load(), height + 1);
        return false;
    }
    pblocktree->WriteFlag(FLAG_PAST_BLOCKS_MISSING, false);
    pblocktree->Sync();
    boost::system::error_code ec;
    fs::remove(GetDataDir() / NO_PAST_BLOCKS_NAME, ec);
    fPastBlocksMissing = false;
    uiInterface.NotifyAlertChanged();
    LogPrintf("Snapshot: the past blocks are on disk (%d blocks, %.1f s). The node serves them to peers from its next start.\n",
              height + 1, (GetTimeMillis() - start) / 1000.0);
    return true;
}

void ThreadFetchPastBlocks()
{
    RenameThread("aokchain-pastblk");
    std::string error;
    bool ok = false;
    try {
        ok = FetchPastBlocksWork(error);
    } catch (const boost::thread_interrupted&) {
        fFetching = false;
        throw;
    } catch (const std::exception& e) {
        error = e.what();
    }
    if (!ok)
        LogPrintf("Snapshot: fetching the past blocks stopped: %s\n", error);
    std::function<void()> hook;
    {
        LOCK(cs_snapshot);
        strFetchError = ok ? "" : error;
        hook = rescanHook;
    }
    if (ok && fRescanPending && hook) {
        // a wallet was waiting for these blocks
        LogPrintf("Snapshot: scanning the past blocks for the wallet\n");
        try {
            hook();
            SetWalletRescanPending(false);
        } catch (const boost::thread_interrupted&) {
            fFetching = false;
            throw;
        } catch (const std::exception& e) {
            LogPrintf("Snapshot: wallet scan failed: %s\n", e.what());
        }
    }
    fFetching = false;
}

} // namespace

bool FetchPastBlocks(std::string& error)
{
    if (!fPastBlocksMissing) {
        error = "the past blocks are on disk";
        return false;
    }
    if (!BootstrapHasSource()) {
        error = "there is no address or folder to take the block archives from (-bootstrapurl, -bootstrapdir)";
        return false;
    }
    if (!pthreads || fFetching.exchange(true)) {
        error = "a download of the past blocks is running";
        return false;
    }
    {
        LOCK(cs_snapshot);
        strFetchError.clear();
    }
    pthreads->create_thread(&ThreadFetchPastBlocks);
    return true;
}

bool FetchPastBlocksNow(std::string& error)
{
    if (!fPastBlocksMissing)
        return true;
    if (!BootstrapHasSource()) {
        error = "there is no address or folder to take the block archives from (-bootstrapurl, -bootstrapdir)";
        return false;
    }
    if (fFetching.exchange(true)) {
        error = "a download of the past blocks is running";
        return false;
    }
    const bool ok = FetchPastBlocksWork(error);
    {
        LOCK(cs_snapshot);
        strFetchError = ok ? "" : error;
    }
    fFetching = false;
    return ok;
}

// ---------------------------------------------------------------- installing a snapshot

namespace {

struct Part {
    std::string file;
    uint64_t size;
    std::string sha256;
};

/**
 * Writes the unpacked stream of a snapshot into the data directory. The stream is a list of
 * files: the length of the name (2 bytes), the name, the size (8 bytes), the bytes; a name of
 * length 0 ends it. Only files below the directories of SNAPSHOT_DIRS are accepted.
 */
class Unpacker
{
public:
    explicit Unpacker(const fs::path& _root) : root(_root), state(NAME_LENGTH), need(2), left(0), file(nullptr), finished(false), files(0) {}
    ~Unpacker() { if (file) fclose(file); }

    bool Write(const unsigned char* data, size_t size)
    {
        while (size > 0) {
            if (finished) {
                error = "data after the end of the snapshot";
                return false;
            }
            if (state == CONTENT) {
                const size_t n = (size_t)std::min<uint64_t>(left, size);
                if (fwrite(data, 1, n, file) != n) {
                    error = "cannot write into the data directory (disk full?)";
                    return false;
                }
                data += n;
                size -= n;
                left -= n;
                if (left == 0 && !Next())
                    return false;
                continue;
            }
            const size_t n = std::min(need - buffer.size(), size);
            buffer.insert(buffer.end(), data, data + n);
            data += n;
            size -= n;
            if (buffer.size() == need && !Next())
                return false;
        }
        return true;
    }

    bool Finished() const { return finished; }
    int Files() const { return files; }
    std::string error;

private:
    enum State { NAME_LENGTH, NAME, SIZE, CONTENT };

    bool Next()
    {
        if (state == NAME_LENGTH) {
            const size_t length = buffer[0] | (buffer[1] << 8);
            buffer.clear();
            if (length == 0) {
                finished = true;
                return true;
            }
            if (length > 200) {
                error = "a file name of the snapshot is too long";
                return false;
            }
            state = NAME;
            need = length;
        } else if (state == NAME) {
            name.assign(buffer.begin(), buffer.end());
            buffer.clear();
            if (!Allowed(name)) {
                error = "the snapshot has a file that does not belong into it: " + name;
                return false;
            }
            state = SIZE;
            need = 8;
        } else if (state == SIZE) {
            left = ReadLE64(buffer.data());
            buffer.clear();
            const fs::path path = root / name;
            boost::system::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            file = fsbridge::fopen(path, "wb");
            if (!file) {
                error = "cannot create " + path.string();
                return false;
            }
            ++files;
            state = CONTENT;
            if (left == 0)
                return Next();
        } else {
            // (on disk: the snapshot takes the place of other data only when it is all there)
            FileCommit(file);
            if (fclose(file) != 0) {
                file = nullptr;
                error = "cannot write into the data directory (disk full?)";
                return false;
            }
            file = nullptr;
            state = NAME_LENGTH;
            need = 2;
        }
        return true;
    }

    static bool Allowed(const std::string& path)
    {
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos || path[0] == '/')
            return false;
        for (char c : path) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '/'))
                return false;
        }
        for (const char* dir : SNAPSHOT_DIRS) {
            const std::string prefix = std::string(dir) + "/";
            // a file directly in one of the directories
            if (path.compare(0, prefix.size(), prefix) == 0 && path.find('/', prefix.size()) == std::string::npos && path.size() > prefix.size())
                return true;
        }
        return false;
    }

    fs::path root;
    State state;
    size_t need;
    uint64_t left;
    std::vector<unsigned char> buffer;
    std::string name;
    FILE* file;
    bool finished;
    int files;
};

/**
 * True when the data directory has no chain yet: nothing at all, or the databases of a node that
 * never got past the genesis block (it was started without a connection, say).
 */
bool DataDirIsNew()
{
    boost::system::error_code ec;
    if (!fs::exists(GetDataDir() / "chainstate", ec) || fs::is_empty(GetDataDir() / "chainstate", ec))
        return !fs::exists(GetDataDir() / "blocks" / "blk00001.dat", ec);
    if (fs::exists(GetDataDir() / "blocks" / "blk00001.dat", ec))
        return false;
    try {
        CCoinsViewDB view(1 << 20, false, false);
        const uint256 best = view.GetBestBlock();
        return best.IsNull() || best == Params().GetConsensus().hashGenesisBlock;
    } catch (const std::exception&) {
        return false;
    }
}

/** The names of the directories are on disk (after files were created in them or moved). */
void SyncDirectory(const fs::path& dir)
{
#ifndef WIN32
    const int fd = open(dir.string().c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif
}

/** Remove what belongs to the chain data and is not one of the databases of SNAPSHOT_DIRS: the
 *  block files, and the records of what the blocks replaced in the governance state. */
void WipeBlockFilesAndRecords()
{
    boost::system::error_code ec;
    // (what the blocks of the removed chain replaced in the governance state; a snapshot
    // brings none)
    fs::remove_all(GetDataDir() / "governanceundo", ec);
    fs::remove(GetDataDir() / "governanceundo.since", ec);
    const fs::path blocks = GetDataDir() / "blocks";
    if (fs::is_directory(blocks, ec)) {
        std::vector<fs::path> files;
        for (fs::directory_iterator it(blocks, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.size() == 12 && (name.compare(0, 3, "blk") == 0 || name.compare(0, 3, "rev") == 0) && name.compare(8, 4, ".dat") == 0)
                files.push_back(it->path());
        }
        for (const fs::path& file : files)
            fs::remove(file, ec);
    }
}

/** The size of the files below a directory, in bytes (0 when there is none). */
uint64_t DirectorySize(const fs::path& dir)
{
    uint64_t size = 0;
    boost::system::error_code ec;
    if (!fs::is_directory(dir, ec))
        return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        boost::system::error_code ecFile;
        if (fs::is_regular_file(it->path(), ecFile)) {
            const uintmax_t n = fs::file_size(it->path(), ecFile);
            if (!ecFile)
                size += n;
        }
    }
    return size;
}

/** Copies a file; the copy is on disk when this returns true. */
bool CopyFileToDisk(const fs::path& from, const fs::path& to, std::string& error)
{
    FILE* in = fsbridge::fopen(from, "rb");
    if (!in) {
        error = strprintf("cannot read %s", from.string());
        return false;
    }
    FILE* out = fsbridge::fopen(to, "wb");
    if (!out) {
        fclose(in);
        error = strprintf("cannot write %s", to.string());
        return false;
    }
    std::vector<char> buffer(1 << 20);
    bool ok = true;
    size_t got;
    while (ok && (got = fread(buffer.data(), 1, buffer.size(), in)) > 0)
        ok = fwrite(buffer.data(), 1, got, out) == got;
    ok = ok && !ferror(in) && fflush(out) == 0;
    if (ok)
        FileCommit(out);
    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    if (!ok)
        error = strprintf("cannot write %s (is the disk full?)", to.string());
    return ok;
}

/** Copies what a directory holds into another one, which exists. */
bool CopyDirectoryContent(const fs::path& from, const fs::path& to, std::string& error)
{
    boost::system::error_code ec;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec))
        entries.push_back(it->path());
    if (ec) {
        error = strprintf("cannot read %s (%s)", from.string(), ec.message());
        return false;
    }
    for (const fs::path& entry : entries) {
        const fs::path dest = to / entry.filename();
        if (fs::is_directory(entry, ec)) {
            fs::create_directory(dest, ec);
            if (!fs::is_directory(dest, ec)) {
                error = strprintf("cannot make %s", dest.string());
                return false;
            }
            if (!CopyDirectoryContent(entry, dest, error))
                return false;
        } else if (!CopyFileToDisk(entry, dest, error)) {
            return false;
        }
    }
    SyncDirectory(to);
    return true;
}

/** Removes what a directory holds, and keeps the directory. */
bool EmptyDirectory(const fs::path& dir, std::string& error)
{
    boost::system::error_code ec;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        // (the directory that a file system keeps at its root, when the database directory
        // is the mount point itself: not the node's, and not for the node to remove)
        if (it->path().filename() != "lost+found")
            entries.push_back(it->path());
    }
    if (ec) {
        error = strprintf("cannot read %s (%s)", dir.string(), ec.message());
        return false;
    }
    for (const fs::path& entry : entries) {
        fs::remove_all(entry, ec);
        if (ec) {
            error = strprintf("cannot remove %s (%s)", entry.string(), ec.message());
            return false;
        }
    }
    return true;
}

/** Whether a directory of the node is a link to a directory, or the mount point of a file
 *  system of its own: the operator has put it elsewhere. */
bool KeptElsewhere(const fs::path& dir)
{
    boost::system::error_code ec;
    if (fs::is_symlink(dir, ec))
        return fs::is_directory(dir, ec);
#ifndef WIN32
    struct stat stDir, stParent;
    if (stat(dir.string().c_str(), &stDir) == 0 && S_ISDIR(stDir.st_mode) &&
        stat(dir.parent_path().string().c_str(), &stParent) == 0 && stDir.st_dev != stParent.st_dev)
        return true;
#endif
    return false;
}

/** Remove the chain databases and the block files, to take a snapshot away. A database
 *  directory that the operator keeps elsewhere (a link, a mount point) stays what it is: it
 *  is emptied. False when one of the databases is still there, with error naming it. */
bool WipeChainData(std::string& error)
{
    boost::system::error_code ec;
    bool fGone = true;
    for (const char* dir : SNAPSHOT_DIRS) {
        const fs::path path = GetDataDir() / dir;
        std::string why;
        if (KeptElsewhere(path)) {
            if (!EmptyDirectory(path, why))
                fGone = false;
        } else {
            fs::remove_all(path, ec);
            if (fs::exists(path, ec)) {
                why = "it cannot be removed";
                fGone = false;
            }
        }
        if (!why.empty()) {
            LogPrintf("Snapshot: %s is still there (%s).\n", dir, why);
            error = strprintf("%s (%s)", dir, why);
        }
    }
    WipeBlockFilesAndRecords();
    return fGone;
}

/**
 * Whether the directory of the snapshot can take the place of the node's by a rename. It
 * cannot when the node's directory is a link or lies on another file system (the operator has
 * put that database, or the block directory, elsewhere): the files are copied into it then,
 * and the link or the mount stays what it is.
 */
bool MovesByRename(const fs::path& from, const fs::path& target)
{
    boost::system::error_code ec;
    if (fs::is_symlink(target, ec))
        return false;
#ifndef WIN32
    // (the file system of the place where the directory will be: its parent, or the nearest
    // directory above it that exists; and of the directory itself, which can be a mount point)
    struct stat stFrom, stTo;
    if (stat(from.string().c_str(), &stFrom) != 0)
        return true;
    if (stat(target.string().c_str(), &stTo) == 0 && stTo.st_dev != stFrom.st_dev)
        return false;
    fs::path above = target.parent_path();
    while (!above.empty() && stat(above.string().c_str(), &stTo) != 0)
        above = above.parent_path();
    if (!above.empty() && stTo.st_dev != stFrom.st_dev)
        return false;
#endif
    return true;
}

enum class MoveResult {
    DONE,
    NOT_BEGUN,  //!< it cannot be done, and nothing of the data directory was changed
    FAILED,     //!< it stopped half-way: the block files and records are removed, what is left to move is in STAGING_NAME
};

/**
 * An unpacked snapshot (in STAGING_NAME) takes the place of the chain data of the node: for
 * each of its directories the one of the node is removed and the one of the snapshot moved
 * in; the block files and the governance records go. The marker is written before this
 * begins, and a start that finds the marker and what is left in the staging directory does
 * the rest (InstallSnapshotIfWanted): a directory that is still there has not been moved.
 *
 * A directory of the node that is a link or lies on another file system is not replaced: it
 * is emptied and the files of the snapshot are copied into it; they are on disk before the
 * unpacked ones are removed, so a stop in the middle is made good in the same way. Whether
 * such a place is there, can be written to and has room for the copies is looked at before
 * anything is removed.
 */
MoveResult MoveSnapshotIn(std::string& error)
{
    const fs::path staging = GetDataDir() / STAGING_NAME;
    boost::system::error_code ec;
    // What has to be copied, and whether there is room for it where it goes (what the copy
    // replaces there is removed first, so it counts as room).
    std::map<std::string, bool> rename;
    for (const char* dir : SNAPSHOT_DIRS) {
        if (!fs::is_directory(staging / dir, ec))
            continue; // moved before this start
        const fs::path target = GetDataDir() / dir;
        rename[dir] = MovesByRename(staging / dir, target);
        if (rename[dir])
            continue;
        if (fs::is_symlink(target, ec) && !fs::exists(target, ec)) {
            error = strprintf("%s is a link to a place that is not there (%s)", dir, target.string());
            LogPrintf("Snapshot: %s.\n", error);
            return MoveResult::NOT_BEGUN;
        }
        fs::path there = target;
        while (!there.empty() && !fs::exists(there, ec))
            there = there.parent_path();
#ifndef WIN32
        if (!there.empty() && access(there.string().c_str(), R_OK | W_OK | X_OK) != 0) {
            error = strprintf("the place where this node keeps %s cannot be read or written to (%s)", dir, there.string());
            LogPrintf("Snapshot: %s.\n", error);
            return MoveResult::NOT_BEGUN;
        }
#endif
        const fs::space_info space = fs::space(there, ec);
        // (with a reserve: the database grows as soon as the node runs)
        const uint64_t need = DirectorySize(staging / dir) + (16 << 20);
        if (!ec && space.available != static_cast<uintmax_t>(-1) && space.available + DirectorySize(target) < need) {
            error = strprintf("there is not enough room for %s where this node keeps it (%s; %s are needed there, %s are free)", dir, there.string(),
                              Megabytes(need), Megabytes(space.available + DirectorySize(target)));
            LogPrintf("Snapshot: %s.\n", error);
            return MoveResult::NOT_BEGUN;
        }
    }

    WipeBlockFilesAndRecords();
    fs::remove(GetDataDir() / NO_PAST_BLOCKS_NAME, ec);
    fPastBlocksMissing = false;
    bool first = true;
    for (const char* dir : SNAPSHOT_DIRS) {
        if (!fs::is_directory(staging / dir, ec))
            continue; // moved before this start
        const fs::path target = GetDataDir() / dir;
        bool moved = false;
        if (rename[dir]) {
            fs::remove_all(target, ec);
            fs::create_directories(target.parent_path(), ec);
            fs::rename(staging / dir, target, ec);
            moved = !ec;
            if (!moved)
                LogPrintf("Snapshot: %s is not moved by a rename (%s): its files are copied.\n", dir, ec.message());
        }
        if (!moved) {
            fs::create_directories(target, ec);
            std::string why;
            if (!fs::is_directory(target, ec))
                why = strprintf("cannot make %s", target.string());
            if (why.empty() && EmptyDirectory(target, why) && CopyDirectoryContent(staging / dir, target, why)) {
                // The copy is complete and on disk. The unpacked directory gets another name
                // before it is removed: a stop inside the removal must not leave it, with
                // some of its files, to be taken for one that is still to be copied.
                const fs::path copied = (staging / dir).string() + ".copied";
                fs::remove_all(copied, ec);
                fs::rename(staging / dir, copied, ec);
                SyncDirectory((staging / dir).parent_path());
                TestCrashPoint("snapshot-copied");
                fs::remove_all(ec ? staging / dir : copied, ec);
            } else {
                error = strprintf("%s cannot be put into its place (%s)", dir, why);
                LogPrintf("Snapshot: %s.\n", error);
                return MoveResult::FAILED;
            }
        }
        SyncDirectory(target.parent_path());
        if (first)
            TestCrashPoint("snapshot-moving");
        first = false;
    }
    fs::remove_all(staging, ec);
    SyncDirectory(GetDataDir());
    return MoveResult::DONE;
}

/**
 * The marker holds how often a start has tried to check the snapshot that is unpacked in the
 * data directory, and whether that snapshot took the place of the chain data of a node that
 * ran (a start-over) or is the first chain data of a new node.
 */
int ReadMarker(bool& fStartOver)
{
    fStartOver = false;
    FILE* marker = fsbridge::fopen(GetDataDir() / MARKER_NAME, "rb");
    if (!marker)
        return 0;
    const int c = fgetc(marker);
    fStartOver = fgetc(marker) == 'o';
    fclose(marker);
    if (c >= '1' && c <= '9')
        return c - '0';
    // A marker without its count (a version that wrote it in place, stopped inside that
    // write): how often was tried is not known, and neither is whose chain data the
    // snapshot replaced. It is taken for the case in which a removal costs most.
    fStartOver = true;
    return 1;
}

/** Written beside the marker and moved over it: the marker is never there without its content. */
bool WriteMarker(int attempts, bool fStartOver)
{
    const fs::path path = GetDataDir() / MARKER_NAME;
    const fs::path pathNew = path.string() + ".new";
    FILE* marker = fsbridge::fopen(pathNew, "wb");
    if (!marker)
        return false;
    bool ok = fputc('0' + std::min(attempts, 9), marker) != EOF;
    if (fStartOver)
        ok = ok && fputc('o', marker) != EOF;
    ok = ok && fflush(marker) == 0;
    FileCommit(marker);
    if (fclose(marker) != 0 || !ok || !RenameOver(pathNew, path))
        return false;
    SyncDirectory(GetDataDir());
    return true;
}

} // namespace

bool RemoveInstalledSnapshot(std::string& strLeft)
{
    boost::system::error_code ec;
    // What cannot be removed must not be opened as the data of this node. The marker stays
    // then, so that whatever is left counts as a snapshot that is not accepted at every
    // later start too, and the caller ends this start.
    std::string what;
    if (!WipeChainData(what)) {
        strLeft = strprintf(_("A chain state snapshot that was not accepted could not be removed completely: %s is still there. Nothing of it is used. Mend what is in the way and start the program again: it removes the rest."), what);
        LogPrintf("Snapshot: not accepted, and not removed completely. The start ends.\n");
        fSnapshotNotRemoved = true;
        return false;
    }
    fs::remove_all(GetDataDir() / STAGING_NAME, ec);
    fs::remove_all(GetDataDir() / "snapshot-download", ec);
    fs::remove(GetDataDir() / MARKER_NAME, ec);
    fs::remove(GetDataDir() / (std::string(MARKER_NAME) + ".new"), ec);
    // (the file of a node without the past blocks is written before a snapshot is accepted,
    // and belongs to the snapshot that goes: the node that is left has all its blocks)
    fs::remove(GetDataDir() / NO_PAST_BLOCKS_NAME, ec);
    fPastBlocksMissing = false;
    fInstalledThisStart = false;
    LogPrintf("Snapshot: removed. The node synchronizes by validating the blocks.\n");
    return true;
}

namespace {

/**
 * Fetch the snapshot that this program accepts, check its files, unpack it beside the chain
 * data, and then let it take the place of the chain data: that of a new node, or with
 * fStartOver that of a node that cannot go on from its databases. Nothing of the data
 * directory is removed before the snapshot is unpacked completely and on disk. False when no
 * snapshot is installed: why says what is in the way, and the data directory is as it was.
 * Except with fMoveFailed (a start-over only): the unpacked snapshot could not be put into its
 * place, the block files and what was replaced so far are gone, the rest of the unpacked
 * snapshot waits in the data directory, and the next start goes on with it.
 */
bool FetchAndInstallSnapshot(std::function<void(const std::string&)> status, bool fStartOver, std::string& why, bool& fMoveFailed)
{
    fMoveFailed = false;
    const CChainParams::SnapshotParams& pinned = Params().Snapshot();
    // -bootstrap=0 (the fast first synchronization of the settings page) turns the snapshot off too
    if (!gArgs.GetBoolArg("-snapshot", DEFAULT_SNAPSHOT) || !gArgs.GetBoolArg("-bootstrap", DEFAULT_BOOTSTRAP) || pinned.height <= 0) {
        why = "the snapshot is turned off (-snapshot=0 or -bootstrap=0)";
        return false;
    }
    if (!fStartOver && !DataDirIsNew())
        return false; // a node with a chain goes on with what it has
    // A data directory that counts as new (no chain state, one block file at most) and holds
    // the file of a node without the past blocks is a snapshot node that ran and lost its
    // chain state: the snapshot takes the place of the data of a node that ran, as in a
    // start-over, and is kept like one.
    {
        boost::system::error_code ecRan;
        if (fs::exists(GetDataDir() / NO_PAST_BLOCKS_NAME, ecRan))
            fStartOver = true;
    }
    // The snapshot is made with the default indexes: other settings need the blocks.
    if (fReindex || gArgs.GetBoolArg("-reindex-chainstate", false) || gArgs.GetArg("-prune", 0) != 0
        || !gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX) || !gArgs.GetBoolArg("-tokenindex", DEFAULT_TOKENINDEX)
        || gArgs.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) || gArgs.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX)
        || gArgs.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
        LogPrintf("Snapshot: not used, because an option of this start needs the blocks to be validated here (reindex, prune or an index option).\n");
        why = "an option of this start needs the blocks to be validated here (reindex, prune or an index option)";
        return false;
    }
    if (!BootstrapHasSource()) {
        LogPrintf("Snapshot: not used, there is no address or folder to take it from.\n");
        why = "this node has no address or folder to take it from (-bootstrapurl=<address> or -bootstrapdir=<folder>)";
        return false;
    }

    const int64_t start = GetTimeMillis();
    status(_("Fast start: looking for the chain state snapshot..."));
    std::string text, error;
    std::vector<Part> parts;
    uint64_t total = 0, unpackedSize = 0;
    try {
        UniValue manifest;
        if (!BootstrapReadFile(MANIFEST_NAME, text, error) || !manifest.read(text) || !manifest.isObject()) {
            LogPrintf("Snapshot: no usable %s (%s).%s\n", MANIFEST_NAME, error, fStartOver ? "" : " The node synchronizes by validating the blocks.");
            why = strprintf("its list of files (%s) cannot be read from the address or folder that this node takes it from: %s", MANIFEST_NAME, error);
            return false;
        }
        if (find_value(manifest, "format").get_int() != 1 || find_value(manifest, "network").get_str() != Params().NetworkIDString()) {
            LogPrintf("Snapshot: the manifest is for another format or network.\n");
            why = "what is offered there is for another format or network";
            return false;
        }
        // a snapshot of another height or block cannot pass the check: do not fetch it at all
        if (find_value(manifest, "height").get_int() != pinned.height || uint256S(find_value(manifest, "block_hash").get_str()) != pinned.block) {
            LogPrintf("Snapshot: the server has a snapshot for height %d, this program accepts the one at height %d (%s). Not used.\n",
                      find_value(manifest, "height").get_int(), pinned.height, pinned.block.GetHex());
            why = strprintf("the snapshot that is offered there is for height %d, and this program accepts the one at height %d only", find_value(manifest, "height").get_int(), pinned.height);
            return false;
        }
        unpackedSize = (uint64_t)find_value(manifest, "size").get_int64();
        const UniValue& list = find_value(manifest, "parts").get_array();
        for (size_t i = 0; i < list.size(); ++i) {
            Part part;
            part.file = find_value(list[i], "file").get_str();
            part.size = (uint64_t)find_value(list[i], "size").get_int64();
            part.sha256 = find_value(list[i], "sha256").get_str();
            if (!IsHash(part.sha256) || part.size == 0 || part.size > MAX_SNAPSHOT_SIZE) {
                LogPrintf("Snapshot: the manifest is not valid.\n");
                why = "its list of files is not valid";
                return false;
            }
            for (char& c : part.sha256)
                c = (char)tolower((unsigned char)c);
            total += part.size;
            parts.push_back(part);
        }
        if (parts.empty() || parts.size() > 1000 || unpackedSize == 0 || unpackedSize > MAX_SNAPSHOT_SIZE) {
            LogPrintf("Snapshot: the manifest is not valid.\n");
            why = "its list of files is not valid";
            return false;
        }
    } catch (const std::exception&) {
        LogPrintf("Snapshot: the manifest lacks a field.\n");
        why = "its list of files is not valid";
        return false;
    }

    // download
    LogPrintf("Snapshot: height %d, %d file(s), %.1f MB\n", pinned.height, parts.size(), total / 1e6);
    const fs::path dir = GetDataDir() / "snapshot-download";
    std::atomic<uint64_t> done(0);
    std::vector<std::string> paths;
    std::vector<bool> temporary;
    std::atomic<bool> downloading(true);
    std::thread reporter([&] {
        while (downloading) {
            status(strprintf(_("Fast start: downloading the snapshot, %s of %s"), Megabytes(done), Megabytes(total)) + TimeLeft(start, done, total));
            MilliSleep(500);
        }
    });
    bool ok = true;
    for (const Part& part : parts) {
        std::string path;
        bool temp = false;
        if (!BootstrapGetFile(part.file, part.size, part.sha256, dir.string(), &done, path, temp, error)) {
            ok = false;
            break;
        }
        paths.push_back(path);
        temporary.push_back(temp);
    }
    downloading = false;
    reporter.join();
    const int64_t downloaded = GetTimeMillis();
    boost::system::error_code ec;
    if (!ok) {
        LogPrintf("Snapshot: download failed (%s).%s\n", error, fStartOver ? "" : " The node synchronizes by validating the blocks.");
        if (!ShutdownRequested())
            fs::remove_all(dir, ec);
        why = "the download failed: " + error;
        return false;
    }

    // The program pins the published files: nothing else is unpacked. (The content hashes can
    // only be compared once the databases are opened, so other files would reach the database
    // code before that check.)
    if (!pinned.archive.empty()) {
        status(_("Fast start: checking the snapshot files..."));
        CSHA256 hasher;
        std::vector<unsigned char> buffer(1 << 20);
        bool readable = true;
        for (const std::string& path : paths) {
            FILE* file = fsbridge::fopen(fs::path(path), "rb");
            if (!file) {
                readable = false;
                break;
            }
            size_t n;
            while ((n = fread(buffer.data(), 1, buffer.size(), file)) > 0)
                hasher.Write(buffer.data(), n);
            fclose(file);
        }
        unsigned char digest[CSHA256::OUTPUT_SIZE];
        hasher.Finalize(digest);
        const std::string sha256 = HexStr(digest, digest + CSHA256::OUTPUT_SIZE);
        if (!readable || sha256 != pinned.archive) {
            LogPrintf("Snapshot: the files are not the ones that this program accepts (SHA-256 %s, accepted %s).%s\n",
                      readable ? sha256 : "unreadable", pinned.archive, fStartOver ? "" : " The node synchronizes by validating the blocks.");
            for (size_t i = 0; i < paths.size(); ++i) {
                if (temporary[i])
                    fs::remove(fs::path(paths[i]), ec);
            }
            fs::remove_all(dir, ec);
            why = "the files that are offered there are not the ones that this program accepts";
            return false;
        }
    }

    // unpack: the parts are one gzip stream. Beside the chain data: it takes their place when
    // it is all there.
    status(_("Fast start: unpacking the snapshot..."));
    const fs::path staging = GetDataDir() / STAGING_NAME;
    fs::remove_all(staging, ec);
    Unpacker unpacker(staging);
    size_t current = 0;
    FILE* in = nullptr;
    uint64_t size = 0;
    std::string gzipError;
    const bool unpacked = Gunzip(
        [&](unsigned char* buffer, size_t n) -> size_t {
            while (true) {
                if (!in) {
                    if (current >= paths.size())
                        return 0;
                    in = fsbridge::fopen(fs::path(paths[current]), "rb");
                    if (!in)
                        return 0;
                }
                const size_t got = fread(buffer, 1, n, in);
                if (got > 0)
                    return got;
                fclose(in);
                in = nullptr;
                ++current;
            }
        },
        [&](const unsigned char* data, size_t n) { return unpacker.Write(data, n); }, unpackedSize, size, gzipError);
    if (in)
        fclose(in);
    for (size_t i = 0; i < paths.size(); ++i) {
        if (temporary[i])
            fs::remove(fs::path(paths[i]), ec);
    }
    fs::remove_all(dir, ec);
    std::string unpackError = !unpacker.error.empty() ? unpacker.error : !gzipError.empty() ? gzipError : "";
    if (unpackError.empty() && (!unpacked || !unpacker.Finished() || size != unpackedSize))
        unpackError = "it is incomplete";
    for (const char* name : SNAPSHOT_DIRS) {
        if (unpackError.empty() && !fs::is_directory(staging / name, ec))
            unpackError = strprintf("it has no %s", name);
        SyncDirectory(staging / name);
    }
    if (!unpackError.empty()) {
        LogPrintf("Snapshot: cannot unpack (%s).%s\n", unpackError, fStartOver ? "" : " The node synchronizes by validating the blocks.");
        fs::remove_all(staging, ec);
        why = "it cannot be unpacked: " + unpackError;
        return false;
    }
    SyncDirectory(staging);
    SyncDirectory(staging / "blocks");
    TestCrashPoint("snapshot-unpacked");

    // From here on the data directory holds a snapshot that is not checked yet.
    if (!WriteMarker(1, fStartOver)) {
        LogPrintf("Snapshot: cannot write into the data directory.\n");
        fs::remove_all(staging, ec);
        why = "the data directory cannot be written to";
        return false;
    }
    std::string moveError;
    const MoveResult moved = MoveSnapshotIn(moveError);
    if (moved == MoveResult::NOT_BEGUN) {
        // (nothing of the node was touched: the marker and the unpacked snapshot go. The
        // marker first, and that on disk: a marker beside the untouched data of the node
        // would have them checked as a snapshot at the next start, and removed.)
        fs::remove(GetDataDir() / MARKER_NAME, ec);
        SyncDirectory(GetDataDir());
        fs::remove_all(staging, ec);
        why = "it cannot be moved into its place: " + moveError;
        return false;
    }
    if (moved == MoveResult::FAILED) {
        if (fStartOver) {
            // The block files and some databases of the node are gone, and the unpacked
            // snapshot is still there: the next start goes on with it (InstallSnapshotIfWanted).
            fMoveFailed = true;
            why = moveError;
        } else {
            // (a new node has nothing to lose: it synchronizes by validating the blocks)
            why = "it cannot be moved into its place: " + moveError;
            if (!RemoveInstalledSnapshot(why))
                fMoveFailed = true;   // (why is the message then: the start ends)
        }
        return false;
    }
    LogPrintf("Snapshot: %d files unpacked (%.1f MB). Download %.1f s, unpacking %.1f s. It is checked next.\n", unpacker.Files(), size / 1e6,
              (downloaded - start) / 1000.0, (GetTimeMillis() - downloaded) / 1000.0);
    fInstalledThisStart = true;
    return true;
}

} // namespace

bool StartOverFromSnapshot(std::function<void(const std::string&)> status, std::string& why, bool& fMoveFailed)
{
    return FetchAndInstallSnapshot(status, true, why, fMoveFailed);
}

bool InstallSnapshotIfWanted(std::function<void(const std::string&)> status, std::string& strCannotGoOn)
{
    const CChainParams::SnapshotParams& pinned = Params().Snapshot();
    strCannotGoOn.clear();
    if (MarkerExists()) {
        // An earlier start unpacked a snapshot and did not get to accept it (the program was
        // closed or stopped while it moved the snapshot in or checked it).
        bool fStartOver = false;
        const int attempts = ReadMarker(fStartOver);
        boost::system::error_code ec;
        const bool fStaged = fs::is_directory(GetDataDir() / STAGING_NAME, ec);
        std::string moveError;
        if (fStartOver) {
            // The snapshot took the place of the chain data of a node that ran. What it
            // replaced is gone, and with it the blocks before the snapshot: there is nothing
            // to fall back to but a synchronization from the first block. A start that was
            // stopped says nothing against the snapshot, so it is not removed for that,
            // however often: it is checked at every start until the check is done. (A
            // snapshot that fails its check is removed, as any.)
            if (attempts >= 2)
                LogPrintf("Snapshot: %d starts ended before this snapshot was checked. It took the place of the chain data of this node, so it is kept, and it is checked now.\n", attempts);
            WriteMarker(attempts + 1, true);
            // (the start was stopped while the snapshot took the place of the chain data: the rest of that)
            if (fStaged && MoveSnapshotIn(moveError) != MoveResult::DONE) {
                strCannotGoOn = strprintf(_("An earlier start had begun to put the chain state snapshot into the place of the chain data of this node, and this start cannot finish that: %s. The block files and the databases that the snapshot replaces may be removed already; the unpacked snapshot is kept in the data directory (snapshot-unpack). Mend what is in the way and start the program again: it goes on from there, and no option is needed."), moveError);
                return false;
            }
        } else {
            // The first chain data of a new node: it is checked once more; a snapshot on which
            // the program stopped twice is removed, and the node synchronizes by validating
            // the blocks, as it would without a snapshot.
            if (attempts >= 2 || pinned.height <= 0 || !WriteMarker(attempts + 1, false)) {
                LogPrintf("Snapshot: an earlier start left a snapshot that could not be checked.\n");
                RemoveInstalledSnapshot(strCannotGoOn);
                return false;
            }
            if (fStaged && MoveSnapshotIn(moveError) != MoveResult::DONE) {
                RemoveInstalledSnapshot(strCannotGoOn);
                return false;
            }
        }
        LogPrintf("Snapshot: an earlier start left a snapshot that was not checked yet. It is checked now.\n");
        fInstalledThisStart = true;
        return true;
    }
    // (an unpacked snapshot without its marker is one that no start will use: a start-over
    // that was stopped before it wrote the marker, on a node that was mended since)
    boost::system::error_code ec;
    fs::remove_all(GetDataDir() / STAGING_NAME, ec);
    std::string why;
    bool fMoveFailed = false;
    const bool fInstalled = FetchAndInstallSnapshot(status, false, why, fMoveFailed);
    // (a snapshot node that lost its chain state, see there: the move cannot be finished; or
    // a new node's snapshot that could be neither moved in nor removed)
    if (fMoveFailed)
        strCannotGoOn = !fSnapshotNotRemoved
            ? strprintf(_("The chain state snapshot is fetched and unpacked, and it could not be put into the place of the chain data of this node: %s. The block files and the databases that it has replaced so far are removed; the unpacked snapshot is kept in the data directory (snapshot-unpack). Mend what is in the way and start the program again: it goes on from there, and no option is needed."), why)
            : why;
    return fInstalled;
}

bool VerifyInstalledSnapshot(std::string& error, std::function<void(const std::string&)> status)
{
    if (!fInstalledThisStart)
        return true;
    const CChainParams::SnapshotParams& pinned = Params().Snapshot();
    status(_("Fast start: checking the snapshot (about a minute)..."));
    const int64_t start = GetTimeMillis();
    StateHashes hashes;
    if (!ComputeStateHashes(hashes, error))
        return false;
    const struct { const char* name; const uint256& have; const uint256& want; } checks[] = {
        {"block", hashes.block, pinned.block},
        {"unspent outputs", hashes.utxo, pinned.utxo},
        {"tokens", hashes.tokens, pinned.tokens},
        {"governance", hashes.governance, pinned.governance},
        {"block index", hashes.index, pinned.index},
    };
    if (hashes.height != pinned.height) {
        error = strprintf("the snapshot is at height %d, not %d", hashes.height, pinned.height);
        return false;
    }
    for (const auto& check : checks) {
        if (check.have != check.want) {
            error = strprintf("the %s of the snapshot have the hash %s, this program accepts %s", check.name, check.have.GetHex(), check.want.GetHex());
            return false;
        }
    }
    // Nothing but the active chain may be in the block index, and the places of the blocks in the
    // block files, which the hashes do not cover, must follow one another.
    {
        LOCK(cs_main);
        if ((int)mapBlockIndex.size() != pinned.height + 1) {
            error = strprintf("the block index of the snapshot has %d entries for %d blocks", mapBlockIndex.size(), pinned.height + 1);
            return false;
        }
        const CBlockIndex* previous = nullptr;
        for (const CBlockIndex* pindex = chainActive.Genesis(); pindex; pindex = chainActive.Next(pindex)) {
            const bool ordered = !previous || pindex->nFile > previous->nFile
                || (pindex->nFile == previous->nFile && pindex->nDataPos >= previous->nDataPos + 80 + 8);
            if (!(pindex->nStatus & BLOCK_HAVE_DATA) || pindex->nFile < 0 || pindex->nFile > 100000 || pindex->nDataPos < 8 || !ordered) {
                error = strprintf("the block index of the snapshot has no usable place for the block at height %d", pindex->nHeight);
                return false;
            }
            previous = pindex;
        }
        // The block tree database may hold the block index, the records of the block files and
        // flags. A transaction index or another index is not covered by the hashes: a snapshot
        // does not bring one.
        CDataStream ssAcceptance(SER_DISK, CLIENT_VERSION);
        ssAcceptance << DB_SNAPSHOT_HEIGHT;
        const std::vector<unsigned char> acceptanceKey(ssAcceptance.begin(), ssAcceptance.end());
        std::unique_ptr<CDBIterator> it(pblocktree->NewIterator());
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            RawRecord key;
            if (!it->GetKey(key) || key.bytes.empty()) {
                error = "cannot read the block index of the snapshot";
                return false;
            }
            const char kind = (char)key.bytes[0];
            // One more record passes: the record of an acceptance at the height of this very
            // snapshot, as MarkSnapshotAccepted() writes it (its two flags are flags). A start
            // that was stopped between that record and the removal of the marker left it.
            // It is let pass, not believed: the hashes above were compared all the same, and
            // none of them covers this record.
            if (key.bytes == acceptanceKey) {
                int height = -1;
                if (!pblocktree->Read(DB_SNAPSHOT_HEIGHT, height) || height != pinned.height) {
                    error = strprintf("the block index of the snapshot says that it was accepted at height %d", height);
                    return false;
                }
                continue;
            }
            if (kind != 'b' && kind != 'f' && kind != 'l' && kind != 'F' && kind != 'R') {
                error = strprintf("the block index of the snapshot has records of the kind '%c' (a snapshot is made by a node with txindex=0 and without other indexes)", isprint((unsigned char)kind) ? kind : '?');
                return false;
            }
        }
        it.reset();
        bool pruned = false;
        int lastFile = -1;
        CBlockFileInfo info;
        const CBlockIndex* tip = chainActive.Tip();
        if ((pblocktree->ReadFlag("prunedblockfiles", pruned) && pruned) || !pblocktree->ReadLastBlockFile(lastFile) || lastFile != tip->nFile
            || !pblocktree->ReadBlockFileInfo(lastFile, info) || info.nSize < tip->nDataPos + 80 || info.nSize > 0x7fffffff) {
            error = "the records of the block files of the snapshot do not fit its block index";
            return false;
        }
    }
    LogPrintf("Snapshot: check passed in %.1f s (%d unspent outputs)\n", (GetTimeMillis() - start) / 1000.0, hashes.coins);
    return true;
}
