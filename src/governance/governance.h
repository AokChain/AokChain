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

    /** Hash over every record of the database, in key order: the same on every node with the same chain.
     *  It covers the database only, not changes of an open level (see BeginChanges). */
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
    bool CommitChanges();
    void DiscardChanges();

    using CDBWrapper::Sync;

private:
    struct Changes {
        std::map<CScript, bool> freeze;                                 //!< script -> frozen
        std::map<std::pair<int, int>, std::optional<CAmount>> cost;     //!< (type, height) -> cost; none: erased
        std::map<int, std::optional<CScript>> fee;                      //!< height -> fee script; none: erased
        std::optional<unsigned int> numberFrozen;
    };
    //! Open levels, the innermost last. Guarded by cs.
    std::vector<Changes> vChanges;
    //! Guards vChanges. Readers outside cs_main (RPC) may run while a block is connected.
    mutable CCriticalSection cs;

    // Reads that see the open levels. Hold cs.
    bool GetFreezeEntry(const CScript& script, bool& frozen) const;
    unsigned int GetNumberFrozen() const;
    bool GetCostEntry(int type, int height, CAmount& cost) const;
    bool GetFeeEntry(int height, CScript& script) const;
    // Writes into the innermost level, or into the batch when no level is open. Hold cs.
    void SetFreezeEntry(CDBBatch& batch, const CScript& script, bool frozen);
    void SetNumberFrozen(CDBBatch& batch, unsigned int number);
    void SetCostEntry(CDBBatch& batch, int type, int height, std::optional<CAmount> cost);
    void SetFeeEntry(CDBBatch& batch, int height, std::optional<CScript> script);
    //! Writes the batch when no level is open. Hold cs.
    bool Finish(CDBBatch& batch);
};

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

    bool Commit()
    {
        if (m_done) return true;
        m_done = true;
        return m_governance ? m_governance->CommitChanges() : true;
    }

private:
    CGovernance* m_governance;
    bool m_done = false;
};

#endif /* AOKCHAIN_GOVERNANCE_H */
