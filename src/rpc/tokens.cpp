// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokens/tokens.h"
#include "tokens/tokendb.h"
#include <map>
#include "tinyformat.h"

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "httpserver.h"
#include "validation.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "wallet/rpcwallet.h"

std::string TokenActivationWarning()
{
    return AreTokensDeployed() ? "" : "\nTHIS COMMAND IS NOT ACTIVATED YET!\n";
}

std::string TokenTypeToString(KnownTokenType& tokenType)
{
    switch (tokenType)
    {
        case KnownTokenType::ROOT:          return "ROOT";
        case KnownTokenType::SUB:           return "SUB";
        case KnownTokenType::UNIQUE:        return "UNIQUE";
        case KnownTokenType::OWNER:         return "OWNER";
        case KnownTokenType::REISSUE:       return "REISSUE";
        case KnownTokenType::USERNAME:      return "USERNAME";
        case KnownTokenType::INVALID:       return "INVALID";
        default:                       return "UNKNOWN";
    }
}

UniValue UnitValueFromAmount(const CAmount& amount, const std::string token_name)
{

    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (!currentActiveTokenCache)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Token cache isn't available.");

    uint8_t units = OWNER_UNITS;
    if (!IsTokenNameAnOwner(token_name)) {
        CNewToken tokenData;
        if (!currentActiveTokenCache->GetTokenMetaDataIfExists(token_name, tokenData))
            units = MAX_UNIT;
            //throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't load token from cache: " + token_name);
        else
            units = tokenData.units;
    }

    return ValueFromAmount(amount, units);
}

