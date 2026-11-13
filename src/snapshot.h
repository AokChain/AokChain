// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_SNAPSHOT_H
#define AOKCHAIN_SNAPSHOT_H

#include "uint256.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class CBlockIndex;
namespace boost { class thread_group; }

/**
 * Chain state snapshot.
 *
 * A new node can start from the state that replaying the blocks up to a fixed
 * height would produce, instead of replaying them: the UTXO set, the block
 * index, and the token and governance databases. The snapshot is fetched like
 * the block archives (-bootstrapurl mirrors, -bootstrapdir, or the built-in
 * address of the main network).
 *
 * What makes a snapshot acceptable is compiled into the program, not stored
 * on the server: the height, the block hash, and hashes over the content of
 * each of those databases (CChainParams::Snapshot()). After unpacking, the
 * node computes the hashes from the databases it loaded and compares. On any
 * difference the snapshot is removed and the node synchronizes by replaying
 * blocks, as it would without a snapshot. The SHA-256 of the files in the
 * manifest only detects a damaged download.
 *
 * A node that started from a snapshot has no block files for the heights up
 * to the snapshot ("past blocks"). It validates and creates new blocks
 * without them. Whatever does need them (a wallet rescan, serving old blocks
 * to peers, looking up an old transaction) is refused with a clear message
 * until they are fetched: FetchPastBlocks() downloads the block archives and
 * stores the blocks at the positions that the block index records, checking
 * every block against its hash and merkle root. Nothing is replayed.
 */

static const bool DEFAULT_SNAPSHOT = true;
static const bool DEFAULT_FETCHBLOCKS = false;

/** The hashes that identify the chain state at a height. */
struct StateHashes
{
    int height;
    uint256 block;          //!< hash of the block at that height
    uint256 utxo;           //!< every unspent output with all the fields that the chainstate stores
    uint256 utxoLegacy;     //!< hash_serialized_2 of gettxoutsetinfo (does not cover every field)
    uint256 tokens;         //!< token database, without undo and mempool records
    uint256 governance;     //!< governance database
    uint256 index;          //!< block index of the active chain: headers and proof-of-stake fields
    uint64_t coins;         //!< number of unspent outputs

    StateHashes() : height(0), coins(0) {}
};

/** Compute the hashes from the databases of this node, at its current tip. Flushes first. */
bool ComputeStateHashes(StateHashes& hashes, std::string& error);

/** True while the block files up to the snapshot height are absent. */
bool PastBlocksMissing();
/** True when the block that this index entry stands for is one of the absent past blocks. */
bool IsPastBlockMissing(const CBlockIndex* pindex);
/** Height of the snapshot that this node started from; -1 when it did not. */
int SnapshotHeight();
/** What to tell someone whose request needs the past blocks. */
std::string PastBlocksMessage();

struct PastBlocksInfo
{
    bool missing;           //!< the past blocks are not all there
    bool fetching;          //!< a download is running
    int snapshotHeight;
    uint64_t bytesTotal;
    uint64_t bytesDone;
    int blocksStored;
    std::string error;      //!< why the last download stopped
    PastBlocksInfo() : missing(false), fetching(false), snapshotHeight(-1), bytesTotal(0), bytesDone(0), blocksStored(0) {}
};
PastBlocksInfo GetPastBlocksInfo();

/** Download the past blocks in the background. False when they are there, or a download runs. */
bool FetchPastBlocks(std::string& error);
/** The same, waiting until it is done (used at start with -fetchblocks). */
bool FetchPastBlocksNow(std::string& error);

/**
 * At start, before the databases are opened: when the data directory is new and a snapshot is
 * pinned for this network, fetch and unpack it. Returns true when a snapshot was unpacked; it is
 * not trusted until VerifyInstalledSnapshot() has passed.
 */
bool InstallSnapshotIfWanted(std::function<void(const std::string&)> status);
/** After the databases are loaded: compare their content hashes with the pinned ones. */
bool VerifyInstalledSnapshot(std::string& error, std::function<void(const std::string&)> status);
/** Remove what InstallSnapshotIfWanted() unpacked (the databases must be closed). */
void RemoveInstalledSnapshot();
/** Record that the node runs from a verified snapshot whose past blocks are absent. */
void MarkSnapshotAccepted();
/** Before the databases are open: this data directory is of a snapshot node that has not fetched the past blocks. */
bool SnapshotNodeWithoutPastBlocks();
/** While true the block files up to the snapshot height need not exist (read from the databases, for the start). */
bool SnapshotBlockFilesMayBeMissing();
/**
 * A wallet whose history reaches below the snapshot height was loaded while the past blocks were
 * absent: it is scanned when they have been fetched (the hook), or at the next start.
 */
bool WalletRescanPending();
void SetWalletRescanPending(bool pending);
void SetPastBlocksRescanHook(std::function<void()> hook);
/** Read the snapshot state of the data directory (after the block index is loaded). */
void LoadSnapshotState(boost::thread_group& threadGroup);

#endif // AOKCHAIN_SNAPSHOT_H
