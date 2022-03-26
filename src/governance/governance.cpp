// Copyright (c) 2022 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <unordered_map>

#include <utilstrencodings.h>
#include <governance/governance.h>
#include <script/script.h>
#include <base58.h>
#include <chain.h>
#include <util.h>

static const CScript DUMMY_SCRIPT = CScript() << ParseHex("6885777789"); 

static const char DB_NUMBER_FROZEN = 'N';
static const char DB_ADDRESS = 'a';

static const char DB_GOVERNANCE_INIT  = 'G';

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
}

CGovernance::CGovernance(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "governance", nCacheSize, fMemory, fWipe) 
{
}

bool CGovernance::Init(bool fWipe){
    bool init;

    if (fWipe || Read(DB_GOVERNANCE_INIT, init) == false || init == false) {
        LogPrintf("Governance: Creating new database\n");

        CDBBatch batch(*this);

        batch.Write(DB_NUMBER_FROZEN, 0);

        // Add dummy script which will be first entry for searching the database
        batch.Write(FreezeEntry(DUMMY_SCRIPT), FreezeDetails()); 

        batch.Write(DB_GOVERNANCE_INIT, true);
        WriteBatch(batch);
    }

    return true;
}

unsigned int CGovernance::GetNumberOfFrozenScripts() {
    unsigned int number;

    Read(DB_NUMBER_FROZEN, number);

    return number;
}

bool CGovernance::FreezeScript(CScript script) {
    FreezeEntry entry(script);
    FreezeDetails details = FreezeDetails();
    CDBBatch batch(*this);

    unsigned int number;
    Read(DB_NUMBER_FROZEN, number);

    if (Read(entry, details)) {
        if (!details.frozen) {
            LogPrintf("Governance: Adding script %s back to freeze list\n", HexStr(script).substr(0, 10));

            details.frozen = true;
            batch.Write(entry, details);
            batch.Write(DB_NUMBER_FROZEN, number + 1);
        } else {
            LogPrintf("Governance: Script %s already frozen\n", HexStr(script).substr(0, 10));
            batch.Write(entry, details);
        }
    } else {
        LogPrintf("Governance: Freezing previously unknown script %s\n", HexStr(script).substr(0, 10));

        batch.Write(entry, FreezeDetails());
        batch.Write(DB_NUMBER_FROZEN, number + 1);
    }

    return WriteBatch(batch);
}

bool CGovernance::UnfreezeScript(CScript script) {
    FreezeEntry entry(script);
    FreezeDetails details = FreezeDetails();
    CDBBatch batch(*this);

    unsigned int number;
    Read(DB_NUMBER_FROZEN, number);

    if (Read(entry, details)) {
        if (details.frozen) {
            LogPrintf("Governance: Removing script %s from freeze list\n", HexStr(script).substr(0, 10));

            details.frozen = false;
            batch.Write(entry, details);
            batch.Write(DB_NUMBER_FROZEN, number - 1);
        } else {
            LogPrintf("Governance: Script %s already unfrozen\n", HexStr(script).substr(0, 10));
            batch.Write(entry, details);
        }
    } else {
        LogPrintf("Governance: Unfreezing previously unknown script %s\n", HexStr(script).substr(0, 10));
        batch.Write(entry, FreezeDetails(false));
    }

    return WriteBatch(batch);
}

bool CGovernance::RevertFreezeScript(CScript script) { 
    // This is different from unfreezing
    // Reverting immediately removes script from the freeze list,
    // This routine only does so if scrip was only added to the list once

    FreezeEntry entry(script);
    FreezeDetails details = FreezeDetails();
    CDBBatch batch(*this);

    unsigned int number;
    Read(DB_NUMBER_FROZEN, number);

    if (Read(entry, details)) {
        if (details.frozen) {
            LogPrintf("Governance: Revert adding of script %s to freeze list\n", HexStr(script).substr(0, 10));

            LogPrintf("Governance: Unfreezing script %s\n", HexStr(script).substr(0, 10));
            details.frozen = false;
            batch.Write(DB_NUMBER_FROZEN, number - 1);

            batch.Write(entry, details);
        } else {
            LogPrintf("Trying to revert freezing of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Trying to revert freezing of unknown script, database is corrupted\n");
        return false;
    }

    return WriteBatch(batch);
}

bool CGovernance::RevertUnfreezeScript(CScript script) { 
    // This is different from freezing
    // Reverting immediately adds script to the freeze list,
    // This routine only does so if script was only removed from the list once

    FreezeEntry entry(script);
    FreezeDetails details = FreezeDetails();
    CDBBatch batch(*this);

    unsigned int number;
    Read(DB_NUMBER_FROZEN, number);

    if (Read(entry, details)) {
        if (!details.frozen) {
            LogPrintf("Governance: Revert disabling of script %s\n", HexStr(script).substr(0, 10));

            LogPrintf("Governance: Freezing script %s\n", HexStr(script).substr(0, 10));
            details.frozen = true;
            batch.Write(DB_NUMBER_FROZEN, number + 1);

            batch.Write(entry, details);
        } else {
            LogPrintf("Trying to revert unfreezing of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Governance: Trying to revert unfreezing of unknown script, database is corrupted\n");
        return false;
    }

    return WriteBatch(batch);
}

bool CGovernance::ScriptExist(CScript script) {
    return Exists(FreezeEntry(script));
}

bool CGovernance::canSend(CScript script) {
    FreezeEntry entry(script);
    FreezeDetails details = FreezeDetails();

    if (!Exists(entry)) {
        return true;
    }

    Read(entry, details);
    return !details.frozen;
}

bool CGovernance::DumpStats(std::vector< std::pair< CScript, bool > > *FreezeVector) {
    if (IsEmpty())
        LogPrintf("Governance: DB is empty\n");

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FreezeEntry(DUMMY_SCRIPT)); it->Valid(); it->Next()) { // DUMMY_SCRIPT is the lexically first script.
        FreezeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_ADDRESS) { // Does this work? Should give false if Key is of other type than what we expect?!
            FreezeDetails details;
            it->GetValue(details);

            FreezeVector->emplace_back(entry.script, details.frozen);
        } else { 
            break; // we are done with the scripts.
        }
    } 

    return true;
}

bool CGovernance::GetFrozenScripts(std::vector< CScript > *FreezeVector) {
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FreezeEntry(DUMMY_SCRIPT)); it->Valid(); it->Next()) {
        FreezeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_ADDRESS) {
            FreezeDetails details;
            it->GetValue(details);

            if (details.frozen)
                FreezeVector->emplace_back(entry.script);
        } else { 
            break;
        }
    } 

    return true;
}
