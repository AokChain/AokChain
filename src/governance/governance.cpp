// Copyright (c) 2022 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <unordered_map>

#include <governance/governance.h>
#include <primitives/transaction.h>
#include <utilstrencodings.h>
#include <script/script.h>
#include <chainparams.h>
#include <core_io.h>
#include <amount.h>
#include <base58.h>
#include <chain.h>
#include <hash.h>
#include <util.h>

static const CScript DUMMY_SCRIPT = CScript() << ParseHex("6885777789"); 
static const int DUMMY_TYPE = 0;

static const char DB_NUMBER_FROZEN = 'N';
static const char DB_FEE_ADDRESS = 'f';
static const char DB_ADDRESS = 'a';
static const char DB_COST = 'c';

static const char DB_GOVERNANCE_INIT  = 'G';

// Keeping in step with the chain state (see BeginChainStateFlush in governance.h). These
// records exist only between two writes of the chain state and are not part of the governance
// state: ContentHash() leaves them out.
static const char DB_CHAINSTATE_PENDING = 'P';  //!< block that the chain state is being flushed for
static const char DB_JOURNAL = 'J';             //!< (DB_JOURNAL, number) -> what a commit replaced
static const char DB_JOURNAL_BASE = 'K';        //!< block that the chain state was at when the journal began

namespace {
    struct FreezeEntry {
        char key;
        CScript script;

        FreezeEntry() : key(DB_ADDRESS), script(DUMMY_SCRIPT) {}
        FreezeEntry(CScript script) : key(DB_ADDRESS), script(script) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << script;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> script;
        }
    };

    struct FreezeDetails {
        bool frozen;

        FreezeDetails() : frozen(true) {}
        FreezeDetails(bool frozen) : frozen(frozen) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << frozen;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> frozen;
        }
    };

    struct CostEntry {
        char key;
        int type;
        int height;

        CostEntry() : key(DB_COST), type(DUMMY_TYPE), height(0) {}
        CostEntry(int type, int height) : key(DB_COST), type(type), height(height) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << type;
            s << height;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> type;
            s >> height;
        }
    };

    struct CostDetails {
        CAmount cost;

        CostDetails() : cost(0) {}
        CostDetails(CAmount cost) : cost(cost) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << cost;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> cost;
        }
    };

    struct FeeEntry {
        char key;
        int height;

        FeeEntry() : key(DB_FEE_ADDRESS), height(0) {}
        FeeEntry(int height) : key(DB_FEE_ADDRESS), height(height) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << height;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> height;
        }
    };

    struct FeeDetails {
        CScript script;

        FeeDetails() : script(DUMMY_SCRIPT) {}
        FeeDetails(CScript script) : script(script) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << script;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> script;
        }
    };
}

namespace {
    struct JournalKey {
        char key;
        uint32_t number;

        JournalKey() : key(DB_JOURNAL), number(0) {}
        explicit JournalKey(uint32_t number) : key(DB_JOURNAL), number(number) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << number;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> number;
        }
    };

    /** The values that one commit replaced (see CGovernanceUndo in governance.h). */
    typedef CGovernanceUndo JournalRecord;
}

CGovernance::CGovernance(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "governance", nCacheSize, fMemory, fWipe),
    m_undo(new CDBWrapper(GetDataDir() / "governanceundo", 1 << 20, fMemory, fWipe))
{
}

// The database of what the blocks replaced (see WriteBlockUndo in governance.h)
static const char DB_BLOCK_UNDO = 'u';   //!< (DB_BLOCK_UNDO, block) -> CGovernanceUndo
static const char DB_UNDO_SINCE = 's';   //!< block from which on every connected block has its record

bool CGovernance::WriteBlockUndo(const uint256& hashBlock, const CGovernanceUndo& undo)
{
    return m_undo->Write(std::make_pair(DB_BLOCK_UNDO, hashBlock), undo, true);
}

bool CGovernance::ReadBlockUndo(const uint256& hashBlock, CGovernanceUndo& undo) const
{
    return m_undo->Read(std::make_pair(DB_BLOCK_UNDO, hashBlock), undo);
}

bool CGovernance::EraseBlockUndo(const uint256& hashBlock)
{
    return m_undo->Erase(std::make_pair(DB_BLOCK_UNDO, hashBlock), true);
}

