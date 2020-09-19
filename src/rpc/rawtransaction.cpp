// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "validation.h"
#include "validationinterface.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "primitives/transaction.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#endif

#include <future>
#include <stdint.h>
#include "tokens/tokens.h"

#include <univalue.h>
#include <tinyformat.h>
#include "timedata.h"

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, bool expanded = false)
{
    // Call into TxToUniv() in aokchain-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in aokchain-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry, true, RPCSerializationFlags());

    if (expanded) {
        uint256 txid = tx.GetHash();
        if (!(tx.IsCoinBase())) {
            const UniValue& oldVin = entry["vin"];
            UniValue newVin(UniValue::VARR);
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const CTxIn& txin = tx.vin[i];
                UniValue in = oldVin[i];

                // Add address and value info if spentindex enabled
                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
                if (GetSpentIndex(spentKey, spentInfo)) {
                    in.pushKV("value", ValueFromAmount(spentInfo.satoshis));
                    in.pushKV("valueSat", spentInfo.satoshis);
                    if (spentInfo.addressType == 1) {
                        in.pushKV("address", CAokChainAddress(CKeyID(spentInfo.addressHash)).ToString());
                    } else if (spentInfo.addressType == 2) {
                        in.pushKV("address", CAokChainAddress(CScriptID(spentInfo.addressHash)).ToString());
                    }
                }
                newVin.push_back(in);
            }
            entry.pushKV("vin", newVin);
        }

        const UniValue& oldVout = entry["vout"];
        UniValue newVout(UniValue::VARR);
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut& txout = tx.vout[i];
            UniValue out = oldVout[i];

            // Add spent information if spentindex is enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txid, i);
            if (GetSpentIndex(spentKey, spentInfo)) {
                out.pushKV("spentTxId", spentInfo.txid.GetHex());
                out.pushKV("spentIndex", (int)spentInfo.inputIndex);
                out.pushKV("spentHeight", spentInfo.blockHeight);
            }

            out.pushKV("valueSat", txout.nValue);
            newVout.push_back(out);
        }
        entry.pushKV("vout", newVout);
    }

    if (!hashBlock.IsNull()) {
        entry.pushKV("blockhash", hashBlock.GetHex());
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.pushKV("height", pindex->nHeight);
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            } else {
                entry.pushKV("height", -1);
                entry.pushKV("confirmations", 0);
            }
        }
    }
}

UniValue getrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions.\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (bool, optional, default=false) If false, return a string, otherwise return a json object\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"address\"        (string) aokchain address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
        );

    LOCK(cs_main);
    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        if (request.params[1].isNum()) {
            if (request.params[1].get_int() != 0) {
                fVerbose = true;
            }
        }
        else if(request.params[1].isBool()) {
            if(request.params[1].isTrue()) {
                fVerbose = true;
            }
        }
        else {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid type provided. Verbose parameter must be a boolean.");
        }
    }

    CTransactionRef tx;

    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string(fTxIndex ? "No such mempool or blockchain transaction"
            : "No such mempool transaction. Use -txindex to enable blockchain transaction queries") +
            ". Use gettransaction for wallet transactions.");

    if (!fVerbose)
        return EncodeHexTx(*tx, RPCSerializationFlags());

    UniValue result(UniValue::VOBJ);
    TxToJSON(*tx, hashBlock, result, true);

    return result;
}

