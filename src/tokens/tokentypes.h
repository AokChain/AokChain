// Copyright (c) 2018 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// Created by Jeremy Anderson on 5/15/18.

#ifndef AOKCHAINCOIN_NEWTOKEN_H
#define AOKCHAINCOIN_NEWTOKEN_H

#include <string>
#include <sstream>
#include <list>
#include <unordered_map>
#include "amount.h"
#include "script/standard.h"
#include "primitives/transaction.h"

#define MAX_UNIT 8
#define MIN_UNIT 0

class CTokensCache;

enum class KnownTokenType
{
    ROOT = 0,
    SUB = 1,
    UNIQUE = 2,
    OWNER = 3,
    MSGCHANNEL = 4,
    VOTE = 5,
    REISSUE = 6,
    INVALID = 7
};

int IntFromTokenType(KnownTokenType type);
KnownTokenType TokenTypeFromInt(int nType);

class CNewToken
{
public:
    std::string strName; // MAX 31 Bytes
    CAmount nAmount;     // 8 Bytes
    int8_t units;        // 1 Byte
    int8_t nReissuable;  // 1 Byte

    CNewToken()
    {
        SetNull();
    }

    CNewToken(const std::string& strName, const CAmount& nAmount, const int& units, const int& nReissuable);
    CNewToken(const std::string& strName, const CAmount& nAmount);

    CNewToken(const CNewToken& token);
    CNewToken& operator=(const CNewToken& token);

    void SetNull()
    {
        strName= "";
        nAmount = 0;
        units = int8_t(MAX_UNIT);
        nReissuable = int8_t(0);
    }

    bool IsNull() const;

    bool IsValid(std::string& strError, CTokensCache& tokenCache, bool fCheckMempool = false, bool fCheckDuplicateInputs = true, bool fForceDuplicateCheck = true) const;

    std::string ToString();

    void ConstructTransaction(CScript& script) const;
    void ConstructOwnerTransaction(CScript& script) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        int8_t empty = 0;

        READWRITE(strName);
        READWRITE(nAmount);
        READWRITE(units);
        READWRITE(nReissuable);
        READWRITE(empty);
    }
};

class TokenComparator
{
public:
    bool operator()(const CNewToken& s1, const CNewToken& s2) const
    {
        return s1.strName < s2.strName;
    }
};

class CDatabasedTokenData
{
public:
    CNewToken token;
    int nHeight;
    uint256 blockHash;

    CDatabasedTokenData(const CNewToken& token, const int& nHeight, const uint256& blockHash);
    CDatabasedTokenData();

    void SetNull()
    {
        token.SetNull();
        nHeight = -1;
        blockHash = uint256();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(token);
        READWRITE(nHeight);
        READWRITE(blockHash);
    }
};

class CTokenTransfer
{
public:
    std::string strName;
    CAmount nAmount;
    uint32_t nTokenLockTime;


    CTokenTransfer()
    {
        SetNull();
    }

    void SetNull()
    {
        nAmount = 0;
        strName = "";
        nTokenLockTime = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strName);
        READWRITE(nAmount);
        READWRITE(nTokenLockTime);
    }

    CTokenTransfer(const std::string& strTokenName, const CAmount& nAmount, const uint32_t& nTokenLockTime);
    bool IsValid(std::string& strError) const;
    void ConstructTransaction(CScript& script) const;
};

class CReissueToken
{
public:
    std::string strName;
    CAmount nAmount;
    int8_t nUnits;
    int8_t nReissuable;

    CReissueToken()
    {
        SetNull();
    }

    void SetNull()
    {
        nAmount = 0;
        strName = "";
        nUnits = 0;
        nReissuable = 1;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(strName);
        READWRITE(nAmount);
        READWRITE(nUnits);
        READWRITE(nReissuable);
    }

    CReissueToken(const std::string& strTokenName, const CAmount& nAmount, const int& nUnits, const int& nReissuable);
    bool IsValid(std::string& strError, CTokensCache& tokenCache, bool fForceCheckPrimaryTokenExists = true) const;
    void ConstructTransaction(CScript& script) const;
    bool IsNull() const;
};


/** THESE ARE ONLY TO BE USED WHEN ADDING THINGS TO THE CACHE DURING CONNECT AND DISCONNECT BLOCK */
struct CTokenCacheNewToken
{
    CNewToken token;
    std::string address;
    uint256 blockHash;
    int blockHeight;

    CTokenCacheNewToken(const CNewToken& token, const std::string& address, const int& blockHeight, const uint256& blockHash)
    {
        this->token = token;
        this->address = address;
        this->blockHash = blockHash;
        this->blockHeight = blockHeight;
    }

    bool operator<(const CTokenCacheNewToken& rhs) const
    {
        return token.strName < rhs.token.strName;
    }
};

struct CTokenCacheReissueToken
{
    CReissueToken reissue;
    std::string address;
    COutPoint out;
    uint256 blockHash;
    int blockHeight;


