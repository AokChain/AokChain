// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util.h>
#include <consensus/params.h>
#include <script/ismine.h>
#include <tinyformat.h>
#include <tokens/tokendb.h>
#include <tokens/tokens.h>
#include <validation.h>

#include <boost/thread.hpp>

static const char TOKEN_FLAG = 'T';
static const char TOKEN_ADDRESS_QUANTITY_FLAG = 'B';
static const char ADDRESS_TOKEN_QUANTITY_FLAG = 'C';
static const char MY_TOKEN_FLAG = 'M';
static const char BLOCK_TOKEN_UNDO_DATA = 'U';
static const char MEMPOOL_REISSUED_TX = 'Z';

// The block that the database stands for (see BeginDump in tokendb.h): under the letter of
// the undo records, with a key that no block hash makes.
static const std::pair<char, std::string> STATE_BLOCK_KEY = std::make_pair(BLOCK_TOKEN_UNDO_DATA, std::string("state"));

namespace {
//! Bytes that are written as they are.
struct RawBytes {
    const std::vector<unsigned char>& bytes;
    explicit RawBytes(const std::vector<unsigned char>& bytesIn) : bytes(bytesIn) {}
    template <typename Stream> void Serialize(Stream& s) const
    {
        if (!bytes.empty())
            s.write((const char*)bytes.data(), bytes.size());
    }
};

template <typename T> std::vector<unsigned char> ToBytes(const T& obj)
{
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << obj;
    return std::vector<unsigned char>(ss.begin(), ss.end());
}
} // namespace

static size_t MAX_DATABASE_RESULTS = 50000;

CTokensDB::CTokensDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "tokens", nCacheSize, fMemory, fWipe) {
}

template <typename K, typename V>
bool CTokensDB::PutRecord(const K& key, const V& value)
{
    if (!m_dumping)
        return Write(key, value);
    m_dump[ToBytes(key)] = std::make_pair(false, ToBytes(value));
    return true;
}

template <typename K>
bool CTokensDB::EraseRecord(const K& key)
{
    if (!m_dumping)
        return Erase(key);
    m_dump[ToBytes(key)] = std::make_pair(true, std::vector<unsigned char>());
    return true;
}

void CTokensDB::BeginDump()
{
    AssertLockHeld(cs_main);
    m_dump.clear();
    m_dumping = true;
}

void CTokensDB::AbortDump()
{
    m_dump.clear();
    m_dumping = false;
}

bool CTokensDB::CommitDump(const uint256& hashBlock)
{
    AssertLockHeld(cs_main);
    CDBBatch batch(*this);
    const size_t nRecords = m_dump.size();
    for (auto it = m_dump.begin(); it != m_dump.end(); it = m_dump.erase(it)) {
        if (it->second.first)
            batch.Erase(RawBytes(it->first));
        else
            batch.Write(RawBytes(it->first), RawBytes(it->second.second));
    }
    batch.Write(STATE_BLOCK_KEY, hashBlock);
    m_dumping = false;
    LogPrint(BCLog::COINDB, "Token database: %u record(s), %.2f MiB, for block %s\n", (unsigned int)nRecords,
             batch.SizeEstimate() * (1.0 / 1048576.0), hashBlock.ToString());
    // For tests (-regtest only, -dbcrashtokenwrite=<n>): stop where the n-th write of token
    // records of this run would begin.
    if (nRecords > 0 && Params().MineBlocksOnDemand()) {
        static int nWrites = 0;
        if (++nWrites == gArgs.GetArg("-dbcrashtokenwrite", 0)) {
            LogPrintf("Simulating a crash in the write of the token database. Goodbye.\n");
            _Exit(0);
        }
    }
    return WriteBatch(batch, true);
}

bool CTokensDB::ReadStateBlock(uint256& hashBlock)
{
    return Read(STATE_BLOCK_KEY, hashBlock);
}

bool CTokensDB::WriteStateBlock(const uint256& hashBlock)
{
    return Write(STATE_BLOCK_KEY, hashBlock, true);
}

bool CTokensDB::IsStateBlockKey(const std::vector<unsigned char>& key)
{
    return key == ToBytes(STATE_BLOCK_KEY);
}

bool CTokensDB::WriteTokenData(const CNewToken &token, const int nHeight, const uint256& blockHash)
{
    CDatabasedTokenData data(token, nHeight, blockHash);
    return PutRecord(std::make_pair(TOKEN_FLAG, token.strName), data);
}

bool CTokensDB::WriteTokenAddressQuantity(const std::string &tokenName, const std::string &address, const CAmount &quantity)
{
    return PutRecord(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, address)), quantity);
}

bool CTokensDB::WriteAddressTokenQuantity(const std::string &address, const std::string &tokenName, const CAmount& quantity) {
    return PutRecord(std::make_pair(ADDRESS_TOKEN_QUANTITY_FLAG, std::make_pair(address, tokenName)), quantity);
}