UniValue issue(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 1 || request.params.size() > 8)
        throw std::runtime_error(
            "issue \"token_name\" qty \"( to_address )\" \"( change_address )\" ( units ) ( reissuable )\n"
            + TokenActivationWarning() +
            "\nIssue an token, subtoken or unique token.\n"
            "Token name must not conflict with any existing token.\n"
            "Unit as the number of decimals precision for the token (0 for whole units (\"1\"), 8 for max precision (\"1.00000000\")\n"
            "Reissuable is true/false for whether additional units can be issued by the original issuer.\n"
            "If issuing a unique token these values are required (and will be defaulted to): qty=1, units=0, reissuable=false.\n"

            "\nArguments:\n"
            "1. \"token_name\"            (string, required) a unique name\n"
            "2. \"qty\"                   (numeric, optional, default=1) the number of units to be issued\n"
            "3. \"to_address\"            (string), optional, default=\"\"), address token will be sent to, if it is empty, address will be generated for you\n"
            "4. \"change_address\"        (string), optional, default=\"\"), address the the AOK change will be sent to, if it is empty, change address will be generated for you\n"
            "5. \"units\"                 (integer, optional, default=0, min=0, max=8), the number of decimals precision for the token (0 for whole units (\"1\"), 8 for max precision (\"1.00000000\")\n"
            "6. \"reissuable\"            (boolean, optional, default=true (false for unique tokens)), whether future reissuance is allowed\n"

            "\nResult:\n"
            "\"txid\"                     (string) The transaction id\n"

            "\nExamples:\n"
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\"")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" 4")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" 2 true")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" 8 false true")
            + HelpExampleCli("issue", "\"TOKEN_NAME/SUB_TOKEN\" 1000 \"myaddress\" \"changeaddress\" 2 true")
            + HelpExampleCli("issue", "\"TOKEN_NAME#uniquetag\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tokenName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(tokenName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tokenName + std::string("\nError: ") + tokenError);
    }

    // Check tokenType supported
    if (!(tokenType == KnownTokenType::ROOT ||
        tokenType == KnownTokenType::SUB ||
        tokenType == KnownTokenType::UNIQUE ||
        tokenType == KnownTokenType::USERNAME)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + TokenTypeToString(tokenType));
    }

    CAmount nAmount = COIN;
    if (request.params.size() > 1)
        nAmount = AmountFromValue(request.params[1]);

    std::string address = "";
    if (request.params.size() > 2)
        address = request.params[2].get_str();

    if (!address.empty()) {
        CTxDestination destination = DecodeDestination(address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid AokChain address: ") + address);
        }
    } else {
        // Create a new address
        std::string strAccount;

        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        CKeyID keyID = newKey.GetID();

        pwallet->SetAddressBook(keyID, strAccount, "receive");

        address = EncodeDestination(keyID);
    }

    std::string changeAddress = "";
    if (request.params.size() > 3)
        changeAddress = request.params[3].get_str();
    if (!changeAddress.empty()) {
        CTxDestination destination = DecodeDestination(changeAddress);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               std::string("Invalid Change Address: Invalid AokChain address: ") + changeAddress);
        }
    }

    int units = 0;
    if (request.params.size() > 4)
        units = request.params[4].get_int();

    bool reissuable = (
        tokenType != KnownTokenType::UNIQUE &&
        tokenType != KnownTokenType::USERNAME);

    if (request.params.size() > 5)
        reissuable = request.params[5].get_bool();

    // check for required unique token params
    if (tokenType == KnownTokenType::UNIQUE && (nAmount != COIN || units != 0 || reissuable)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameters for issuing a unique token."));
    }

    CNewToken token(tokenName, nAmount, units, reissuable ? 1 : 0);

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(changeAddress);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, token, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue issueunique(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 2 || request.params.size() > 5)
        throw std::runtime_error(
                "issueunique \"root_name\" [token_tags] \"( to_address )\" \"( change_address )\"\n"
                + TokenActivationWarning() +
                "\nIssue unique token(s).\n"
                "root_name must be an token you own.\n"
                "An token will be created for each element of token_tags.\n"
                "Five (5) AOK will be burned for each token created.\n"

                "\nArguments:\n"
                "1. \"root_name\"             (string, required) name of the token the unique token(s) are being issued under\n"
                "2. \"token_tags\"            (array, required) the unique tag for each token which is to be issued\n"
                "3. \"to_address\"            (string, optional, default=\"\"), address tokens will be sent to, if it is empty, address will be generated for you\n"
                "4. \"change_address\"        (string, optional, default=\"\"), address the the AOK change will be sent to, if it is empty, change address will be generated for you\n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("issueunique", "\"MY_TOKEN\" \'[\"primo\",\"secundo\"]\'")
                + HelpExampleCli("issueunique", "\"MY_TOKEN\" \'[\"primo\",\"secundo\"]\' \'[\"first_hash\",\"second_hash\"]\'")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);


    const std::string rootName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(rootName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + rootName  + std::string("\nError: ") + tokenError);
    }
    if (tokenType != KnownTokenType::ROOT && tokenType != KnownTokenType::SUB) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Root token must be a regular top-level or sub-token."));
    }

    const UniValue& tokenTags = request.params[1];
    if (!tokenTags.isArray() || tokenTags.size() < 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Token tags must be a non-empty array."));
    }

    std::string address = "";
    if (request.params.size() > 2)
        address = request.params[2].get_str();

    if (!address.empty()) {
        CTxDestination destination = DecodeDestination(address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid AokChain address: ") + address);
        }
    } else {
        // Create a new address
        std::string strAccount;

        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        CKeyID keyID = newKey.GetID();

        pwallet->SetAddressBook(keyID, strAccount, "receive");

        address = EncodeDestination(keyID);
    }

    std::string changeAddress = "";
    if (request.params.size() > 3)
        changeAddress = request.params[3].get_str();
    if (!changeAddress.empty()) {
        CTxDestination destination = DecodeDestination(changeAddress);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               std::string("Invalid Change Address: Invalid AokChain address: ") + changeAddress);
        }
    }

    std::vector<CNewToken> tokens;
    for (int i = 0; i < (int)tokenTags.size(); i++) {
        std::string tag = tokenTags[i].get_str();

        if (!IsUniqueTagValid(tag)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unique token tag is invalid: " + tag));
        }

        std::string tokenName = GetUniqueTokenName(rootName, tag);
        CNewToken token;

        token = CNewToken(tokenName, UNIQUE_TOKEN_AMOUNT, UNIQUE_TOKEN_UNITS, UNIQUE_TOKENS_REISSUABLE);

        tokens.push_back(token);
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;

    crtl.destChange = DecodeDestination(changeAddress);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, tokens, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue listtokenbalancesbyaddress(const JSONRPCRequest& request)
{
    if (!fTokenIndex) {
        return "_This rpc call is not functional unless -tokenindex is enabled. To enable, please run the wallet with -tokenindex, this will require a reindex to occur";
    }

    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 1)
        throw std::runtime_error(
            "listtokenbalancesbyaddress \"address\" (onlytotal) (count) (start)\n"
            + TokenActivationWarning() +
            "\nReturns a list of all token balances for an address.\n"

            "\nArguments:\n"
            "1. \"address\"                  (string, required) a aokchain address\n"
            "2. \"onlytotal\"                (boolean, optional, default=false) when false result is just a list of tokens balances -- when true the result is just a single number representing the number of tokens\n"
            "3. \"count\"                    (integer, optional, default=50000, MAX=50000) truncates results to include only the first _count_ tokens found\n"
            "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

            "\nResult:\n"
            "{\n"
            "  (token_name) : (quantity),\n"
            "  ...\n"
            "}\n"


            "\nExamples:\n"
            + HelpExampleCli("listtokenbalancesbyaddress", "\"myaddress\" false 2 0")
            + HelpExampleCli("listtokenbalancesbyaddress", "\"myaddress\" true")
            + HelpExampleCli("listtokenbalancesbyaddress", "\"myaddress\"")
        );

    ObserveSafeMode();

    std::string address = request.params[0].get_str();
    CTxDestination destination = DecodeDestination(address);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid AokChain address: ") + address);
    }

    bool fOnlyTotal = false;
    if (request.params.size() > 1)
        fOnlyTotal = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    if (!ptokensdb)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "token db unavailable.");

    
    LOCK(cs_main);
    std::vector<std::pair<std::string, CAmount> > vecTokenAmounts;
    int nTotalEntries = 0;

    if (!ptokensdb->AddressDir(vecTokenAmounts, nTotalEntries, fOnlyTotal, address, count, start))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "couldn't retrieve address token directory.");

    // If only the number of addresses is wanted return it
    if (fOnlyTotal) {
        return nTotalEntries;
    }

    UniValue result(UniValue::VOBJ);
    for (auto& pair : vecTokenAmounts) {
        result.pushKV(pair.first, UnitValueFromAmount(pair.second, pair.first));
    }

    return result;
}

