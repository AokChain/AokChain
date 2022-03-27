// Copyright (c) 2022 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ROZA_VALIDATORS_H
#define ROZA_VALIDATORS_H

#include <script/script.h>
#include <chainparams.h>
#include <dbwrapper.h>
#include <chain.h>

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

    using CDBWrapper::Sync;
  
};

#endif /* AOKCHAIN_GOVERNANCE_H */