UniValue gettxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included manually (by blockhash).\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n"
        );

    std::set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = request.params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = nullptr;

    uint256 hashBlock;
    if (!request.params[1].isNull())
    {
        hashBlock = uint256S(request.params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        // Loop through txids and try to find which block they're in. Exit loop once a block is found.
        for (const auto& tx : setTxids) {
            const Coin& coin = AccessByTxid(*pcoinsTip, tx);
            if (!coin.IsSpent()) {
                pblockindex = chainActive[coin.nHeight];
                break;
            }
        }
    }

    if (pblockindex == nullptr)
    {
        CTransactionRef tx;
        if (!GetTransaction(oneTxid, tx, Params().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto& tx : block.vtx)
        if (setTxids.count(tx->GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not all transactions found in specified or retrieved block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid\n"
        );

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);

    if (!mapBlockIndex.count(merkleBlock.header.GetBlockHash()) || !chainActive.Contains(mapBlockIndex[merkleBlock.header.GetBlockHash()]))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    for (const uint256& hash : vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":(amount or object),\"data\":\"hex\",...} ( locktime ) ( replaceable )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs are addresses (paired with a AOK amount, data or object specifying an token operation) or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nPaying for Token Operations:\n"
            "  Some operations require an amount of AOK to be sent to a burn address:\n"
            "    transfer:       0\n"
            "    issue:        500 to Issue Burn Address\n"
            "    issue_unique    5 to Issue Unique Burn Address\n"
            "    reissue:      100 to Reissue Burn Address\n"

            "\nOwnership:\n"
            "  These operations require an ownership token input for the token being operated upon:\n"
            "    issue_unique\n"
            "    reissue\n"

            "\nOutput Ordering:\n"
            "  Token operations require the following:\n"
            "    1) All coin outputs come first (including the burn output).\n"
            "    2) The owner token change output comes next (if required).\n"
            "    3) An issue, issue_unique, reissue or any number of transfers comes last\n"
            "       (different types can't be mixed in a single transaction).\n"

            "\nArguments:\n"
            "1. \"inputs\"                                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",                      (string, required) The transaction id\n"
            "         \"vout\":n,                         (numeric, required) The output number\n"
            "         \"sequence\":n                      (numeric, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"                               (object, required) a json object with outputs\n"
            "     {\n"
            "       \"address\":                          (string, required) The destination aokchain address.  Each output must have a different address.\n"
            "         x.xxx                             (numeric or string, required) The AOK amount\n"
            "           or\n"
            "         {                                 (object) A json object of tokens to send\n"
            "           \"transfer\":\n"
            "             {\n"
            "               \"token-name\":               (string, required) token name\n"
            "               token-quantity              (numeric, required) the number of raw units to transfer\n"
            "               ,...\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing new tokens to issue\n"
            "           \"issue\":\n"
            "             {\n"
            "               \"token_name\":\"token-name\",  (string, required) new token name\n"
            "               \"token_quantity\":n,         (number, required) the number of raw units to issue\n"
            "               \"units\":[1-8],              (number, required) display units, between 1 (integral) to 8 (max precision)\n"
            "               \"reissuable\":[0-1],         (number, required) 1=reissuable token\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing new unique tokens to issue\n"
            "           \"issue_unique\":\n"
            "             {\n"
            "               \"root_name\":\"root-name\",         (string, required) name of the token the unique token(s) are being issued under\n"
            "               \"token_tags\":[\"token_tag\", ...], (array, required) the unique tag for each token which is to be issued\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing follow-on token issue.  Requires matching ownership input.\n"
            "           \"reissue\":\n"
            "             {\n"
            "               \"token_name\":\"token-name\",  (string, required) name of token to be reissued\n"
            "               \"token_quantity\":n,         (number, required) the number of raw units to issue\n"
            "               \"reissuable\":[0-1],         (number, optional) default is 1, 1=reissuable token\n"
            "             }\n"
            "         }\n"
            "         or\n"
            "       \"data\": \"hex\"                       (string, required) The key is \"data\", the value is hex encoded data\n"
            "       ,...\n"
            "     }\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
//            "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
//            "                                        Allows this transaction to be replaced by a transaction with higher fees.\n"
//            "                                        If provided, it is an error if explicit sequence numbers are incompatible.\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueTokenXXXXXXXXXXXXXXXXXhhZGt\\\":500,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue\\\":{\\\"token_name\\\":\\\"MYTOKEN\\\",\\\"token_quantity\\\":1000000,\\\"units\\\":1,\\\"reissuable\\\":0\\\"}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueUniqueTokenXXXXXXXXXXWEAe58\\\":20,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue_unique\\\":{\\\"root_name\\\":\\\"MYTOKEN\\\",\\\"token_tags\\\":[\\\"AOKHA\\\",\\\"BETA\\\"],\\\"]}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"mytoken\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":{\\\"transfer\\\":{\\\"MYTOKEN\\\":50}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"myownership\\\",\\\"vout\\\":0}]\" \"{\\\"issuer_address\\\":{\\\"reissue\\\":{\\\"token_name\\\":\\\"MYTOKEN\\\",\\\"token_quantity\\\":2000000}}}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ, UniValue::VNUM}, true);
    if (request.params[0].isNull() || request.params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = request.params[0].get_array();
    UniValue sendTo = request.params[1].get_obj();

    CMutableTransaction rawTx;

    rawTx.nTime = GetAdjustedTime();

    if (!request.params[2].isNull()) {
        int64_t nLockTime = request.params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

//    bool rbfOptIn = request.params[3].isTrue();

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence;
        if (rawTx.nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    auto currentActiveTokenCache = GetCurrentTokenCache();

    std::set<CTxDestination> destinations;
    std::vector<std::string> addrList = sendTo.getKeys();
    for (const std::string& name_ : addrList) {

        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(),"Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination = DecodeDestination(name_);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid AokChain address: ") + name_);
            }

            if (!destinations.insert(destination).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }

            CScript scriptPubKey = GetScriptForDestination(destination);
            CScript ownerPubKey = GetScriptForDestination(destination);


            if (sendTo[name_].type() == UniValue::VNUM || sendTo[name_].type() == UniValue::VSTR) {
                CAmount nAmount = AmountFromValue(sendTo[name_]);
                CTxOut out(nAmount, scriptPubKey);
                rawTx.vout.push_back(out);
            }
            /** AOK COIN START **/
            else if (sendTo[name_].type() == UniValue::VOBJ) {
                auto token_ = sendTo[name_].get_obj();
                auto tokenKey_ = token_.getKeys()[0];

                if (tokenKey_ == "issue")
                {

                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto tokenData = token_.getValues()[0].get_obj();

                    /**-------Process the tokens data-------**/
                    const UniValue& token_name = find_value(tokenData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_name");

                    const UniValue& token_quantity = find_value(tokenData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_quantity");

                    const UniValue& units = find_value(tokenData, "units");
                    if (!units.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: units");

                    const UniValue& reissuable = find_value(tokenData, "reissuable");
                    if (!reissuable.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: reissuable");

                    CAmount nAmount = AmountFromValue(token_quantity);

                    // Create a new token
                    CNewToken token(token_name.get_str(), nAmount, units.get_int(), reissuable.get_int(), 0, DecodeIPFS(""));

                    // Verify that data
                    std::string strError = "";
                    if (!token.IsValid(strError, *currentActiveTokenCache))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                    // Construct the token transaction
                    token.ConstructTransaction(scriptPubKey);

                    KnownTokenType type;
                    if (IsTokenNameValid(token.strName, type)) {
                        if (type != KnownTokenType::UNIQUE) {
                            token.ConstructOwnerTransaction(ownerPubKey);

                            // Push the scriptPubKey into the vouts.
                            CTxOut ownerOut(0, ownerPubKey);
                            rawTx.vout.push_back(ownerOut);
                        }
                    } else {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, ("Invalid parameter, invalid token name"));
                    }

                    // Push the scriptPubKey into the vouts.
                    CTxOut out(0, scriptPubKey);
                    rawTx.vout.push_back(out);

                }
                else if (tokenKey_ == "issue_unique")
                {

                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_unique\": {\"root_name\": value}, ...}"));

                    // Get the token data object from the json
                    auto tokenData = token_.getValues()[0].get_obj();

                    /**-------Process the tokens data-------**/
                    const UniValue& root_name = find_value(tokenData, "root_name");
                    if (!root_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: root_name");

                    const UniValue& token_tags = find_value(tokenData, "token_tags");
                    if (!token_tags.isArray() || token_tags.size() < 1)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_tags");

                    // Create the scripts for the change of the ownership token
                    CScript scriptTransferOwnerToken = GetScriptForDestination(destination);
                    CTokenTransfer tokenTransfer(root_name.get_str() + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
                    tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);

                    // Create the CTxOut for the owner token
                    CTxOut out(0, scriptTransferOwnerToken);
                    rawTx.vout.push_back(out);

                    // Create the tokens
                    for (int i = 0; i < (int)token_tags.size(); i++) {

                        // Create a new token
                        CNewToken token;
                        token = CNewToken(GetUniqueTokenName(root_name.get_str(), token_tags[i].get_str()),
                                              UNIQUE_TOKEN_AMOUNT,  UNIQUE_TOKEN_UNITS, UNIQUE_TOKENS_REISSUABLE, 0, "");

                        // Verify that data
                        std::string strError = "";
                        if (!token.IsValid(strError, *currentActiveTokenCache))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                        // Construct the token transaction
                        scriptPubKey = GetScriptForDestination(destination);
                        token.ConstructTransaction(scriptPubKey);

                        // Push the scriptPubKey into the vouts.
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);

                    }
                }
                else if (tokenKey_ == "reissue")
                {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"reissue\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto reissueData = token_.getValues()[0].get_obj();

                    CReissueToken reissueObj;

                    /**-------Process the reissue data-------**/
                    const UniValue& token_name = find_value(reissueData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing reissue data for key: token_name");

                    const UniValue& token_quantity = find_value(reissueData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing reissue data for key: token_quantity");

                    const UniValue& reissuable = find_value(reissueData, "reissuable");
                    if (!reissuable.isNull()) {
                        if (!reissuable.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: reissuable");

                        int nReissuable = reissuable.get_int();
                        if (nReissuable > 1 || nReissuable < 0)
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, reissuable data must be a 0 or 1");

                        reissueObj.nReissuable = int8_t(nReissuable);
                    }

                    // Add the received data into the reissue object
                    reissueObj.strName = token_name.get_str();
                    reissueObj.nAmount = AmountFromValue(token_quantity);

                    // Validate the the object is valid
                    std::string strError;
                    if (!reissueObj.IsValid(strError, *currentActiveTokenCache))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);


                    // Create the scripts for the change of the ownership token
                    CScript scriptTransferOwnerToken = GetScriptForDestination(destination);
                    CTokenTransfer tokenTransfer(token_name.get_str() + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
                    tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);

                    // Create the scripts for the reissued tokens
                    CScript scriptReissueToken = GetScriptForDestination(destination);
                    reissueObj.ConstructTransaction(scriptReissueToken);

                    // Create the CTxOut for the owner token
                    CTxOut out(0, scriptTransferOwnerToken);
                    rawTx.vout.push_back(out);

                    // Create the CTxOut for the reissue token
                    CTxOut out2(0, scriptReissueToken);
                    rawTx.vout.push_back(out2);

                } else if (tokenKey_ == "transfer") {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"transfer\": {\"token_name\": amount, ...} }"));

                    UniValue transferData = token_.getValues()[0].get_obj();
                    auto keys = transferData.getKeys();

                    if (keys.size() == 0)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"transfer\": {\"token_name\": amount, ...} }"));

                    UniValue token_quantity;
                    for (auto token_name : keys) {
                        token_quantity = find_value(transferData, token_name);

                        if (!token_quantity.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing or invalid quantity");

                        CAmount nAmount = AmountFromValue(token_quantity);

                        // Create a new transfer
                        // ToDo: Pass nTokenLockTime here
                        CTokenTransfer transfer(token_name, nAmount, 0);

                        // Verify
                        std::string strError = "";
                        if (!transfer.IsValid(strError))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                        // Construct transaction
                        CScript scriptPubKey = GetScriptForDestination(destination);
                        transfer.ConstructTransaction(scriptPubKey);

                        // Push into vouts
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);
                    }
                }
                else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, unknown output type (should be 'issue', 'reissue' or 'transfer'): " + tokenKey_));
                }
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, Output must be of the type object"));
            }
            /** AOK COIN STOP **/
        }
    }

//    if (!request.params[3].isNull() && rbfOptIn != SignalsOptInRBF(rawTx)) {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter combination: Sequence number(s) contradict replaceable option");
//    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) aokchain address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR});

    CMutableTransaction mtx;

    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToUniv(CTransaction(std::move(mtx)), uint256(), result, false);

    return result;
}