    CTokenCacheReissueToken(const CReissueToken& reissue, const std::string& address, const COutPoint& out, const int& blockHeight, const uint256& blockHash)
    {
        this->reissue = reissue;
        this->address = address;
        this->out = out;
        this->blockHash = blockHash;
        this->blockHeight = blockHeight;
    }

    bool operator<(const CTokenCacheReissueToken& rhs) const
    {
        return out < rhs.out;
    }

};

struct CTokenCacheNewTransfer
{
    CTokenTransfer transfer;
    std::string address;
    COutPoint out;

    CTokenCacheNewTransfer(const CTokenTransfer& transfer, const std::string& address, const COutPoint& out)
    {
        this->transfer = transfer;
        this->address = address;
        this->out = out;
    }

    bool operator<(const CTokenCacheNewTransfer& rhs ) const
    {
        return out < rhs.out;
    }
};

struct CTokenCacheNewOwner
{
    std::string tokenName;
    std::string address;

    CTokenCacheNewOwner(const std::string& tokenName, const std::string& address)
    {
        this->tokenName = tokenName;
        this->address = address;
    }

    bool operator<(const CTokenCacheNewOwner& rhs) const
    {

        return tokenName < rhs.tokenName;
    }
};

struct CTokenCacheUndoTokenAmount
{
    std::string tokenName;
    std::string address;
    CAmount nAmount;

    CTokenCacheUndoTokenAmount(const std::string& tokenName, const std::string& address, const CAmount& nAmount)
    {
        this->tokenName = tokenName;
        this->address = address;
        this->nAmount = nAmount;
    }
};

struct CTokenCacheSpendToken
{
    std::string tokenName;
    std::string address;
    CAmount nAmount;

    CTokenCacheSpendToken(const std::string& tokenName, const std::string& address, const CAmount& nAmount)
    {
        this->tokenName = tokenName;
        this->address = address;
        this->nAmount = nAmount;
    }
};

// Least Recently Used Cache
template<typename cache_key_t, typename cache_value_t>
class CLRUCache
{
public:
    typedef typename std::pair<cache_key_t, cache_value_t> key_value_pair_t;
    typedef typename std::list<key_value_pair_t>::iterator list_iterator_t;

    CLRUCache(size_t max_size) : maxSize(max_size)
    {
    }
    CLRUCache()
    {
        SetNull();
    }

    void Put(const cache_key_t& key, const cache_value_t& value)
    {
        auto it = cacheItemsMap.find(key);
        cacheItemsList.push_front(key_value_pair_t(key, value));
        if (it != cacheItemsMap.end())
        {
            cacheItemsList.erase(it->second);
            cacheItemsMap.erase(it);
        }
        cacheItemsMap[key] = cacheItemsList.begin();

        if (cacheItemsMap.size() > maxSize)
        {
            auto last = cacheItemsList.end();
            last--;
            cacheItemsMap.erase(last->first);
            cacheItemsList.pop_back();
        }
    }

    void Erase(const cache_key_t& key)
    {
        auto it = cacheItemsMap.find(key);
        if (it != cacheItemsMap.end())
        {
            cacheItemsList.erase(it->second);
            cacheItemsMap.erase(it);
        }
    }

    const cache_value_t& Get(const cache_key_t& key)
    {
        auto it = cacheItemsMap.find(key);
        if (it == cacheItemsMap.end())
        {
            throw std::range_error("There is no such key in cache");
        }
        else
        {
            cacheItemsList.splice(cacheItemsList.begin(), cacheItemsList, it->second);
            return it->second->second;
        }
    }

    bool Exists(const cache_key_t& key) const
    {
        return cacheItemsMap.find(key) != cacheItemsMap.end();
    }

    size_t Size() const
    {
        return cacheItemsMap.size();
    }


    void Clear()
    {
        cacheItemsMap.clear();
        cacheItemsList.clear();
    }

    void SetNull()
    {
        maxSize = 0;
        Clear();
    }

    size_t MaxSize() const
    {
        return maxSize;
    }


    void SetSize(const size_t size)
    {
        maxSize = size;
    }

   const std::unordered_map<cache_key_t, list_iterator_t>& GetItemsMap()
    {
        return cacheItemsMap;
    }

    const std::list<key_value_pair_t>& GetItemsList()
    {
        return cacheItemsList;
    }


    CLRUCache(const CLRUCache& cache)
    {
        this->cacheItemsList = cache.cacheItemsList;
        this->cacheItemsMap = cache.cacheItemsMap;
        this->maxSize = cache.maxSize;
    }

private:
    std::list<key_value_pair_t> cacheItemsList;
    std::unordered_map<cache_key_t, list_iterator_t> cacheItemsMap;
    size_t maxSize;
};

#endif //AOKCHAINCOIN_NEWTOKEN_H
