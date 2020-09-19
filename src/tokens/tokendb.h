// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_TOKENDB_H
#define AOKCHAIN_TOKENDB_H

#include "fs.h"
#include "serialize.h"

#include <string>
#include <map>
#include <dbwrapper.h>

class CNewToken;
class uint256;
class COutPoint;
class CDatabasedTokenData;

struct CBlockTokenUndo
{
    bool fChangedIPFS;
    bool fChangedUnits;
    std::string strIPFS;
    int nUnits;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(fChangedUnits);
        READWRITE(fChangedIPFS);
        READWRITE(strIPFS);
        READWRITE(nUnits);
    }
};

/** Access to the block database (blocks/index/) */
class CTokensDB : public CDBWrapper
{
public:
    explicit CTokensDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CTokensDB(const CTokensDB&) = delete;
    CTokensDB& operator=(const CTokensDB&) = delete;

    // Write to database functions
    bool WriteTokenData(const CNewToken& token, const int nHeight, const uint256& blockHash);
    bool WriteTokenAddressQuantity(const std::string& tokenName, const std::string& address, const CAmount& quantity);
    bool WriteAddressTokenQuantity( const std::string& address, const std::string& tokenName, const CAmount& quantity);
    bool WriteBlockUndoTokenData(const uint256& blockhash, const std::vector<std::pair<std::string, CBlockTokenUndo> >& tokenUndoData);
    bool WriteReissuedMempoolState();

    // Read from database functions
    bool ReadTokenData(const std::string& strName, CNewToken& token, int& nHeight, uint256& blockHash);
    bool ReadTokenAddressQuantity(const std::string& tokenName, const std::string& address, CAmount& quantity);
    bool ReadAddressTokenQuantity(const std::string& address, const std::string& tokenName, CAmount& quantity);
    bool ReadBlockUndoTokenData(const uint256& blockhash, std::vector<std::pair<std::string, CBlockTokenUndo> >& tokenUndoData);
    bool ReadReissuedMempoolState();

    // Erase from database functions
    bool EraseTokenData(const std::string& tokenName);
    bool EraseMyTokenData(const std::string& tokenName);
    bool EraseTokenAddressQuantity(const std::string &tokenName, const std::string &address);
    bool EraseAddressTokenQuantity(const std::string &address, const std::string &tokenName);

    // Helper functions
    bool LoadTokens();
    bool TokenDir(std::vector<CDatabasedTokenData>& tokens, const std::string filter, const size_t count, const long start);
    bool TokenDir(std::vector<CDatabasedTokenData>& tokens);

    bool AddressDir(std::vector<std::pair<std::string, CAmount> >& vecTokenAmount, int& totalEntries, const bool& fGetTotal, const std::string& address, const size_t count, const long start);
    bool TokenAddressDir(std::vector<std::pair<std::string, CAmount> >& vecAddressAmount, int& totalEntries, const bool& fGetTotal, const std::string& tokenName, const size_t count, const long start);
};


#endif //AOKCHAIN_TOKENDB_H
