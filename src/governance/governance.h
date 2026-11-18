// Copyright (c) 2022 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ROZA_VALIDATORS_H
#define ROZA_VALIDATORS_H

#include <script/script.h>
#include <chainparams.h>
#include <dbwrapper.h>
#include <chain.h>
#include <sync.h>

#include <memory>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#define GOVERNANCE_MARKER 71
#define GOVERNANCE_ACTION 65
#define GOVERNANCE_FREEZE 70
#define GOVERNANCE_UNFREEZE 85
#define GOVERNANCE_COST 67
#define GOVERNANCE_FEE 102

#define GOVERNANCE_COST_ROOT 1
#define GOVERNANCE_COST_REISSUE 2
#define GOVERNANCE_COST_UNIQUE 3
#define GOVERNANCE_COST_SUB 4
#define GOVERNANCE_COST_USERNAME 5

/**
 * What a commit of governance changes replaced: for every key that it wrote or erased, what the
 * key held before. It is the exact inverse of the commit as a whole, whatever the number and
 * the order of the commands that made it, and whether a command changed anything.
 *  - The journal that keeps the database in step with the chain state is made of these
 *    (CGovernance::BeginChainStateFlush).
 *  - The one of a block's commit is kept from the moment the block is connected
 *    (CGovernance::WriteBlockUndo). When the block is disconnected, the governance state is
 *    put back from it to what it was before the block (CGovernance::ApplyBlockUndo).
 */
struct CGovernanceUndo {
    struct Freeze { CScript script; bool had; bool frozen; };
    struct Cost { int type; int height; bool had; CAmount cost; };
    struct Fee { int height; bool had; CScript script; };
    std::vector<Freeze> freeze;
    std::vector<Cost> cost;
    std::vector<Fee> fee;
    bool hadNumber = false;
    bool setNumber = false;
    unsigned int number = 0;
    //! (not stored) a commit went to the database, and this is what it replaced
    bool fSet = false;

    template<typename Stream>
    void Serialize(Stream &s) const {
        WriteCompactSize(s, freeze.size());
        for (const Freeze& item : freeze)
            s << item.script << item.had << item.frozen;
        WriteCompactSize(s, cost.size());
        for (const Cost& item : cost)
            s << item.type << item.height << item.had << item.cost;
        WriteCompactSize(s, fee.size());
        for (const Fee& item : fee)
            s << item.height << item.had << item.script;
        s << setNumber << hadNumber << number;
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        freeze.resize(ReadCompactSize(s));
        for (Freeze& item : freeze)
            s >> item.script >> item.had >> item.frozen;
        cost.resize(ReadCompactSize(s));
        for (Cost& item : cost)
            s >> item.type >> item.height >> item.had >> item.cost;
        fee.resize(ReadCompactSize(s));
        for (Fee& item : fee)
            s >> item.height >> item.had >> item.script;
        s >> setNumber >> hadNumber >> number;
    }
};

class CGovernance : CDBWrapper 
{
public:
    CGovernance(size_t nCacheSize, bool fMemory, bool fWipe);
    bool Init(bool fWipe, const CChainParams& chainparams);

    // Statistics
    unsigned int GetNumberOfFrozenScripts();
    
    // Managing freeze list
    bool FreezeScript(CScript script);
    bool UnfreezeScript(CScript script);
    bool RevertFreezeScript(CScript script);
    bool RevertUnfreezeScript(CScript script);
    bool ScriptExist(CScript script);
    bool CanSend(CScript script);

    bool UpdateCost(CAmount cost, int type, int height);
    bool RevertUpdateCost(int type, int height);
    CAmount GetCost(int type);

    bool UpdateFeeScript(CScript script, int height);
    bool RevertUpdateFeeScript(int height);
    CScript GetFeeScript();

    bool DumpFreezeStats(std::vector< std::pair< CScript, bool > > *FreezeVector);
    bool GetFrozenScripts(std::vector< CScript > *FreezeVector);

    /** Hash over every record of the governance state in the database, in key order: the same on
     *  every node with the same chain. It covers the database only, not changes of an open level
     *  (see BeginChanges), and not the journal that keeps it in step with the chain state. */
    uint256 ContentHash();