bool CTokensDB::ReadTokenData(const std::string& strName, CNewToken& token, int& nHeight, uint256& blockHash)
{

    CDatabasedTokenData data;
    bool ret =  Read(std::make_pair(TOKEN_FLAG, strName), data);

    if (ret) {
        token = data.token;
        nHeight = data.nHeight;
        blockHash = data.blockHash;
    }

    return ret;
}

bool CTokensDB::ReadTokenAddressQuantity(const std::string& tokenName, const std::string& address, CAmount& quantity)
{
    return Read(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, address)), quantity);
}

bool CTokensDB::ReadAddressTokenQuantity(const std::string &address, const std::string &tokenName, CAmount& quantity) {
    return Read(std::make_pair(ADDRESS_TOKEN_QUANTITY_FLAG, std::make_pair(address, tokenName)), quantity);
}

bool CTokensDB::EraseTokenData(const std::string& tokenName)
{
    return EraseRecord(std::make_pair(TOKEN_FLAG, tokenName));
}

bool CTokensDB::EraseMyTokenData(const std::string& tokenName)
{
    return Erase(std::make_pair(MY_TOKEN_FLAG, tokenName));
}

bool CTokensDB::EraseTokenAddressQuantity(const std::string &tokenName, const std::string &address) {
    return EraseRecord(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, address)));
}

bool CTokensDB::EraseAddressTokenQuantity(const std::string &address, const std::string &tokenName) {
    return EraseRecord(std::make_pair(ADDRESS_TOKEN_QUANTITY_FLAG, std::make_pair(address, tokenName)));
}

bool EraseAddressTokenQuantity(const std::string &address, const std::string &tokenName);

bool CTokensDB::WriteBlockUndoTokenData(const uint256& blockhash, const std::vector<std::pair<std::string, CBlockTokenUndo> >& tokenUndoData)
{
    return Write(std::make_pair(BLOCK_TOKEN_UNDO_DATA, blockhash), tokenUndoData);
}

bool CTokensDB::ReadBlockUndoTokenData(const uint256 &blockhash, std::vector<std::pair<std::string, CBlockTokenUndo> > &tokenUndoData)
{
    // If it exists, return the read value.
    if (Exists(std::make_pair(BLOCK_TOKEN_UNDO_DATA, blockhash)))
           return Read(std::make_pair(BLOCK_TOKEN_UNDO_DATA, blockhash), tokenUndoData);

    // If it doesn't exist, we just return true because we don't want to fail just because it didn't exist in the db
    return true;
}

bool CTokensDB::WriteReissuedMempoolState()
{
    return Write(MEMPOOL_REISSUED_TX, mapReissuedTokens);
}

bool CTokensDB::ReadReissuedMempoolState()
{
    mapReissuedTokens.clear();
    mapReissuedTx.clear();
    // If it exists, return the read value.
    bool rv = Read(MEMPOOL_REISSUED_TX, mapReissuedTokens);
    if (rv) {
        for (auto pair : mapReissuedTokens)
            mapReissuedTx.insert(std::make_pair(pair.second, pair.first));
    }
    return rv;
}

bool CTokensDB::LoadTokens()
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(TOKEN_FLAG, std::string()));

    // Load tokens
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::string> key;
        if (pcursor->GetKey(key) && key.first == TOKEN_FLAG) {
            CDatabasedTokenData data;
            if (pcursor->GetValue(data)) {
                ptokensCache->Put(data.token.strName, data);
                pcursor->Next();

                // Loaded enough from database to have in memory.
                // No need to load everything if it is just going to be removed from the cache
                if (ptokensCache->Size() == (ptokensCache->MaxSize() / 2))
                    break;
            } else {
                return error("%s: failed to read token", __func__);
            }
        } else {
            break;
        }
    }


    if (fTokenIndex) {
        std::unique_ptr<CDBIterator> pcursor3(NewIterator());
        pcursor3->Seek(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(std::string(), std::string())));

        // Load mapTokenAddressAmount
        while (pcursor3->Valid()) {
            boost::this_thread::interruption_point();
            std::pair<char, std::pair<std::string, std::string> > key; // <Token Name, Address> -> Quantity
            if (pcursor3->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG) {
                CAmount value;
                if (pcursor3->GetValue(value)) {
                    ptokens->mapTokensAddressAmount.insert(
                            std::make_pair(std::make_pair(key.second.first, key.second.second), value));
                    if (ptokens->mapTokensAddressAmount.size() > MAX_CACHE_TOKENS_SIZE)
                        break;
                    pcursor3->Next();
                } else {
                    return error("%s: failed to read my address quantity from database", __func__);
                }
            } else {
                break;
            }
        }
    }

    return true;
}

std::string CTokensDB::UsernameAddress(const std::string& tokenName)
{
    FlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, std::string())));

    std::string address = "";
    size_t loaded = 0;

    // Load tokens
    while (pcursor->Valid() && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG && key.second.first == tokenName) {
            address = key.second.second;
            loaded += 1;

            break;
        } else {
            break;
        }
    }

    return address;
}