bool CGovernance::ReadUndoSince(uint256& hashBlock, CScript& master) const
{
    std::pair<uint256, CScript> since;
    if (!m_undo->Read(DB_UNDO_SINCE, since))
        return false;
    hashBlock = since.first;
    master = since.second;
    return true;
}

bool CGovernance::WriteUndoSince(const uint256& hashBlock, const CScript& master)
{
    return m_undo->Write(DB_UNDO_SINCE, std::make_pair(hashBlock, master), true);
}

bool CGovernance::Init(bool fWipe, const CChainParams& chainparams) {
    bool init;

    // Bug fix
    CostEntry fix_entry = CostEntry(GOVERNANCE_COST_REISSUE, 0);
    if (Exists(fix_entry)) {
        CostDetails details = CostDetails();
        Read(fix_entry, details);
        if (details.cost != chainparams.ReissueFeeAmount()) {
            LogPrintf("Governance: Found cost bug, fixing it\n");
            CDBBatch fix_batch(*this);
            fix_batch.Erase(CostEntry(GOVERNANCE_COST_REISSUE, 0));
            fix_batch.Erase(CostEntry(GOVERNANCE_COST_SUB, 0));
            fix_batch.Write(CostEntry(GOVERNANCE_COST_REISSUE, 0), CostDetails(chainparams.ReissueFeeAmount()));
            fix_batch.Write(CostEntry(GOVERNANCE_COST_SUB, 0), CostDetails(chainparams.SubFeeAmount()));
            WriteBatch(fix_batch);
        }
    }

    if (fWipe || Read(DB_GOVERNANCE_INIT, init) == false || init == false) {
        LogPrintf("Governance: Creating new database\n");

        CDBBatch batch(*this);

        batch.Write(DB_NUMBER_FROZEN, 0);

        // Add dummy entries will be first for searching the database
        batch.Write(FreezeEntry(), FreezeDetails());
        batch.Write(CostEntry(), CostDetails());

        // Add initial token issuance cost values
        batch.Write(CostEntry(GOVERNANCE_COST_ROOT, 0), CostDetails(chainparams.RootFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_REISSUE, 0), CostDetails(chainparams.ReissueFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_UNIQUE, 0), CostDetails(chainparams.UniqueFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_SUB, 0), CostDetails(chainparams.SubFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_USERNAME, 0), CostDetails(chainparams.UsernameFeeAmount()));

        // Add initial token fee address from chainparams
        CTxDestination destination = DecodeDestination(Params().TokenFeeAddress());
        CScript feeScript = GetScriptForDestination(destination);
        batch.Write(FeeEntry(), FeeDetails(feeScript));

        batch.Write(DB_GOVERNANCE_INIT, true);
        WriteBatch(batch);
        fNew = !fWipe;
    }

    return true;
}

// ---- Levels of changes (see BeginChanges in governance.h)

void CGovernance::BeginChanges()
{
    LOCK(cs);
    vChanges.emplace_back();
}

void CGovernance::DiscardChanges()
{
    LOCK(cs);
    assert(!vChanges.empty());
    vChanges.pop_back();
}

bool CGovernance::CommitChanges(CGovernanceUndo* pundo)
{
    LOCK(cs);
    assert(!vChanges.empty());
    Changes changes = std::move(vChanges.back());
    vChanges.pop_back();

    if (!vChanges.empty()) {
        // to the level below
        Changes& outer = vChanges.back();
        for (const auto& item : changes.freeze)
            outer.freeze[item.first] = item.second;
        for (const auto& item : changes.cost)
            outer.cost[item.first] = item.second;
        for (const auto& item : changes.fee)
            outer.fee[item.first] = item.second;
        if (changes.numberFrozen)
            outer.numberFrozen = changes.numberFrozen;
        return true;
    }

    // to the database: what it replaces is the inverse of this commit (for the caller, who
    // keeps it with the block, and for the journal)
    const CGovernanceUndo record = Replaced(changes);
    if (pundo) {
        *pundo = record;
        pundo->fSet = true;
    }
    if (changes.freeze.empty() && changes.cost.empty() && changes.fee.empty() && !changes.numberFrozen)
        return true;

    // as one batch, with what it replaces (see BeginChainStateFlush)
    CDBBatch batch(*this);
    WriteJournal(batch, record);
    for (const auto& item : changes.freeze) {
        if (item.second)
            batch.Write(FreezeEntry(item.first), FreezeDetails(*item.second));
        else
            batch.Erase(FreezeEntry(item.first));
    }
    for (const auto& item : changes.cost) {
        if (item.second)
            batch.Write(CostEntry(item.first.first, item.first.second), CostDetails(*item.second));
        else
            batch.Erase(CostEntry(item.first.first, item.first.second));
    }
    for (const auto& item : changes.fee) {
        if (item.second)
            batch.Write(FeeEntry(item.first), FeeDetails(*item.second));
        else
            batch.Erase(FeeEntry(item.first));
    }
    if (changes.numberFrozen)
        batch.Write(DB_NUMBER_FROZEN, *changes.numberFrozen);
    return WriteBatch(batch);
}