    /**
     * Levels of changes. While a level is open, the commands of a block go into it instead
     * of the database, and every read sees them: a later transaction of the same block sees
     * the command of an earlier one, as before. CommitChanges() writes the changes of the
     * level as one batch, or hands them to the level below when there is one. DiscardChanges()
     * drops them.
     *
     * Before, every command was written to the database the moment it was read from a block,
     * so a block that turned out invalid after that (a coinbase that pays too much, a script
     * that fails), a check of a block that is not connected (TestBlockValidity, used for block
     * templates) and the check of the last blocks at startup changed the state of the node, and
     * the node then judged the next blocks differently from the rest of the network.
     * Use CGovernanceChanges. The levels are not saved: they live while cs_main is held.
     */
    void BeginChanges();
    //! pundo: when the commit goes to the database (the outermost level), what it replaced
    bool CommitChanges(CGovernanceUndo* pundo = nullptr);
    void DiscardChanges();

    /**
     * Puts back what the commit of a block replaced: the governance state is then exactly the
     * one before that block, for every command and any number and order of commands, also
     * commands that changed nothing. (The Revert... functions above guess the state before a
     * command from the command; they are used only for blocks that a version without this
     * record connected.) Like the commands, it goes to the open level of changes.
     */
    bool ApplyBlockUndo(const CGovernanceUndo& undo);

    /**
     * The records of what the blocks replaced, by block. They are kept in a database of their
     * own, next to this one (<datadir>/governanceundo): this database holds the governance
     * state and nothing else, every record of it counts for the hash that nodes compare, and
     * the block index and the chain state are files that a snapshot is made of and that other
     * versions read. A version that does not know the records never looks at them, and a
     * snapshot cannot bring any.
     *  - WriteBlockUndo() is on disk when it returns: a block is never connected for good
     *    without its record.
     *  - A record belongs to one block (its hash) and is written again every time the block is
     *    connected. It is read only when that block is disconnected, which is when the state
     *    is the one after that block.
     *  - The database is emptied with this one (a rebuild of the chain state connects every
     *    block again and writes every record again).
     * ReadUndoSince()/WriteUndoSince(): the block that the chain state was at when a version
     * that keeps the records took the node over (null: from the first block on), and the
     * script of the master address then (the records are kept for the blocks that spend
     * from it). That block and the blocks before it may have no record; a later block
     * without one is damage. See LoadGovernanceUndoSince() in validation.cpp.
     */
    bool WriteBlockUndo(const uint256& hashBlock, const CGovernanceUndo& undo);
    bool ReadBlockUndo(const uint256& hashBlock, CGovernanceUndo& undo) const;
    bool EraseBlockUndo(const uint256& hashBlock);
    bool ReadUndoSince(uint256& hashBlock, CScript& master) const;
    bool WriteUndoSince(const uint256& hashBlock, const CScript& master);

    /**
     * Keeping in step with the chain state. The commands of a block are written here when the
     * block is connected or disconnected; the chain state (the coins database) is written much
     * later, at its next flush. After a stop without a flush (a kill, a crash, a power failure)
     * this database was therefore that of a later block than the chain state, and the blocks
     * in between were judged again with it: an address that is frozen later counted as frozen
     * already, and a valid block was rejected and marked invalid.
     *
     * Every commit to the database now also records what it replaced (a journal), and with
     * the first record the block that the chain state was at:
     *   BeginChainStateFlush(tip)  before the chain state is written for this tip (synced: the
     *                              chain state is never ahead of what this database can be
     *                              brought back to)
     *   EndChainStateFlush(tip)    after it is on disk: the journal is not needed any more
     *   RecoverTo(tip, fInStep)    at startup, with the block that the chain state is at: undoes
     *                              the journal when the chain state is still at the block that
     *                              the journal began at.
     * When the chain state is at neither that block nor the block of a flush in progress, the
     * journal does not explain the state. If a version without the journal has written the
     * chain state in between (fInStep false, see CCoinsViewDB::IsInStep), what is left of the
     * journal says nothing: it is dropped and the database is taken to match the chain state,
     * as it always was. If this version wrote the chain state last, RecoverTo() returns false:
     * the database cannot be brought to the chain state, both have to be built again.
     * The journal exists only between two writes of the chain state; after a clean stop the
     * database holds nothing that an earlier version does not know.
     * RecoverTo() also returns false when the journal cannot be read (strError says what).
     */
    bool BeginChainStateFlush(const uint256& hashTip);
    bool EndChainStateFlush(const uint256& hashTip);
    bool RecoverTo(const uint256& hashChainStateTip, bool fInStep, std::string& strError);
    //! Whether this version began a write of the chain state for this block and did not get to
    //! note its end (asked before RecoverTo(), which takes the note away).
    bool ChainStateFlushBegun(const uint256& hashTip) const;
    //! Whether the state was written since the chain state was (a journal is kept).
    bool HasJournal() const;
    //! Whether Init() found no database and made this one without being asked to (fWipe), at
    //! this start or at an earlier one that did not confirm it: see ConfirmNew().
    bool IsNew() const { return fNew; }
    /**
     * A database that Init() made without being asked to is right beside a chain state that
     * holds no block. Beside one that holds blocks it is not the database that belonged to
     * them, and has to be built again. It is not marked as initialised until the caller has
     * found the first to be the case: a start that ends on the second finds it new again.
     */
    bool ConfirmNew();