bool CTokensDB::TokenDir(std::vector<CDatabasedTokenData>& tokens, const std::string filter, const size_t count, const long start)
{
    FlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(TOKEN_FLAG, std::string()));

    auto prefix = filter;
    bool wildcard = prefix.back() == '*';
    if (wildcard)
        prefix.pop_back();

    size_t skip = 0;
    if (start >= 0) {
        skip = start;
    }
    else {
        // compute table size for backwards offset
        long table_size = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::string> key;
            if (pcursor->GetKey(key) && key.first == TOKEN_FLAG) {
                if (prefix == "" ||
                    (wildcard && key.second.find(prefix) == 0) ||
                    (!wildcard && key.second == prefix)) {
                    table_size += 1;
                }
            }
            pcursor->Next();
        }
        skip = table_size + start;
        pcursor->SeekToFirst();
    }


    size_t loaded = 0;
    size_t offset = 0;

    // Load tokens
    while (pcursor->Valid() && loaded < count) {
        boost::this_thread::interruption_point();

        std::pair<char, std::string> key;
        if (pcursor->GetKey(key) && key.first == TOKEN_FLAG) {
            if (prefix == "" ||
                    (wildcard && key.second.find(prefix) == 0) ||
                    (!wildcard && key.second == prefix)) {
                if (offset < skip) {
                    offset += 1;
                }
                else {
                    CDatabasedTokenData data;
                    if (pcursor->GetValue(data)) {
                        tokens.push_back(data);
                        loaded += 1;
                    } else {
                        return error("%s: failed to read token", __func__);
                    }
                }
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CTokensDB::AddressDir(std::vector<std::pair<std::string, CAmount> >& vecTokenAmount, int& totalEntries, const bool& fGetTotal, const std::string& address, const size_t count, const long start)
{
    FlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(ADDRESS_TOKEN_QUANTITY_FLAG, std::make_pair(address, std::string())));

    if (fGetTotal) {
        totalEntries = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == ADDRESS_TOKEN_QUANTITY_FLAG && key.second.first == address) {
                totalEntries++;
            }
            pcursor->Next();
        }
        return true;
    }

    size_t skip = 0;
    if (start >= 0) {
        skip = start;
    }
    else {
        // compute table size for backwards offset
        long table_size = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == ADDRESS_TOKEN_QUANTITY_FLAG && key.second.first == address) {
                table_size += 1;
            }
            pcursor->Next();
        }
        skip = table_size + start;
        pcursor->SeekToFirst();
    }

    size_t loaded = 0;
    size_t offset = 0;

    // Load tokens
    while (pcursor->Valid() && loaded < count && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == ADDRESS_TOKEN_QUANTITY_FLAG && key.second.first == address) {
                if (offset < skip) {
                    offset += 1;
                }
                else {
                    CAmount amount;
                    if (pcursor->GetValue(amount)) {
                        vecTokenAmount.emplace_back(std::make_pair(key.second.second, amount));
                        loaded += 1;
                    } else {
                        return error("%s: failed to Address Token Quanity", __func__);
                    }
                }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

// Can get to total count of addresses that belong to a certain token_name, or get you the list of all address that belong to a certain token_name
bool CTokensDB::TokenAddressDir(std::vector<std::pair<std::string, CAmount> >& vecAddressAmount, int& totalEntries, const bool& fGetTotal, const std::string& tokenName, const size_t count, const long start)
{
    FlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, std::string())));

    if (fGetTotal) {
        totalEntries = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG && key.second.first == tokenName) {
                totalEntries += 1;
            }
            pcursor->Next();
        }
        return true;
    }

    size_t skip = 0;
    if (start >= 0) {
        skip = start;
    }
    else {
        // compute table size for backwards offset
        long table_size = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG && key.second.first == tokenName) {
                table_size += 1;
            }
            pcursor->Next();
        }
        skip = table_size + start;
        pcursor->SeekToFirst();
    }

    size_t loaded = 0;
    size_t offset = 0;

    // Load tokens
    while (pcursor->Valid() && loaded < count && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG && key.second.first == tokenName) {
            if (offset < skip) {
                offset += 1;
            }
            else {
                CAmount amount;
                if (pcursor->GetValue(amount)) {
                    vecAddressAmount.emplace_back(std::make_pair(key.second.second, amount));
                    loaded += 1;
                } else {
                    return error("%s: failed to Token Address Quanity", __func__);
                }
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CTokensDB::TokenDir(std::vector<CDatabasedTokenData>& tokens)
{
    return CTokensDB::TokenDir(tokens, "*", MAX_SIZE, 0);
}

bool CTokensDB::TokenAddressDirUnique(std::string& holderAddress, const std::string& tokenName)
{
    FlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, std::string())));

    size_t skip = 0;

    size_t loaded = 0;
    size_t offset = 0;

    size_t count = 1;

    // Load tokens
    while (pcursor->Valid() && loaded < count && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG && key.second.first == tokenName) {

            holderAddress = key.second.second;
            loaded += 1;

            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}
