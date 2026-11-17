// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_TOKENDB_H
#define AOKCHAIN_TOKENDB_H

#include <fs.h>
#include <serialize.h>

#include <string>
#include <map>
#include <vector>
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

    bool TokenAddressDirUnique(std::string& holderAddress, const std::string& tokenName);

    std::string UsernameAddress(const std::string& tokenName);

    /**
     * The state of the token database (tokens and balances) is written in dumps. All records
     * of one dump and the block that the database then stands for go out as one batch, written
     * and synced together: a stop inside a dump leaves the database at the block of the dump
     * before, never in between, and RecoverTokenState() brings it forward at the next start.
     *
     * Between BeginDump() and CommitDump() the calls above that write or erase tokens and
     * balances are collected instead of written; the last call for a key counts. Reads do not
     * see them: a dump writes from the token cache and reads nothing back.
     *
     * The block is kept under a key that begins like the token undo records. No version counts
     * those into the hash of the token state, and a version that does not know the record
     * reads past it.
     */
    void BeginDump();
    bool CommitDump(const uint256& hashBlock);
    void AbortDump();
    //! The block that the database stands for (false: it does not say).
    bool ReadStateBlock(uint256& hashBlock);
    //! Notes that block alone: the database is taken to stand for it as it is.
    bool WriteStateBlock(const uint256& hashBlock);
    //! Whether these are the bytes of the key of that record (for code that walks the undo records).
    static bool IsStateBlockKey(const std::vector<unsigned char>& key);
    //! A hash of the tokens and balances: of every record but the undo data, the block above and
    //! the reissues that wait in the memory pool. Null when a record cannot be read.
    uint256 StateHash();

private:
    template <typename K, typename V> bool PutRecord(const K& key, const V& value);
    template <typename K> bool EraseRecord(const K& key);

    //! Between BeginDump() and CommitDump(): key -> (erase, value), as they go to the database.
    bool m_dumping = false;
    std::map<std::vector<unsigned char>, std::pair<bool, std::vector<unsigned char> > > m_dump;
};


#endif //AOKCHAIN_TOKENDB_H