    using CDBWrapper::Sync;

private:
    struct Changes {
        std::map<CScript, std::optional<bool>> freeze;                  //!< script -> frozen; none: erased
        std::map<std::pair<int, int>, std::optional<CAmount>> cost;     //!< (type, height) -> cost; none: erased
        std::map<int, std::optional<CScript>> fee;                      //!< height -> fee script; none: erased
        std::optional<unsigned int> numberFrozen;
    };
    //! Open levels, the innermost last. Guarded by cs.
    std::vector<Changes> vChanges;
    //! What the blocks replaced (see WriteBlockUndo).
    std::unique_ptr<CDBWrapper> m_undo;
    //! See IsNew().
    bool fNew = false;
    //! Guards vChanges. Readers outside cs_main (RPC) may run while a block is connected.
    mutable CCriticalSection cs;

    // Reads that see the open levels. Hold cs.
    bool GetFreezeEntry(const CScript& script, bool& frozen) const;
    unsigned int GetNumberFrozen() const;
    bool GetCostEntry(int type, int height, CAmount& cost) const;
    bool GetFeeEntry(int height, CScript& script) const;
    // Writes into the innermost level, or into the batch when no level is open. Hold cs.
    void SetFreezeEntry(CDBBatch& batch, const CScript& script, bool frozen);
    void EraseFreezeEntry(CDBBatch& batch, const CScript& script);
    void SetNumberFrozen(CDBBatch& batch, unsigned int number);
    void SetCostEntry(CDBBatch& batch, int type, int height, std::optional<CAmount> cost);
    void SetFeeEntry(CDBBatch& batch, int height, std::optional<CScript> script);
    //! Writes the batch when no level is open. Hold cs.
    bool Finish(CDBBatch& batch);

    //! Number of the next journal record. Guarded by cs.
    uint32_t nJournalNext = 0;
    //! The block that the chain state was last written for (or loaded at). Guarded by cs.
    uint256 hashChainState;
    //! Puts what the changes replace in the database into the batch, as a journal record. Hold cs.
    CGovernanceUndo Replaced(const Changes& changes) const;
    void WriteJournal(CDBBatch& batch, const CGovernanceUndo& record);
    //! Erases the journal records and the pending mark in the batch. Hold cs.
    void EraseJournal(CDBBatch& batch);
};

/**
 * The scripts that the freeze commands of a transaction name: data outputs OP_RETURN <len> 'G' 'A'
 * 'F' <length> <script>, read as ConnectBlock() reads them. They take effect only when the
 * transaction spends a coin of the governance master address (see ConnectBlock()).
 */
std::vector<CScript> GovernanceFreezeScripts(const CTransaction& tx);

/**
 * Opens a level of governance changes (CGovernance::BeginChanges) and discards it when it
 * goes out of scope, unless Commit() was called: every early return of a failed check
 * leaves the governance state as it was.
 */
class CGovernanceChanges
{
public:
    explicit CGovernanceChanges(CGovernance* governance) : m_governance(governance)
    {
        if (m_governance) m_governance->BeginChanges();
    }
    ~CGovernanceChanges()
    {
        if (m_governance && !m_done) m_governance->DiscardChanges();
    }
    CGovernanceChanges(const CGovernanceChanges&) = delete;
    CGovernanceChanges& operator=(const CGovernanceChanges&) = delete;

    bool Commit(CGovernanceUndo* pundo = nullptr)
    {
        if (m_done) return true;
        m_done = true;
        return m_governance ? m_governance->CommitChanges(pundo) : true;
    }

private:
    CGovernance* m_governance;
    bool m_done = false;
};

#endif /* AOKCHAIN_GOVERNANCE_H */