UniValue decodescript(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) aokchain address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\":\"address\",       (string) address of P2SH script wrapping this redeem script (not returned if the script is already a P2SH).\n"
            "  \"(The following only appears if the script is an token script)\n"
            "  \"token_name\":\"name\",      (string) Name of the token.\n"
            "  \"amount\":\"x.xx\",          (numeric) The amount of tokens interacted with.\n"
            "  \"units\": n,                (numeric) The units of the token. (Only appears in the type (new_token))\n"
            "  \"reissuable\": true|false, (boolean) If this token is reissuable. (Only appears in type (new_token|reissue_token))\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0){
        std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToUniv(script, r, false);

    UniValue type;
    type = find_value(r, "type");

    if (type.isStr() && type.get_str() != "scripthash") {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.pushKV("p2sh", EncodeDestination(CScriptID(script)));
    }

    /** TOKENS START */
    if (type.isStr() && type.get_str() == TOKEN_TRANSFER_STRING) {
        if (!AreTokensDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tokens are not active");

        CTokenTransfer transfer;
        std::string address;

        if (!TransferTokenFromScript(script, transfer, address))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the transfer token script");

        r.pushKV("token_name", transfer.strName);
        r.pushKV("amount", ValueFromAmount(transfer.nAmount));

    } else if (type.isStr() && type.get_str() == TOKEN_REISSUE_STRING) {
        if (!AreTokensDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tokens are not active");

        CReissueToken reissue;
        std::string address;

        if (!ReissueTokenFromScript(script, reissue, address))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the reissue token script");

        r.pushKV("token_name", reissue.strName);
        r.pushKV("amount", ValueFromAmount(reissue.nAmount));

        bool reissuable = reissue.nReissuable ? true : false;
        r.pushKV("reissuable", reissuable);

    } else if (type.isStr() && type.get_str() == TOKEN_NEW_STRING) {
        if (!AreTokensDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tokens are not active");

        CNewToken token;
        std::string ownerToken;
        std::string address;

        if(TokenFromScript(script, token, address)) {
            r.pushKV("token_name", token.strName);
            r.pushKV("amount", ValueFromAmount(token.nAmount));
            r.pushKV("units", token.units);

            bool reissuable = token.nReissuable ? true : false;
            r.pushKV("reissuable", reissuable);
        }
        else if (OwnerTokenFromScript(script, ownerToken, address))
        {
            r.pushKV("token_name", ownerToken);
            r.pushKV("amount", ValueFromAmount(OWNER_TOKEN_AMOUNT));
            r.pushKV("units", OWNER_UNITS);
        }
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the new token script");
        }
    } else {

    }
    /** TOKENS END */

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hash.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    UniValue witness(UniValue::VARR);
    for (unsigned int i = 0; i < txin.scriptWitness.stack.size(); i++) {
        witness.push_back(HexStr(txin.scriptWitness.stack[i].begin(), txin.scriptWitness.stack[i].end()));
    }
    entry.pushKV("witness", witness);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

UniValue combinerawtransaction(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinerawtransaction [\"hexstring\",...]\n"
            "\nCombine multiple partially signed transactions into one transaction.\n"
            "The combined transaction may be another partially signed transaction or a \n"
            "fully signed transaction."

            "\nArguments:\n"
            "1. \"txs\"         (string) A json array of hex strings of partially signed transactions\n"
            "    [\n"
            "      \"hexstring\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "\"hex\"            (string) The hex-encoded raw transaction with signature(s)\n"

            "\nExamples:\n"
            + HelpExampleCli("combinerawtransaction", "[\"myhex1\", \"myhex2\", \"myhex3\"]")
        );


    UniValue txs = request.params[0].get_array();
    std::vector<CMutableTransaction> txVariants(txs.size());

    for (unsigned int idx = 0; idx < txs.size(); idx++) {
        if (!DecodeHexTx(txVariants[idx], txs[idx].get_str(), true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed for tx %d", idx));
        }
    }

    if (txVariants.empty()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transactions");
    }

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mergedTx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Input not found or already spent");
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;

        // ... and merge in other signatures:
        for (const CMutableTransaction& txv : txVariants) {
            if (txv.vin.size() > i) {
                sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(txv, i));
            }
        }

        UpdateTransaction(mergedTx, i, sigdata);
    }

    return EncodeHexTx(mergedTx);
}

UniValue signrawtransaction(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
#endif

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase(pwallet) + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privkeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

    ObserveSafeMode();
#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (!request.params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = request.params[2].get_array();
        for (unsigned int idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CAokChainSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwallet) {
        EnsureWalletIsUnlocked(pwallet);
    }
#endif

    // Add previous txouts given in the RPC call:
    if (!request.params[1].isNull()) {
        UniValue prevTxs = request.params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin& coin = view.AccessCoin(out);
                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = 0;
                if (prevOut.exists("amount")) {
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"txid", UniValueType(UniValue::VSTR)},
                        {"vout", UniValueType(UniValue::VNUM)},
                        {"scriptPubKey", UniValueType(UniValue::VSTR)},
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwallet) ? tempKeystore : *pwallet);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (!request.params[3].isNull()) {
        static std::map<std::string, int> mapSigHashValues = {
            {std::string("ALL"), int(SIGHASH_ALL)},
            {std::string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY)},
            {std::string("NONE"), int(SIGHASH_NONE)},
            {std::string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY)},
            {std::string("SINGLE"), int(SIGHASH_SINGLE)},
            {std::string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY)},
        };
        std::string strHashType = request.params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mtx, i, amount, nHashType), prevPubKey, sigdata);
        sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(mtx, i));

        UpdateTransaction(mtx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
            } else {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(mtx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }

    return result;
}