// ---- Keeping in step with the chain state (see BeginChainStateFlush in governance.h)

CGovernanceUndo CGovernance::Replaced(const Changes& changes) const
{
    AssertLockHeld(cs);
    CGovernanceUndo record;
    for (const auto& item : changes.freeze) {
        FreezeDetails details;
        const bool had = Read(FreezeEntry(item.first), details);
        record.freeze.push_back({item.first, had, had && details.frozen});
    }
    for (const auto& item : changes.cost) {
        CostDetails details;
        const bool had = Read(CostEntry(item.first.first, item.first.second), details);
        record.cost.push_back({item.first.first, item.first.second, had, had ? details.cost : 0});
    }
    for (const auto& item : changes.fee) {
        FeeDetails details;
        const bool had = Read(FeeEntry(item.first), details);
        record.fee.push_back({item.first, had, had ? details.script : CScript()});
    }
    if (changes.numberFrozen) {
        record.setNumber = true;
        record.hadNumber = Read(DB_NUMBER_FROZEN, record.number);
    }
    return record;
}

void CGovernance::WriteJournal(CDBBatch& batch, const CGovernanceUndo& record)
{
    AssertLockHeld(cs);
    if (nJournalNext == 0)
        batch.Write(DB_JOURNAL_BASE, hashChainState);
    batch.Write(JournalKey(nJournalNext++), record);
}

bool CGovernance::ApplyBlockUndo(const CGovernanceUndo& undo)
{
    LOCK(cs);
    CDBBatch batch(*this);
    for (const CGovernanceUndo::Freeze& item : undo.freeze) {
        if (item.had)
            SetFreezeEntry(batch, item.script, item.frozen);
        else
            EraseFreezeEntry(batch, item.script);
    }
    for (const CGovernanceUndo::Cost& item : undo.cost)
        SetCostEntry(batch, item.type, item.height, item.had ? std::optional<CAmount>(item.cost) : std::nullopt);
    for (const CGovernanceUndo::Fee& item : undo.fee)
        SetFeeEntry(batch, item.height, item.had ? std::optional<CScript>(item.script) : std::nullopt);
    if (undo.setNumber) {
        // (the database has this record from its first day on: Init())
        if (!undo.hadNumber)
            return error("%s: the record of a block says that there was no number of frozen scripts", __func__);
        SetNumberFrozen(batch, undo.number);
    }
    return Finish(batch);
}

void CGovernance::EraseJournal(CDBBatch& batch)
{
    AssertLockHeld(cs);
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(JournalKey()); it->Valid(); it->Next()) {
        JournalKey key;
        if (!it->GetKey(key) || key.key != DB_JOURNAL)
            break;
        batch.Erase(key);
    }
    batch.Erase(DB_JOURNAL_BASE);
    batch.Erase(DB_CHAINSTATE_PENDING);
    nJournalNext = 0;
}

bool CGovernance::BeginChainStateFlush(const uint256& hashTip)
{
    LOCK(cs);
    CDBBatch batch(*this);
    batch.Write(DB_CHAINSTATE_PENDING, hashTip);
    // On disk before the chain state is written: the chain state must never be ahead of what
    // this database can be brought back to.
    return WriteBatch(batch, true);
}

