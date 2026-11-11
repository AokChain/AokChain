// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "snapshot.h"

#include "bootstrap.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/merkle.h"
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

#include <map>
#include <thread>
#include <vector>

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
            if (it->GetKey(key) && !key.bytes.empty() && (char)key.bytes[0] == 'U') {
                batch.Erase(RawKey(key.bytes));
                ++erased;
            }
        }
        it.reset();
        ptokensdb->WriteBatch(batch, true);
        LogPrintf("Snapshot: %d token undo records removed\n", erased);
    }
    pblocktree->Write(DB_SNAPSHOT_HEIGHT, height);
    pblocktree->WriteFlag(FLAG_PAST_BLOCKS_MISSING, true);
    pblocktree->WriteFlag("txindex", fTxIndex);
    pblocktree->Sync();
    if (FILE* note = fsbridge::fopen(GetDataDir() / NO_PAST_BLOCKS_NAME, "wb")) {
        fprintf(note, "This node started from a chain state snapshot at height %d and does not have the blocks up to that height.\n", height);
        fclose(note);
    }
    boost::system::error_code ec;
    fs::remove(GetDataDir() / MARKER_NAME, ec);
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
    if (pblocktree->Read(DB_SNAPSHOT_HEIGHT, height)) {
        pblocktree->ReadFlag(FLAG_PAST_BLOCKS_MISSING, missing);
        pblocktree->ReadFlag(FLAG_RESCAN_PENDING, rescan);
        nSnapshotHeight = height;
        fPastBlocksMissing = missing;
        fRescanPending = rescan;
        LogPrintf("Snapshot: this node started from a snapshot at height %d; past blocks %s\n", height, missing ? "not downloaded" : "present");
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
            fclose(file);
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

/** Remove the chain databases and the block files, to put a snapshot in their place or to take one away. */
void WipeChainData()
{
    boost::system::error_code ec;
    for (const char* dir : SNAPSHOT_DIRS)
        fs::remove_all(GetDataDir() / dir, ec);
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

/** How often a start has tried to check the snapshot that is unpacked in the data directory. */
int ReadMarker()
{
    FILE* marker = fsbridge::fopen(GetDataDir() / MARKER_NAME, "rb");
    if (!marker)
        return 0;
    const int c = fgetc(marker);
    fclose(marker);
    return c >= '1' && c <= '9' ? c - '0' : 9;
}

bool WriteMarker(int attempts)
{
    FILE* marker = fsbridge::fopen(GetDataDir() / MARKER_NAME, "wb");
    if (!marker)
        return false;
    fputc('0' + attempts, marker);
    FileCommit(marker);
    fclose(marker);
    return true;
}

} // namespace

void RemoveInstalledSnapshot()
{
    boost::system::error_code ec;
    WipeChainData();
    fs::remove_all(GetDataDir() / "snapshot-download", ec);
    fs::remove(GetDataDir() / MARKER_NAME, ec);
    fInstalledThisStart = false;
    LogPrintf("Snapshot: removed. The node synchronizes by validating the blocks.\n");
}

bool InstallSnapshotIfWanted(std::function<void(const std::string&)> status)
{
    const CChainParams::SnapshotParams& pinned = Params().Snapshot();
    if (MarkerExists()) {
        // An earlier start unpacked a snapshot and did not get to accept it (the program was
        // closed or stopped while checking). It is checked once more; a snapshot on which the
        // program stopped twice is removed.
        const int attempts = ReadMarker();
        if (attempts >= 2 || pinned.height <= 0 || !WriteMarker(attempts + 1)) {
            LogPrintf("Snapshot: an earlier start left a snapshot that could not be checked.\n");
            RemoveInstalledSnapshot();
            return false;
        }
        LogPrintf("Snapshot: an earlier start left a snapshot that was not checked yet. It is checked now.\n");
        fInstalledThisStart = true;
        return true;
    }
    // -bootstrap=0 (the fast first synchronization of the settings page) turns the snapshot off too
    if (!gArgs.GetBoolArg("-snapshot", DEFAULT_SNAPSHOT) || !gArgs.GetBoolArg("-bootstrap", DEFAULT_BOOTSTRAP) || pinned.height <= 0)
        return false;
    if (!DataDirIsNew())
        return false; // a node with a chain goes on with what it has
    // The snapshot is made with the default indexes: other settings need the blocks.
    if (fReindex || gArgs.GetBoolArg("-reindex-chainstate", false) || gArgs.GetArg("-prune", 0) != 0
        || !gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX) || !gArgs.GetBoolArg("-tokenindex", DEFAULT_TOKENINDEX)
        || gArgs.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) || gArgs.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX)
        || gArgs.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
        LogPrintf("Snapshot: not used, because an option of this start needs the blocks to be validated here (reindex, prune or an index option).\n");
        return false;
    }
    if (!BootstrapHasSource()) {
        LogPrintf("Snapshot: not used, there is no address or folder to take it from.\n");
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
            LogPrintf("Snapshot: no usable %s (%s). The node synchronizes by validating the blocks.\n", MANIFEST_NAME, error);
            return false;
        }
        if (find_value(manifest, "format").get_int() != 1 || find_value(manifest, "network").get_str() != Params().NetworkIDString()) {
            LogPrintf("Snapshot: the manifest is for another format or network.\n");
            return false;
        }
        // a snapshot of another height or block cannot pass the check: do not fetch it at all
        if (find_value(manifest, "height").get_int() != pinned.height || uint256S(find_value(manifest, "block_hash").get_str()) != pinned.block) {
            LogPrintf("Snapshot: the server has a snapshot for height %d, this program accepts the one at height %d (%s). Not used.\n",
                      find_value(manifest, "height").get_int(), pinned.height, pinned.block.GetHex());
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
                return false;
            }
            for (char& c : part.sha256)
                c = (char)tolower((unsigned char)c);
            total += part.size;
            parts.push_back(part);
        }
        if (parts.empty() || parts.size() > 1000 || unpackedSize == 0 || unpackedSize > MAX_SNAPSHOT_SIZE) {
            LogPrintf("Snapshot: the manifest is not valid.\n");
            return false;
        }
    } catch (const std::exception&) {
        LogPrintf("Snapshot: the manifest lacks a field.\n");
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
        LogPrintf("Snapshot: download failed (%s). The node synchronizes by validating the blocks.\n", error);
        if (!ShutdownRequested())
            fs::remove_all(dir, ec);
        return false;
    }

    // unpack: the parts are one gzip stream
    status(_("Fast start: unpacking the snapshot..."));
    if (!WriteMarker(1)) {
        LogPrintf("Snapshot: cannot write into the data directory.\n");
        return false;
    }
    WipeChainData(); // the databases of a node that never got past the genesis block
    Unpacker unpacker(GetDataDir());
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
    if (!unpacked || !unpacker.Finished() || size != unpackedSize) {
        LogPrintf("Snapshot: cannot unpack (%s). The node synchronizes by validating the blocks.\n",
                  !unpacker.error.empty() ? unpacker.error : !gzipError.empty() ? gzipError : "it is incomplete");
        RemoveInstalledSnapshot();
        return false;
    }
    LogPrintf("Snapshot: %d files unpacked (%.1f MB). Download %.1f s, unpacking %.1f s. It is checked next.\n", unpacker.Files(), size / 1e6,
              (downloaded - start) / 1000.0, (GetTimeMillis() - downloaded) / 1000.0);
    fInstalledThisStart = true;
    return true;
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
        std::unique_ptr<CDBIterator> it(pblocktree->NewIterator());
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            RawRecord key;
            if (!it->GetKey(key) || key.bytes.empty()) {
                error = "cannot read the block index of the snapshot";
                return false;
            }
            const char kind = (char)key.bytes[0];
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
