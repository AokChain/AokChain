// Copyright (c) 2022 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ROZA_VALIDATORS_H
#define ROZA_VALIDATORS_H

#include <script/script.h>
#include <dbwrapper.h>
#include <chain.h>

#define GOVERNANCE_MARKER 71
#define GOVERNANCE_ACTION 65
#define GOVERNANCE_FREEZE 70
#define GOVERNANCE_UNFREEZE 85

class CGovernance : CDBWrapper 
{
public:
    CGovernance(size_t nCacheSize, bool fMemory, bool fWipe);
    bool Init(bool fWipe);

    // Statistics
    unsigned int GetNumberOfFrozenScripts();
    
    // Managing freeze list
    bool FreezeScript(CScript script);
    bool UnfreezeScript(CScript script);
    bool RevertFreezeScript(CScript script);
    bool RevertUnfreezeScript(CScript script);
    bool ScriptExist(CScript script);
    bool canSend(CScript script);

    bool DumpStats(std::vector< std::pair< CScript, bool > > *FreezeVector);
    bool GetFrozenScripts(std::vector< CScript > *FreezeVector);

    using CDBWrapper::Sync;
  
};

#endif /* AOKCHAIN_GOVERNANCE_H */