bool CGovernance::EndChainStateFlush(const uint256& hashTip)
{
    LOCK(cs);
    CDBBatch batch(*this);
    EraseJournal(batch);
    hashChainState = hashTip;
    return WriteBatch(batch);
}

bool CGovernance::HasJournal() const
{
    LOCK(cs);
    return nJournalNext != 0;
}

bool CGovernance::ChainStateFlushBegun(const uint256& hashTip) const
{
    LOCK(cs);
    uint256 hashPending;
    return Read(DB_CHAINSTATE_PENDING, hashPending) && hashPending == hashTip;
}

bool CGovernance::RecoverTo(const uint256& hashChainStateTip, bool fInStep, std::string& strError)
{
    LOCK(cs);
    assert(vChanges.empty());

    hashChainState = hashChainStateTip;
    uint256 hashPending, hashBase;
    const bool fPending = Read(DB_CHAINSTATE_PENDING, hashPending);
    const bool fBase = Read(DB_JOURNAL_BASE, hashBase);

    std::map<uint32_t, JournalRecord> journal;
    {
        std::unique_ptr<CDBIterator> it(NewIterator());
        for (it->Seek(JournalKey()); it->Valid(); it->Next()) {
            JournalKey key;
            if (!it->GetKey(key) || key.key != DB_JOURNAL)
                break;
            JournalRecord record;
            if (!it->GetValue(record)) {
                strError = "a journal record of the governance database cannot be read";
                return false;
            }
            journal.emplace(key.number, std::move(record));
        }
    }
    nJournalNext = 0;
    if (journal.empty() && !fPending && !fBase)
        return true;

    CDBBatch batch(*this);
    if (fPending && hashPending == hashChainStateTip) {
        // The write of the chain state went through, only its end was not noted here: the
        // state is that of this block, the journal is of blocks before it.
    } else if (!fBase || hashBase != hashChainStateTip) {
        // The chain state is not at the block that the journal began at.
        if (fBase && fInStep) {
            // This version wrote the chain state last, and it never does so without a note
            // here: the journal does not explain the state.
            strError = strprintf("the journal of the governance database began at block %s, the chain state is that of block %s",
                                 hashBase.ToString(), hashChainStateTip.ToString());
            return false;
        }
        // A version that keeps no journal has written the chain state since. What is left of
        // the journal says nothing about the state now; the database is taken to match the
        // chain state, as it always was.
        if (!journal.empty())
            LogPrintf("Governance: the chain state was written by another version since the journal began; the journal is dropped\n");
    } else {
        // The chain state is where it was when the journal began: undo what was written here
        // since, the newest first.
        for (auto it = journal.rbegin(); it != journal.rend(); ++it) {
            const JournalRecord& record = it->second;
            for (const JournalRecord::Freeze& item : record.freeze) {
                if (item.had)
                    batch.Write(FreezeEntry(item.script), FreezeDetails(item.frozen));
                else
                    batch.Erase(FreezeEntry(item.script));
            }
            for (const JournalRecord::Cost& item : record.cost) {
                if (item.had)
                    batch.Write(CostEntry(item.type, item.height), CostDetails(item.cost));
                else
                    batch.Erase(CostEntry(item.type, item.height));
            }
            for (const JournalRecord::Fee& item : record.fee) {
                if (item.had)
                    batch.Write(FeeEntry(item.height), FeeDetails(item.script));
                else
                    batch.Erase(FeeEntry(item.height));
            }
            if (record.setNumber) {
                if (record.hadNumber)
                    batch.Write(DB_NUMBER_FROZEN, record.number);
                else
                    batch.Erase(DB_NUMBER_FROZEN);
            }
        }
        if (!journal.empty())
            LogPrintf("Governance: the chain state was not written after the last %u change(s) of the governance state; they are undone and come back with their blocks\n", (unsigned int)journal.size());
    }
    EraseJournal(batch);
    return WriteBatch(batch, true);
}

bool CGovernance::GetFreezeEntry(const CScript& script, bool& frozen) const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->freeze.find(script);
        if (it != level->freeze.end()) {
            if (!it->second)
                return false;
            frozen = *it->second;
            return true;
        }
    }
    FreezeDetails details;
    if (!Read(FreezeEntry(script), details))
        return false;
    frozen = details.frozen;
    return true;
}