UniValue gettokendata(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "gettokendata \"token_name\"\n"
                + TokenActivationWarning() +
                "\nReturns tokens metadata if that token exists\n"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) the name of the token\n"

                "\nResult:\n"
                "{\n"
                "  name: (string),\n"
                "  amount: (number),\n"
                "  units: (number),\n"
                "  reissuable: (number),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("getalltokens", "\"TOKEN_NAME\"")
        );


    std::string token_name = request.params[0].get_str();

    LOCK(cs_main);
    UniValue result (UniValue::VOBJ);

    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (currentActiveTokenCache) {
        CNewToken token;
        if (!currentActiveTokenCache->GetTokenMetaDataIfExists(token_name, token))
            return NullUniValue;

        result.pushKV("name", token.strName);
        result.pushKV("amount", UnitValueFromAmount(token.nAmount, token.strName));
        result.pushKV("units", token.units);
        result.pushKV("reissuable", token.nReissuable);

        return result;
    }

    return NullUniValue;
}

template <class Iter, class Incr>
void safe_advance(Iter& curr, const Iter& end, Incr n)
{
    size_t remaining(std::distance(curr, end));
    if (remaining < n)
    {
        n = remaining;
    }
    std::advance(curr, n);
};

UniValue listmytokens(const JSONRPCRequest &request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4)
        throw std::runtime_error(
                "listmytokens \"( token )\" ( verbose ) ( count ) ( start )\n"
                + TokenActivationWarning() +
                "\nReturns a list of all token that are owned by this wallet\n"

                "\nArguments:\n"
                "1. \"token\"                    (string, optional, default=\"*\") filters results -- must be an token name or a partial token name followed by '*' ('*' matches all trailing characters)\n"
                "2. \"verbose\"                  (boolean, optional, default=false) when false results only contain balances -- when true results include outpoints\n"
                "3. \"count\"                    (integer, optional, default=ALL) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult (verbose=false):\n"
                "{\n"
                "  (token_name): balance,\n"
                "  ...\n"
                "}\n"

                "\nResult (verbose=true):\n"
                "{\n"
                "  (token_name):\n"
                "    {\n"
                "      \"balance\": balance,\n"
                "      \"outpoints\":\n"
                "        [\n"
                "          {\n"
                "            \"txid\": txid,\n"
                "            \"vout\": vout,\n"
                "            \"amount\": amount\n"
                "          }\n"
                "          {...}, {...}\n"
                "        ]\n"
                "    }\n"
                "}\n"
                "{...}, {...}\n"

                "\nExamples:\n"
                + HelpExampleRpc("listmytokens", "")
                + HelpExampleCli("listmytokens", "TOKEN")
                + HelpExampleCli("listmytokens", "\"TOKEN*\" true 10 20")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string filter = "*";
    if (request.params.size() > 0)
        filter = request.params[0].get_str();

    if (filter == "")
        filter = "*";

    bool verbose = false;
    if (request.params.size() > 1)
        verbose = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    // retrieve balances
    std::map<std::string, CAmount> balances;
    std::map<std::string, std::vector<COutput> > outputs;
    if (filter == "*") {
        if (!GetAllMyTokenBalancesWallet(pwallet, outputs, balances))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else if (filter.back() == '*') {
        std::vector<std::string> tokenNames;
        filter.pop_back();
        if (!GetAllMyTokenBalancesWallet(pwallet, outputs, balances, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else {
        if (!IsTokenNameValid(filter))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token name.");
        if (!GetAllMyTokenBalancesWallet(pwallet, outputs, balances, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }

    // pagination setup
    auto bal = balances.begin();
    if (start >= 0) {
        safe_advance(bal, balances.end(), (size_t)start);
    } else {
        safe_advance(bal, balances.end(), balances.size() + start);
    }

    auto end = bal;
    safe_advance(end, balances.end(), count);

    // generate output
    UniValue result(UniValue::VOBJ);
    if (verbose) {
        for (; bal != end && bal != balances.end(); bal++) {
            UniValue token(UniValue::VOBJ);
            token.pushKV("balance", UnitValueFromAmount(bal->second, bal->first));

            UniValue outpoints(UniValue::VARR);
            for (auto const& out : outputs.at(bal->first)) {
                UniValue tempOut(UniValue::VOBJ);
                tempOut.pushKV("txid", out.tx->GetHash().GetHex());
                tempOut.pushKV("vout", (int)out.i);

                //
                // get amount for this outpoint
                CAmount txAmount = 0;
                auto it = pwallet->mapWallet.find(out.tx->GetHash());
                if (it == pwallet->mapWallet.end()) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
                }
                const CWalletTx* wtx = out.tx;
                CTxOut txOut = wtx->tx->vout[out.i];
                std::string strAddress;
                int nTokenLockTime = 0;
                if (CheckIssueDataTx(txOut)) {
                    CNewToken token;
                    if (!TokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckReissueDataTx(txOut)) {
                    CReissueToken token;
                    if (!ReissueTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckTransferOwnerTx(txOut)) {
                    CTokenTransfer token;
                    if (!TransferTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                    nTokenLockTime = token.nTokenLockTime;
                }
                else if (CheckOwnerDataTx(txOut)) {
                    std::string tokenName;
                    if (!OwnerTokenFromScript(txOut.scriptPubKey, tokenName, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = OWNER_TOKEN_AMOUNT;
                }
                tempOut.pushKV("amount", UnitValueFromAmount(txAmount, bal->first));
                if (nTokenLockTime > 0) {
                    tempOut.pushKV("token_lock_time", (int)nTokenLockTime);
                }

                outpoints.push_back(tempOut);
            }
            token.pushKV("outpoints", outpoints);
            result.pushKV(bal->first, token);
        }
    }
    else {
        for (; bal != end && bal != balances.end(); bal++) {
            result.pushKV(bal->first, UnitValueFromAmount(bal->second, bal->first));
        }
    }
    return result;
}

UniValue listmylockedtokens(const JSONRPCRequest &request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4)
        throw std::runtime_error(
                "listmylockedtokens \"( token )\" ( verbose ) ( count ) ( start )\n"
                + TokenActivationWarning() +
                "\nReturns a list of all locked token that are owned by this wallet\n"

                "\nArguments:\n"
                "1. \"token\"                    (string, optional, default=\"*\") filters results -- must be an token name or a partial token name followed by '*' ('*' matches all trailing characters)\n"
                "2. \"verbose\"                  (boolean, optional, default=false) when false results only contain balances -- when true results include outpoints\n"
                "3. \"count\"                    (integer, optional, default=ALL) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult (verbose=false):\n"
                "{\n"
                "  (token_name): balance,\n"
                "  ...\n"
                "}\n"

                "\nResult (verbose=true):\n"
                "{\n"
                "  (token_name):\n"
                "    {\n"
                "      \"balance\": balance,\n"
                "      \"outpoints\":\n"
                "        [\n"
                "          {\n"
                "            \"txid\": txid,\n"
                "            \"vout\": vout,\n"
                "            \"amount\": amount\n"
                "          }\n"
                "          {...}, {...}\n"
                "        ]\n"
                "    }\n"
                "}\n"
                "{...}, {...}\n"

                "\nExamples:\n"
                + HelpExampleRpc("listmylockedtokens", "")
                + HelpExampleCli("listmylockedtokens", "TOKEN")
                + HelpExampleCli("listmylockedtokens", "\"TOKEN*\" true 10 20")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string filter = "*";
    if (request.params.size() > 0)
        filter = request.params[0].get_str();

    if (filter == "")
        filter = "*";

    bool verbose = false;
    if (request.params.size() > 1)
        verbose = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    // retrieve balances
    std::map<std::string, CAmount> balances;
    std::map<std::string, std::vector<COutput> > outputs;
    if (filter == "*") {
        if (!GetAllMyLockedTokenBalancesWallet(pwallet, outputs, balances))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else if (filter.back() == '*') {
        std::vector<std::string> tokenNames;
        filter.pop_back();
        if (!GetAllMyLockedTokenBalancesWallet(pwallet, outputs, balances, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else {
        if (!IsTokenNameValid(filter))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token name.");
        if (!GetAllMyLockedTokenBalancesWallet(pwallet, outputs, balances, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }

    // pagination setup
    auto bal = balances.begin();
    if (start >= 0) {
        safe_advance(bal, balances.end(), (size_t)start);
    } else {
        safe_advance(bal, balances.end(), balances.size() + start);
    }

    auto end = bal;
    safe_advance(end, balances.end(), count);

    // generate output
    UniValue result(UniValue::VOBJ);
    if (verbose) {
        for (; bal != end && bal != balances.end(); bal++) {
            UniValue token(UniValue::VOBJ);
            token.pushKV("balance", UnitValueFromAmount(bal->second, bal->first));

            UniValue outpoints(UniValue::VARR);
            for (auto const& out : outputs.at(bal->first)) {
                UniValue tempOut(UniValue::VOBJ);
                tempOut.pushKV("txid", out.tx->GetHash().GetHex());
                tempOut.pushKV("vout", (int)out.i);

                //
                // get amount for this outpoint
                CAmount txAmount = 0;
                auto it = pwallet->mapWallet.find(out.tx->GetHash());
                if (it == pwallet->mapWallet.end()) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
                }
                const CWalletTx* wtx = out.tx;
                CTxOut txOut = wtx->tx->vout[out.i];
                std::string strAddress;
                int nTokenLockTime = 0;
                if (CheckIssueDataTx(txOut)) {
                    CNewToken token;
                    if (!TokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckReissueDataTx(txOut)) {
                    CReissueToken token;
                    if (!ReissueTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckTransferOwnerTx(txOut)) {
                    CTokenTransfer token;
                    if (!TransferTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                    nTokenLockTime = token.nTokenLockTime;
                }
                else if (CheckOwnerDataTx(txOut)) {
                    std::string tokenName;
                    if (!OwnerTokenFromScript(txOut.scriptPubKey, tokenName, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = OWNER_TOKEN_AMOUNT;
                }
                tempOut.pushKV("amount", UnitValueFromAmount(txAmount, bal->first));
                if (nTokenLockTime > 0) {
                    tempOut.pushKV("token_lock_time", (int)nTokenLockTime);
                }

                outpoints.push_back(tempOut);
            }
            token.pushKV("outpoints", outpoints);
            result.pushKV(bal->first, token);
        }
    }
    else {
        for (; bal != end && bal != balances.end(); bal++) {
            result.pushKV(bal->first, UnitValueFromAmount(bal->second, bal->first));
        }
    }
    return result;
}

UniValue listaddressesbytoken(const JSONRPCRequest &request)
{
    if (!fTokenIndex) {
        return "_This rpc call is not functional unless -tokenindex is enabled. To enable, please run the wallet with -tokenindex, this will require a reindex to occur";
    }

    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4 || request.params.size() < 1)
        throw std::runtime_error(
                "listaddressesbytoken \"token_name\" (onlytotal) (count) (start)\n"
                + TokenActivationWarning() +
                "\nReturns a list of all address that own the given token (with balances)"
                "\nOr returns the total size of how many address own the given token"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token\n"
                "2. \"onlytotal\"                (boolean, optional, default=false) when false result is just a list of addresses with balances -- when true the result is just a single number representing the number of addresses\n"
                "3. \"count\"                    (integer, optional, default=50000, MAX=50000) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult:\n"
                "[ "
                "  (address): balance,\n"
                "  ...\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listaddressesbytoken", "\"TOKEN_NAME\" false 2 0")
                + HelpExampleCli("listaddressesbytoken", "\"TOKEN_NAME\" true")
                + HelpExampleCli("listaddressesbytoken", "\"TOKEN_NAME\"")
        );

    LOCK(cs_main);

    std::string token_name = request.params[0].get_str();
    bool fOnlyTotal = false;
    if (request.params.size() > 1)
        fOnlyTotal = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    if (!IsTokenNameValid(token_name))
        return "_Not a valid token name";

    LOCK(cs_main);
    std::vector<std::pair<std::string, CAmount> > vecAddressAmounts;
    int nTotalEntries = 0;
    if (!ptokensdb->TokenAddressDir(vecAddressAmounts, nTotalEntries, fOnlyTotal, token_name, count, start))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "couldn't retrieve address token directory.");

    // If only the number of addresses is wanted return it
    if (fOnlyTotal) {
        return nTotalEntries;
    }

    UniValue result(UniValue::VOBJ);
    for (auto& pair : vecAddressAmounts) {
        result.pushKV(pair.first, UnitValueFromAmount(pair.second, token_name));
    }

    
    return result;
}

UniValue transfer(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
                "transfer \"token_name\" qty \"to_address\"\n"
                + TokenActivationWarning() +
                "\nTransfers a quantity of an owned token to a given address"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token\n"
                "2. \"qty\"                      (numeric, required) number of tokens you want to send to the address\n"
                "3. \"to_address\"               (string, required) address to send the token to\n"
                "4. \"token_lock_time\"          (integer, optional, default=0) Locktime for token UTXOs, could be height or timestamp\n"

                "\nResult:\n"
                "txid"
                "[ \n"
                "txid\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("transfer", "\"TOKEN_NAME\" 20 \"address\"")
                + HelpExampleCli("transfer", "\"TOKEN_NAME\" 20 \"address\" 120000")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string token_name = request.params[0].get_str();

    CAmount nAmount = AmountFromValue(request.params[1]);

    std::string address = request.params[2].get_str();

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    int token_lock_time = 0;
    if (request.params.size() > 3) {
        token_lock_time = request.params[3].get_int();
        if (token_lock_time < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "token_lock_time time must be greater or equal to 0.");
        }
    }

    vTransfers.emplace_back(std::make_pair(CTokenTransfer(token_name, nAmount, token_lock_time), address));
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue transfermany(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "transfermany \"token_name\" {\"address\":amount,...}\n"
            "\nSend multiple times. Amounts are double-precision floating point numbers."
            + TokenActivationWarning() +
            "\nArguments:\n"
            "1. \"token_name\"          (string, required) name of token\n"
            "2. \"amounts\"             (object, required) A json object with addresses and amounts\n"
            "    {\n"
            "      \"address\":amount   (numeric or string) The aokchain address is the key, the numeric amount (can be string) in " + CURRENCY_UNIT + " is the value\n"
            "      ,...\n"
            "    }\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            "\nSend two amounts to two different addresses:\n"
            + HelpExampleCli("transfermany", "\"TOKEN_NAME\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    std::string token_name = request.params[0].get_str();

    UniValue sendTo = request.params[1].get_obj();

    std::set<CTxDestination> destinations;
    std::vector<std::string> keys = sendTo.getKeys();

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    for (const std::string& address : keys) {
        int token_lock_time = 0;
        CTxDestination dest = DecodeDestination(address);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid AokChain address: ") + address);
        }

        if (destinations.count(dest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + address);
        }
        destinations.insert(dest);

        CAmount nAmount = AmountFromValue(sendTo[address]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

        vTransfers.emplace_back(std::make_pair(CTokenTransfer(token_name, nAmount, token_lock_time), address));
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    return transaction.GetHash().GetHex();
}

UniValue transfermanyoutputs(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "transfermanyoutputs [{\"address\":\"Kb9ma4bw6aRcB6CrbE67VPzGH8VUyafTrh\",...}]\n"
            "\nSend transaction with multiple outputs."
            + TokenActivationWarning() +
            "\nArguments:\n"
            "1. \"outputs\"             (array, required) name of token\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            "\nSend two amounts to two different addresses:\n"
            + HelpExampleCli("transfermanyoutputs", "[{\"address\":\"Kb9ma4bw6aRcB6CrbE67VPzGH8VUyafTrh\",...}]")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    UniValue inputs = request.params[0].get_array();

    std::pair<int, std::string> error;
    std::vector<std::pair<CTokenTransfer, std::string> >vTransfers;

    for (unsigned int i = 0; i < inputs.size(); i++)
    {
        UniValue sendTo = inputs[i].get_obj();

        if (!sendTo.exists("address"))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing address field");

        if (!sendTo.exists("token_name"))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing token name field");

        if (!sendTo.exists("amount"))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing amount field");

        if (!sendTo.exists("locktime"))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing locktime field");

        const UniValue& address_field = find_value(sendTo, "address");
        if (!address_field.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

        std::string address = address_field.get_str();
        CTxDestination dest = DecodeDestination(address);

        if (!IsValidDestination(dest))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid AokChain address: ") + address);

        CAmount nAmount = AmountFromValue(find_value(sendTo, "amount"));
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

        const UniValue& token_name_field = find_value(sendTo, "token_name");
        if (!token_name_field.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

        std::string token_name = token_name_field.get_str();

        int locktime = find_value(sendTo, "locktime").get_int();
        if (locktime < 0)
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "locktime time must be greater or equal to 0.");

        vTransfers.emplace_back(std::make_pair(CTokenTransfer(token_name, nAmount, locktime), address));
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    return transaction.GetHash().GetHex();
}

UniValue transferfromaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 4 || request.params.size() > 5)
        throw std::runtime_error(
                "transferfromaddress \"address\" \"token_name\" qty \"to_address\" \"token_lock_time\"\n"
                + TokenActivationWarning() +
                "\nTransfer a quantity of an owned token in a specific address to a given address"

                "\nArguments:\n"
                "1. \"address\"                  (string, required) valid AOK address\n"
                "2. \"token_name\"               (string, required) name of token\n"
                "3. \"qty\"                      (numeric, required) number of tokens you want to send to the address\n"
                "4. \"to_address\"               (string, required) address to send the token to\n"
                "5. \"token_lock_time\"          (integer, optional, default=0) Locktime for token UTXOs, could be height or timestamp\n"

                "\nResult:\n"
                "txid"
                "[ \n"
                "txid\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("transferfromaddress", "\"address\" \"ASSET_NAME\" 20 \"address\" \"token_lock_time\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string from_address = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(from_address);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("From address must be valid addresses. Invalid address: ") + from_address);

    std::string token_name = request.params[1].get_str();

    CAmount nAmount = AmountFromValue(request.params[2]);

    std::string address = request.params[3].get_str();

    int token_lock_time = 0;
    if (request.params.size() > 4) {
        token_lock_time = request.params[4].get_int();
        if (token_lock_time < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "token_lock_time time must be greater or equal to 0.");
        }
    }

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    vTransfers.emplace_back(std::make_pair(CTokenTransfer(token_name, nAmount, token_lock_time), address));
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;
    std::map<std::string, std::vector<COutput> > mapTokenCoins;
    pwallet->AvailableTokens(mapTokenCoins);

    if (!mapTokenCoins.count(token_name)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Wallet doesn't own the token_name: " + token_name));
    }

    // Add all the token outpoints that match the given from addresses
    for (const auto& out : mapTokenCoins.at(token_name)) {
        // Get the address that the coin resides in, because to send a valid message. You need to send it to the same address that it currently resides in.
        CTxDestination dest;
        ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, dest);

        if (from_address == EncodeDestination(dest))
            ctrl.SelectToken(COutPoint(out.tx->GetHash(), out.i));
    }

    std::vector<COutPoint> outs;
    ctrl.ListSelectedTokens(outs);
    if (!outs.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Account has insufficient token funds"));
    }

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue reissue(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 7 || request.params.size() < 3)
        throw std::runtime_error(
                "reissue \"token_name\" qty \"to_address\" \"change_address\" ( reissuable ) ( new_unit ) \n"
                + TokenActivationWarning() +
                "\nReissues a quantity of an token to an owned address if you own the Owner Token"
                "\nCan change the reissuable flag during reissuance"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token that is being reissued\n"
                "2. \"qty\"                      (numeric, required) number of tokens to reissue\n"
                "3. \"to_address\"               (string, required) address to send the token to\n"
                "4. \"change_address\"           (string, optional) address that the change of the transaction will be sent to\n"
                "5. \"reissuable\"               (boolean, optional, default=true), whether future reissuance is allowed\n"
                "6. \"new_unit\"                 (numeric, optional, default=-1), the new units that will be associated with the token\n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("reissue", "\"TOKEN_NAME\" 20 \"address\"")
                + HelpExampleCli("reissue", "\"TOKEN_NAME\" 20 \"address\" \"change_address\" \"true\" 8")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    // To send a transaction the wallet must be unlocked
    EnsureWalletIsUnlocked(pwallet);

    // Get that paramaters
    std::string token_name = request.params[0].get_str();
    CAmount nAmount = AmountFromValue(request.params[1]);
    std::string address = request.params[2].get_str();

    std::string changeAddress =  "";
    if (request.params.size() > 3)
        changeAddress = request.params[3].get_str();

    bool reissuable = true;
    if (request.params.size() > 4) {
        reissuable = request.params[4].get_bool();
    }

    int newUnits = -1;
    if (request.params.size() > 5) {
        newUnits = request.params[5].get_int();
    }

    CReissueToken reissueToken(token_name, nAmount, newUnits, reissuable);

    std::pair<int, std::string> error;
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(changeAddress);

    // Create the Transaction
    if (!CreateReissueTokenTransaction(pwallet, crtl, reissueToken, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue listtokens(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4)
        throw std::runtime_error(
                "listtokens \"( token )\" ( verbose ) ( count ) ( start )\n"
                + TokenActivationWarning() +
                "\nReturns a list of all tokens\n"
                "\nThis could be a slow/expensive operation as it reads from the database\n"

                "\nArguments:\n"
                "1. \"token\"                    (string, optional, default=\"*\") filters results -- must be an token name or a partial token name followed by '*' ('*' matches all trailing characters)\n"
                "2. \"verbose\"                  (boolean, optional, default=false) when false result is just a list of token names -- when true results are token name mapped to metadata\n"
                "3. \"count\"                    (integer, optional, default=ALL) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult (verbose=false):\n"
                "[\n"
                "  token_name,\n"
                "  ...\n"
                "]\n"

                "\nResult (verbose=true):\n"
                "{\n"
                "  (token_name):\n"
                "    {\n"
                "      amount: (number),\n"
                "      units: (number),\n"
                "      reissuable: (number),\n"
                "    },\n"
                "  {...}, {...}\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleRpc("listtokens", "")
                + HelpExampleCli("listtokens", "TOKEN")
                + HelpExampleCli("listtokens", "\"TOKEN*\" true 10 20")
        );

    ObserveSafeMode();

    if (!ptokensdb)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "token db unavailable.");

    std::string filter = "*";
    if (request.params.size() > 0)
        filter = request.params[0].get_str();

    if (filter == "")
        filter = "*";

    bool verbose = false;
    if (request.params.size() > 1)
        verbose = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    std::vector<CDatabasedTokenData> tokens;
    if (!ptokensdb->TokenDir(tokens, filter, count, start))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "couldn't retrieve token directory.");

    UniValue result;
    result = verbose ? UniValue(UniValue::VOBJ) : UniValue(UniValue::VARR);

    for (auto data : tokens) {
        CNewToken token = data.token;
        if (verbose) {
            UniValue detail(UniValue::VOBJ);
            detail.pushKV("name", token.strName);
            detail.pushKV("amount", UnitValueFromAmount(token.nAmount, token.strName));
            detail.pushKV("units", token.units);
            detail.pushKV("reissuable", token.nReissuable);
            detail.pushKV("block_height", data.nHeight);
            detail.pushKV("blockhash", data.blockHash.GetHex());
            result.pushKV(token.strName, detail);
        } else {
            result.push_back(token.strName);
        }
    }

    return result;
}

UniValue getcacheinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size())
        throw std::runtime_error(
                "getcacheinfo \n"
                + TokenActivationWarning() +

                "\nResult:\n"
                "[\n"
                "  uxto cache size:\n"
                "  token total (exclude dirty):\n"
                "  token address map:\n"
                "  token address balance:\n"
                "  my unspent token:\n"
                "  reissue data:\n"
                "  token metadata map:\n"
                "  token metadata list (est):\n"
                "  dirty cache (est):\n"


                "]\n"

                "\nExamples:\n"
                + HelpExampleRpc("getcacheinfo", "")
                + HelpExampleCli("getcacheinfo", "")
        );

    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (!currentActiveTokenCache)
        throw JSONRPCError(RPC_VERIFY_ERROR, "token cache is null");

    if (!pcoinsTip)
        throw JSONRPCError(RPC_VERIFY_ERROR, "coins tip cache is null");

    if (!ptokensCache)
        throw JSONRPCError(RPC_VERIFY_ERROR, "token metadata cache is nul");

    UniValue result(UniValue::VARR);

    UniValue info(UniValue::VOBJ);
    info.pushKV("uxto cache size", (int)pcoinsTip->DynamicMemoryUsage());
    info.pushKV("token total (exclude dirty)", (int)currentActiveTokenCache->DynamicMemoryUsage());

    UniValue descendants(UniValue::VOBJ);

    descendants.pushKV("token address balance",   (int)memusage::DynamicUsage(currentActiveTokenCache->mapTokensAddressAmount));
    descendants.pushKV("reissue data",   (int)memusage::DynamicUsage(currentActiveTokenCache->mapReissuedTokenData));

    info.pushKV("reissue tracking (memory only)", (int)memusage::DynamicUsage(mapReissuedTokens) + (int)memusage::DynamicUsage(mapReissuedTx));
    info.pushKV("token data", descendants);
    info.pushKV("token metadata map",  (int)memusage::DynamicUsage(ptokensCache->GetItemsMap()));
    info.pushKV("token metadata list (est)",  (int)ptokensCache->GetItemsList().size() * (32 + 80)); // Max 32 bytes for token name, 80 bytes max for token data
    info.pushKV("dirty cache (est)",  (int)currentActiveTokenCache->GetCacheSize());
    info.pushKV("dirty cache V2 (est)",  (int)currentActiveTokenCache->GetCacheSizeV2());

    result.push_back(info);
    return result;
}

static const CRPCCommand commands[] =
{ //  category    name                          actor (function)             argNames
  //  ----------- ------------------------      -----------------------      ----------
    { "tokens",   "issue",                      &issue,                      {"token_name","qty","to_address","change_address","units","reissuable"} },
    { "tokens",   "issueunique",                &issueunique,                {"root_name", "token_tags", "to_address", "change_address"}},
    { "tokens",   "listtokenbalancesbyaddress", &listtokenbalancesbyaddress, {"address", "onlytotal", "count", "start"} },
    { "tokens",   "gettokendata",               &gettokendata,               {"token_name"}},
    { "tokens",   "listmytokens",               &listmytokens,               {"token", "verbose", "count", "start"}},
    { "tokens",   "listmylockedtokens",         &listmylockedtokens,         {"token", "verbose", "count", "start"}},
    { "tokens",   "listaddressesbytoken",       &listaddressesbytoken,       {"token_name", "onlytotal", "count", "start"}},
    { "tokens",   "transfer",                   &transfer,                   {"token_name", "qty", "to_address", "token_lock_time"}},
    { "tokens",   "transfermany",               &transfermany,               {"token_name", "amounts"}},
    { "tokens",   "transfermanyoutputs",        &transfermanyoutputs,        {"outputs"}},
    { "tokens",   "transferfromaddress",        &transferfromaddress,        {"address", "token_name", "qty", "to_address", "token_lock_time"}},
    { "tokens",   "reissue",                    &reissue,                    {"token_name", "qty", "to_address", "change_address", "reissuable", "new_unit"}},
    { "tokens",   "listtokens",                 &listtokens,                 {"token", "verbose", "count", "start"}},
    { "tokens",   "getcacheinfo",               &getcacheinfo,               {}}
};

void RegisterTokenRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
