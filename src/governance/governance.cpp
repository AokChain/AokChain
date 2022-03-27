// Copyright (c) 2022 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <unordered_map>

#include <governance/governance.h>
#include <utilstrencodings.h>
#include <script/script.h>
#include <chainparams.h>
#include <core_io.h>
#include <amount.h>
#include <base58.h>
#include <chain.h>
#include <util.h>

static const CScript DUMMY_SCRIPT = CScript() << ParseHex("6885777789"); 
static const int DUMMY_TYPE = 0;

static const char DB_NUMBER_FROZEN = 'N';
static const char DB_FEE_ADDRESS = 'f';
static const char DB_ADDRESS = 'a';
static const char DB_COST = 'c';

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

CGovernance::CGovernance(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "governance", nCacheSize, fMemory, fWipe) 
{
}

bool CGovernance::Init(bool fWipe, const CChainParams& chainparams) {
    bool init;

    if (fWipe || Read(DB_GOVERNANCE_INIT, init) == false || init == false) {
        LogPrintf("Governance: Creating new database\n");

        CDBBatch batch(*this);

        batch.Write(DB_NUMBER_FROZEN, 0);

        // Add dummy entries will be first for searching the database
        batch.Write(FreezeEntry(), FreezeDetails());
        batch.Write(CostEntry(), CostDetails());

        // Add initial token issuance cost values
        batch.Write(CostEntry(GOVERNANCE_COST_ROOT, 0), CostDetails(chainparams.RootFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_REISSUE, 0), CostDetails(chainparams.SubFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_UNIQUE, 0), CostDetails(chainparams.UniqueFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_SUB, 0), CostDetails(chainparams.ReissueFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_USERNAME, 0), CostDetails(chainparams.UsernameFeeAmount()));

        // Add initial token fee address from chainparams
        CTxDestination destination = DecodeDestination(Params().TokenFeeAddress());
        CScript feeScript = GetScriptForDestination(destination);
        batch.Write(FeeEntry(), FeeDetails(feeScript));

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

bool CGovernance::CanSend(CScript script) {
    FreezeEntry entry(script);
    FreezeDetails details = FreezeDetails();

    if (!Exists(entry)) {
        return true;
    }

    Read(entry, details);
    return !details.frozen;
}

bool CGovernance::DumpFreezeStats(std::vector< std::pair< CScript, bool > > *FreezeVector) {
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

CAmount CGovernance::GetCost(int type) {
    CostDetails details = CostDetails();
    int height = -1;

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(CostEntry()); it->Valid(); it->Next()) {
        CostEntry entry;
        if (it->GetKey(entry) && entry.key == DB_COST) {
            if (entry.type == type && entry.height > height) {
                height = entry.height;
                it->GetValue(details);
            }
        } else {
            break;
        }
    }

    return details.cost;
}

bool CGovernance::UpdateCost(CAmount cost, int type, int height) {
    CostEntry entry(type, height);
    CostDetails details = CostDetails();
    CDBBatch batch(*this);
    std::string type_name;

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
        LogPrintf("Governance: Trying to update issuance cost for unknow type\n");
        return false;
    }

    if (!Read(entry, details)) {
        LogPrintf("Governance: Updating issuance cost for \"%s\" to %s AOK\n", type_name, ValueFromAmountString(cost, 8));
        batch.Write(entry, CostDetails(cost));
    }

    return WriteBatch(batch);
}

bool CGovernance::RevertUpdateCost(int type, int height) {
    CostEntry entry(type, height);
    CostDetails details = CostDetails();
    CDBBatch batch(*this);

    std::string type_name;

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
    }

    if (Read(entry, details)) {
        LogPrintf("Governance: Revert updating issuance cost for \"%s\" to %s AOK\n", type_name, ValueFromAmountString(details.cost, 8));
        batch.Erase(entry);
    } else {
        LogPrintf("Governance: Trying to revert unknown issuance cost update, database is corrupted\n");
        return false;
    }

    return WriteBatch(batch);
}

CScript CGovernance::GetFeeScript() {
    FeeDetails details = FeeDetails();
    int height = -1;

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FeeEntry()); it->Valid(); it->Next()) {
        FeeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_FEE_ADDRESS) {
            if (entry.height > height) {
                height = entry.height;
                it->GetValue(details);
            }
        } else {
            break;
        }
    }

    return details.script;
}

bool CGovernance::UpdateFeeScript(CScript script, int height) {
    FeeEntry entry(height);
    FeeDetails details = FeeDetails();
    CDBBatch batch(*this);

    if (!Read(entry, details)) {
        LogPrintf("Governance: Updating fee script to %s\n", HexStr(script).substr(0, 10));
        batch.Write(entry, FeeDetails(script));
    }

    return WriteBatch(batch);
}

bool CGovernance::RevertUpdateFeeScript(int height) {
    FeeEntry entry(height);
    FeeDetails details = FeeDetails();
    CDBBatch batch(*this);

    if (Read(entry, details)) {
        LogPrintf("Governance: Revert updating fee script to %s\n", HexStr(details.script).substr(0, 10));
        batch.Erase(entry);
    } else {
        LogPrintf("Governance: Trying to revert unknown fee script update, database is corrupted\n");
        return false;
    }

    return WriteBatch(batch);
}