unsigned int CGovernance::GetNumberFrozen() const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        if (level->numberFrozen)
            return *level->numberFrozen;
    }
    unsigned int number = 0;
    Read(DB_NUMBER_FROZEN, number);
    return number;
}

bool CGovernance::GetCostEntry(int type, int height, CAmount& cost) const
{
    AssertLockHeld(cs);
    const std::pair<int, int> key(type, height);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->cost.find(key);
        if (it != level->cost.end()) {
            if (!it->second)
                return false;
            cost = *it->second;
            return true;
        }
    }
    CostDetails details;
    if (!Read(CostEntry(type, height), details))
        return false;
    cost = details.cost;
    return true;
}

bool CGovernance::GetFeeEntry(int height, CScript& script) const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->fee.find(height);
        if (it != level->fee.end()) {
            if (!it->second)
                return false;
            script = *it->second;
            return true;
        }
    }
    FeeDetails details;
    if (!Read(FeeEntry(height), details))
        return false;
    script = details.script;
    return true;
}

void CGovernance::SetFreezeEntry(CDBBatch& batch, const CScript& script, bool frozen)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().freeze[script] = frozen;
    else
        batch.Write(FreezeEntry(script), FreezeDetails(frozen));
}

void CGovernance::EraseFreezeEntry(CDBBatch& batch, const CScript& script)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().freeze[script] = std::nullopt;
    else
        batch.Erase(FreezeEntry(script));
}

void CGovernance::SetNumberFrozen(CDBBatch& batch, unsigned int number)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().numberFrozen = number;
    else
        batch.Write(DB_NUMBER_FROZEN, number);
}

void CGovernance::SetCostEntry(CDBBatch& batch, int type, int height, std::optional<CAmount> cost)
{
    AssertLockHeld(cs);
    if (!vChanges.empty()) {
        vChanges.back().cost[std::make_pair(type, height)] = cost;
    } else if (cost) {
        batch.Write(CostEntry(type, height), CostDetails(*cost));
    } else {
        batch.Erase(CostEntry(type, height));
    }
}

void CGovernance::SetFeeEntry(CDBBatch& batch, int height, std::optional<CScript> script)
{
    AssertLockHeld(cs);
    if (!vChanges.empty()) {
        vChanges.back().fee[height] = script;
    } else if (script) {
        batch.Write(FeeEntry(height), FeeDetails(*script));
    } else {
        batch.Erase(FeeEntry(height));
    }
}

bool CGovernance::Finish(CDBBatch& batch)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        return true;
    return WriteBatch(batch);
}

std::vector<CScript> GovernanceFreezeScripts(const CTransaction& tx)
{
    std::vector<CScript> scripts;
    for (const CTxOut& out : tx.vout) {
        const CScript& script = out.scriptPubKey;
        if (script.size() >= 6 && script[0] == OP_RETURN && script[2] == GOVERNANCE_MARKER && script[3] == GOVERNANCE_ACTION
            && script[4] == GOVERNANCE_FREEZE && script.size() == 6 + (size_t)script[5]) {
            scripts.emplace_back(script.begin() + 6, script.end());
        }
    }
    return scripts;
}

// ---- Freeze list

unsigned int CGovernance::GetNumberOfFrozenScripts() {
    LOCK(cs);
    return GetNumberFrozen();
}

bool CGovernance::FreezeScript(CScript script) {
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (!frozen) {
            LogPrintf("Governance: Adding script %s back to freeze list\n", HexStr(script));
            SetFreezeEntry(batch, script, true);
            SetNumberFrozen(batch, number + 1);
        } else {
            LogPrintf("Governance: Script %s already frozen\n", HexStr(script));
            SetFreezeEntry(batch, script, true);
        }
    } else {
        LogPrintf("Governance: Freezing previously unknown script %s\n", HexStr(script));
        SetFreezeEntry(batch, script, true);
        SetNumberFrozen(batch, number + 1);
    }

    return Finish(batch);
}