UniValue sendrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    ObserveSafeMode();

    std::promise<void> promise;

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& hashTx = tx->GetHash();

    CAmount nMaxRawTxFee = maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool())
        nMaxRawTxFee = 0;

    { // cs_main scope
    LOCK(cs_main);
    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    for (size_t o = 0; !fHaveChain && o < tx->vout.size(); o++) {
        const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
        fHaveChain = !existingCoin.IsSpent();
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, std::move(tx), &fMissingInputs,
                                nullptr /* plTxnReplaced */, false /* bypass_limits */, nMaxRawTxFee)) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        } else {
            // If wallet is enabled, ensure that the wallet has been made aware
            // of the new transaction prior to returning. This prevents a race
            // where a user might call sendrawtransaction with a transaction
            // to/from their wallet, immediately call some wallet RPC, and get
            // a stale result because callbacks have not yet been processed.
            CallFunctionInValidationInterfaceQueue([&promise] {
                promise.set_value();
            });
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    } else {
        // Make sure we don't block forever if re-sending
        // a transaction already in mempool.
        promise.set_value();
    }

    } // cs_main

    promise.get_future().wait();

    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CInv inv(MSG_TX, hashTx);
    g_connman->ForEachNode([&inv](CNode* pnode)
    {
        pnode->PushInventory(inv);
    });

    return hashTx.GetHex();
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      &getrawtransaction,      {"txid","verbose"} },
    { "rawtransactions",    "createrawtransaction",   &createrawtransaction,   {"inputs","outputs","locktime"} },
    { "rawtransactions",    "decoderawtransaction",   &decoderawtransaction,   {"hexstring"} },
    { "rawtransactions",    "decodescript",           &decodescript,           {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",     &sendrawtransaction,     {"hexstring","allowhighfees"} },
    { "rawtransactions",    "combinerawtransaction",  &combinerawtransaction,  {"txs"} },
    { "rawtransactions",    "signrawtransaction",     &signrawtransaction,     {"hexstring","prevtxs","privkeys","sighashtype"} }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",          &gettxoutproof,          {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",       &verifytxoutproof,       {"proof"} },
};

void RegisterRawTransactionRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