bool CGovernance::UnfreezeScript(CScript script) {
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (frozen) {
            LogPrintf("Governance: Removing script %s from freeze list\n", HexStr(script));
            SetFreezeEntry(batch, script, false);
            SetNumberFrozen(batch, number - 1);
        } else {
            LogPrintf("Governance: Script %s already unfrozen\n", HexStr(script));
            SetFreezeEntry(batch, script, false);
        }
    } else {
        LogPrintf("Governance: Unfreezing previously unknown script %s\n", HexStr(script));
        SetFreezeEntry(batch, script, false);
    }

    return Finish(batch);
}

bool CGovernance::RevertFreezeScript(CScript script) {
    // This is different from unfreezing
    // Reverting immediately removes script from the freeze list,
    // This routine only does so if scrip was only added to the list once
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (frozen) {
            LogPrintf("Governance: Revert adding of script %s to freeze list\n", HexStr(script));
            LogPrintf("Governance: Unfreezing script %s\n", HexStr(script));
            SetNumberFrozen(batch, number - 1);
            SetFreezeEntry(batch, script, false);
        } else {
            LogPrintf("Trying to revert freezing of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Trying to revert freezing of unknown script, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

bool CGovernance::RevertUnfreezeScript(CScript script) {
    // This is different from freezing
    // Reverting immediately adds script to the freeze list,
    // This routine only does so if script was only removed from the list once
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (!frozen) {
            LogPrintf("Governance: Revert disabling of script %s\n", HexStr(script));
            LogPrintf("Governance: Freezing script %s\n", HexStr(script));
            SetNumberFrozen(batch, number + 1);
            SetFreezeEntry(batch, script, true);
        } else {
            LogPrintf("Trying to revert unfreezing of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Governance: Trying to revert unfreezing of unknown script, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

bool CGovernance::ScriptExist(CScript script) {
    LOCK(cs);
    bool frozen;
    return GetFreezeEntry(script, frozen);
}

bool CGovernance::CanSend(CScript script) {
    LOCK(cs);
    bool frozen;
    if (!GetFreezeEntry(script, frozen))
        return true;
    return !frozen;
}

bool CGovernance::DumpFreezeStats(std::vector< std::pair< CScript, bool > > *FreezeVector) {
    LOCK(cs);
    if (IsEmpty())
        LogPrintf("Governance: DB is empty\n");

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FreezeEntry(DUMMY_SCRIPT)); it->Valid(); it->Next()) { // DUMMY_SCRIPT is the lexically first script.
        FreezeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_ADDRESS) {
            FreezeDetails details;
            it->GetValue(details);

            FreezeVector->emplace_back(entry.script, details.frozen);
        } else {
            break; // we are done with the scripts.
        }
    }

    if (!vChanges.empty()) {
        // the open levels: their value for a script in the list (none: the script is taken
        // out of it), new scripts at the end
        std::map<CScript, std::optional<bool>> pending;
        for (const Changes& level : vChanges)
            for (const auto& item : level.freeze)
                pending[item.first] = item.second;
        std::vector<std::pair<CScript, bool>> merged;
        for (const auto& item : *FreezeVector) {
            auto found = pending.find(item.first);
            if (found == pending.end()) {
                merged.push_back(item);
            } else {
                if (found->second)
                    merged.emplace_back(item.first, *found->second);
                pending.erase(found);
            }
        }
        for (const auto& item : pending) {
            if (item.second)
                merged.emplace_back(item.first, *item.second);
        }
        FreezeVector->swap(merged);
    }

    return true;
}

bool CGovernance::GetFrozenScripts(std::vector< CScript > *FreezeVector) {
    std::vector<std::pair<CScript, bool>> all;
    DumpFreezeStats(&all);
    for (const auto& item : all) {
        if (item.second)
            FreezeVector->emplace_back(item.first);
    }
    return true;
}

// ---- Issuing cost

CAmount CGovernance::GetCost(int type) {
    LOCK(cs);
    // every value for this type by height: the database, then the open levels over it
    std::map<int, CAmount> values;

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(CostEntry()); it->Valid(); it->Next()) {
        CostEntry entry;
        if (it->GetKey(entry) && entry.key == DB_COST) {
            if (entry.type == type) {
                CostDetails details = CostDetails();
                it->GetValue(details);
                values[entry.height] = details.cost;
            }
        } else {
            break;
        }
    }
    for (const Changes& level : vChanges) {
        for (const auto& item : level.cost) {
            if (item.first.first != type)
                continue;
            if (item.second)
                values[item.first.second] = *item.second;
            else
                values.erase(item.first.second);
        }
    }

    // the value of the highest height (no value: 0, as before)
    if (values.empty())
        return CostDetails().cost;
    return values.rbegin()->second;
}

static bool CostTypeName(int type, std::string& type_name)
{
    if (type == GOVERNANCE_COST_ROOT) {
        type_name = "root";
    } else if (type == GOVERNANCE_COST_REISSUE) {
        type_name = "reissue";
    } else if (type == GOVERNANCE_COST_UNIQUE) {
        type_name = "unique";
    } else if (type == GOVERNANCE_COST_SUB) {
        type_name = "sub";
    } else if (type == GOVERNANCE_COST_USERNAME) {
        type_name = "username";
    } else {
        return false;
    }
    return true;
}

bool CGovernance::UpdateCost(CAmount cost, int type, int height) {
    std::string type_name;
    if (!CostTypeName(type, type_name)) {
        LogPrintf("Governance: Trying to update issuance cost for unknow type\n");
        return false;
    }

    LOCK(cs);
    CDBBatch batch(*this);
    CAmount existing;
    // (a second command for the same type in the same block does nothing, as before)
    if (!GetCostEntry(type, height, existing)) {
        LogPrintf("Governance: Updating issuance cost for \"%s\" to %s AOK\n", type_name, ValueFromAmountString(cost, 8));
        SetCostEntry(batch, type, height, cost);
    }

    return Finish(batch);
}

bool CGovernance::RevertUpdateCost(int type, int height) {
    std::string type_name;
    CostTypeName(type, type_name);

    LOCK(cs);
    CDBBatch batch(*this);
    CAmount existing;
    if (GetCostEntry(type, height, existing)) {
        LogPrintf("Governance: Revert updating issuance cost for \"%s\" to %s AOK\n", type_name, ValueFromAmountString(existing, 8));
        SetCostEntry(batch, type, height, std::nullopt);
    } else {
        LogPrintf("Governance: Trying to revert unknown issuance cost update, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

// ---- Fee address

CScript CGovernance::GetFeeScript() {
    LOCK(cs);
    std::map<int, CScript> values;

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FeeEntry()); it->Valid(); it->Next()) {
        FeeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_FEE_ADDRESS) {
            FeeDetails details = FeeDetails();
            it->GetValue(details);
            values[entry.height] = details.script;
        } else {
            break;
        }
    }
    for (const Changes& level : vChanges) {
        for (const auto& item : level.fee) {
            if (item.second)
                values[item.first] = *item.second;
            else
                values.erase(item.first);
        }
    }

    if (values.empty())
        return FeeDetails().script;
    return values.rbegin()->second;
}

bool CGovernance::UpdateFeeScript(CScript script, int height) {
    LOCK(cs);
    CDBBatch batch(*this);
    CScript existing;
    if (!GetFeeEntry(height, existing)) {
        LogPrintf("Governance: Updating fee script to %s\n", HexStr(script));
        SetFeeEntry(batch, height, script);
    }

    return Finish(batch);
}

bool CGovernance::RevertUpdateFeeScript(int height) {
    LOCK(cs);
    CDBBatch batch(*this);
    CScript existing;
    if (GetFeeEntry(height, existing)) {
        LogPrintf("Governance: Revert updating fee script to %s\n", HexStr(existing));
        SetFeeEntry(batch, height, std::nullopt);
    } else {
        LogPrintf("Governance: Trying to revert unknown fee script update, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

namespace {
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
} // namespace

uint256 CGovernance::ContentHash()
{
    CHashWriter ss(SER_GETHASH, 0);
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        RawRecord key, value;
        if (!it->GetKey(key) || !it->GetValue(value))
            return uint256();
        // (not the state: the journal that keeps the database in step with the chain state)
        if (!key.bytes.empty() && (key.bytes[0] == DB_CHAINSTATE_PENDING || key.bytes[0] == DB_JOURNAL || key.bytes[0] == DB_JOURNAL_BASE))
            continue;
        ss << key.bytes << value.bytes;
    }
    return ss.GetHash();
}
