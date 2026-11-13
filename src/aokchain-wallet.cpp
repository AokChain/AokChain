// Copyright (c) 2016-2018 The Bitcoin Core developers
// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// aokchain-wallet: look into a wallet file, create one or salvage one, without a node.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "base58.h"
#include "chainparams.h"
#include "chainparamsbase.h"
#include "clientversion.h"
#include "fs.h"
#include "key.h"
#include "pubkey.h"
#include "random.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "wallet/db.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "wallet/walletutil.h"

#include <cstdio>
#include <memory>

#include <db_cxx.h>

static void Usage()
{
    std::string usage =
        "AokChain Core aokchain-wallet version " + FormatFullVersion() + "\n\n"
        "Looks into a wallet file, creates one or salvages one. No node is needed, and the node that\n"
        "uses the wallet must not be running.\n\n"
        "Usage:\n"
        "  aokchain-wallet -wallet=<wallet> [options] <command>\n\n"
        "Commands:\n"
        "  info        Show what the wallet file contains: format, encryption, HD seed, number of keys,\n"
        "              transactions and address book entries\n"
        "  addresses   List the addresses of the address book with their labels (-allkeys: every key of\n"
        "              the wallet, with the key pool and the change keys). The balance of an address is\n"
        "              not in the wallet file: ask a node with scantxoutset\n"
        "  create      Create a new wallet with an HD seed and a key pool (not encrypted). <wallet> becomes\n"
        "              a folder with the file wallet.dat in it; give that folder to the node as -wallet\n"
        "  salvage     Try to recover the private keys of a damaged wallet file; the file is kept as\n"
        "              <name>.<time>.bak\n\n"
        "Options:\n"
        "  -wallet=<wallet>    A wallet file (a path), or the name of a wallet in the wallet directory\n"
        "  -datadir=<dir>      The data directory whose wallet directory is used for a name\n"
        "  -walletdir=<dir>    The wallet directory for a name\n"
        "  -allkeys            addresses: list every key\n"
        "  -testnet, -regtest  The network of the wallet (for the form of the addresses)\n";
    fprintf(stdout, "%s", usage.c_str());
}

static std::unique_ptr<CWallet> OpenWallet(const std::string& name, bool create, std::string& error)
{
    const fs::path path = fs::absolute(name, GetWalletDir());
    const bool exists = fs::exists(fs::is_directory(path) ? path / "wallet.dat" : path);
    if (create && exists) {
        error = "The wallet file exists already: " + path.string();
        return nullptr;
    }
    if (!create && !exists) {
        error = "There is no wallet file " + path.string();
        return nullptr;
    }
    // Open the database environment (this takes the lock that a node holds on its wallets), and
    // check the file without the repair that a node would try: this tool changes nothing unasked.
    if (!CWalletDB::VerifyEnvironment(path, error)) {
        error += " (is a node using this wallet? Stop it first, or work on a copy of the file)";
        return nullptr;
    }
    if (exists) {
        std::string filename, backup;
        CDBEnv* env = GetWalletEnv(path, filename);
        if (env->Verify(filename, nullptr, backup) != CDBEnv::VERIFY_OK) {
            error = "The wallet file is damaged. Nothing was changed. Try the salvage command on a copy of the file";
            return nullptr;
        }
    }
    std::unique_ptr<CWallet> wallet(new CWallet(name, CWalletDBWrapper::Create(path)));
    bool firstRun = false;
    const DBErrors result = wallet->LoadWallet(firstRun);
    if (result == DB_LOAD_OK)
        return wallet;
    if (result == DB_NONCRITICAL_ERROR) {
        fprintf(stderr, "Warning: all keys were read, but transactions or address book entries may be missing or wrong.\n");
        return wallet;
    }
    error = result == DB_CORRUPT ? "The wallet file is corrupted (try the salvage command on a copy)"
          : result == DB_TOO_NEW ? "The wallet file was written by a newer version"
          : result == DB_NEED_REWRITE ? "The wallet file needs to be rewritten: load it with the node once"
          : "The wallet file cannot be loaded";
    wallet->Flush(true);
    return nullptr;
}

static void Info(CWallet& wallet)
{
    LOCK(wallet.cs_wallet);
    const std::set<CKeyID> keys = wallet.GetKeys();
    int major = 0, minor = 0, patch = 0;
    DbEnv::version(&major, &minor, &patch);
    fprintf(stdout, "Wallet info\n===========\n");
    fprintf(stdout, "File: %s\n", fs::absolute(wallet.GetName(), GetWalletDir()).string().c_str());
    fprintf(stdout, "Format: Berkeley DB (opened with %d.%d.%d)\n", major, minor, patch);
    fprintf(stdout, "Wallet version: %d\n", wallet.GetVersion());
    fprintf(stdout, "Encrypted: %s\n", wallet.IsCrypted() ? "yes" : "no");
    fprintf(stdout, "HD (hd seed available): %s\n", wallet.IsHDEnabled() ? "yes" : "no");
    if (wallet.IsHDEnabled())
        fprintf(stdout, "HD seed id: %s\n", wallet.GetHDChain().seed_id.GetHex().c_str());
    fprintf(stdout, "Keys: %u\n", (unsigned int)keys.size());
    fprintf(stdout, "Key pool size: %u\n", wallet.GetKeyPoolSize());
    fprintf(stdout, "Transactions: %u\n", (unsigned int)wallet.mapWallet.size());
    fprintf(stdout, "Address book: %u\n", (unsigned int)wallet.mapAddressBook.size());
    fprintf(stdout, "Watch-only: %s\n", wallet.HaveWatchOnly() ? "yes" : "no");
    int64_t oldest = 0;
    for (const std::pair<CKeyID, CKeyMetadata>& meta : wallet.mapKeyMetadata) {
        if (meta.second.nCreateTime > 1 && (oldest == 0 || meta.second.nCreateTime < oldest))
            oldest = meta.second.nCreateTime;
    }
    if (oldest > 0)
        fprintf(stdout, "Oldest key: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", oldest).c_str());
}

static void Addresses(CWallet& wallet, bool allKeys)
{
    LOCK(wallet.cs_wallet);
    std::set<CTxDestination> listed;
    for (const std::pair<CTxDestination, CAddressBookData>& entry : wallet.mapAddressBook) {
        fprintf(stdout, "%s\t%s\t%s\n", EncodeDestination(entry.first).c_str(), entry.second.purpose.c_str(), entry.second.name.c_str());
        listed.insert(entry.first);
    }
    if (!allKeys)
        return;
    const std::set<CKeyID> keys = wallet.GetKeys();
    for (const CKeyID& keyid : keys) {
        const CTxDestination dest(keyid);
        if (listed.count(dest))
            continue;
        std::string path;
        std::map<CKeyID, CKeyMetadata>::const_iterator meta = wallet.mapKeyMetadata.find(keyid);
        if (meta != wallet.mapKeyMetadata.end())
            path = meta->second.hdKeypath;
        fprintf(stdout, "%s\tkey\t%s\n", EncodeDestination(dest).c_str(), path.c_str());
    }
}

static bool Create(CWallet& wallet, std::string& error)
{
    LOCK(wallet.cs_wallet);
    wallet.SetMinVersion(FEATURE_NO_DEFAULT_KEY);
    const CPubKey seed = wallet.GenerateNewSeed();
    if (!wallet.SetHDSeed(seed)) {
        error = "Storing the HD seed failed";
        return false;
    }
    if (!wallet.TopUpKeyPool()) {
        error = "Unable to generate the initial keys";
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();
    gArgs.ParseParameters(argc, argv);

    std::string command;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (!command.empty()) {
                fprintf(stderr, "Error: one command only (%s, %s)\n", command.c_str(), argv[i]);
                return EXIT_FAILURE;
            }
            command = argv[i];
        }
    }
    if (gArgs.IsArgSet("-?") || gArgs.IsArgSet("-h") || gArgs.IsArgSet("-help") || gArgs.IsArgSet("-version") || argc < 2) {
        Usage();
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (command != "info" && command != "addresses" && command != "create" && command != "salvage") {
        fprintf(stderr, "Error: give one of the commands info, addresses, create, salvage (see -help)\n");
        return EXIT_FAILURE;
    }
    if (!gArgs.IsArgSet("-wallet") || gArgs.GetArg("-wallet", "").empty()) {
        fprintf(stderr, "Error: -wallet=<wallet> is required\n");
        return EXIT_FAILURE;
    }

    // nothing is written to a debug.log, and nothing to the console but the answer
    fPrintToConsole = false;
    fPrintToDebugLog = false;

    try {
        SelectParams(ChainNameFromCommandLine());
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }

    // A path to a file: its folder is the wallet directory, its name the wallet.
    std::string name = gArgs.GetArg("-wallet", "");
    const fs::path given(name);
    if (given.has_parent_path() && !fs::is_directory(given)) {
        const fs::path folder = fs::absolute(given).parent_path();
        if (!fs::is_directory(folder)) {
            fprintf(stderr, "Error: there is no folder %s\n", folder.string().c_str());
            return EXIT_FAILURE;
        }
        gArgs.ForceSetArg("-walletdir", folder.string());
        name = given.filename().string();
    } else if (!gArgs.IsArgSet("-walletdir") && !fs::is_directory(GetDataDir(false))) {
        fprintf(stderr, "Error: the data directory %s does not exist; give the wallet file as a path\n", GetDataDir(false).string().c_str());
        return EXIT_FAILURE;
    }

    RandomInit();
    ECC_Start();
    std::unique_ptr<ECCVerifyHandle> verifyHandle(new ECCVerifyHandle());
    int result = EXIT_FAILURE;
    std::string error;

    if (command == "salvage") {
        std::string warning;
        const fs::path path = fs::absolute(name, GetWalletDir());
        if (!fs::exists(path)) {
            error = "There is no wallet file " + path.string();
        } else if (CWallet::Verify(name, true, error, warning)) {
            fprintf(stdout, "The keys that could be read are in %s; the file as it was is kept next to it as %s.<time>.bak\n", path.string().c_str(), name.c_str());
            if (!warning.empty())
                fprintf(stdout, "%s\n", warning.c_str());
            result = EXIT_SUCCESS;
        }
        // make the new file independent of the log files of this environment, then close it
        std::string filename;
        if (CDBEnv* env = GetWalletEnv(path, filename)) {
            if (result == EXIT_SUCCESS && fs::exists(path))
                env->CheckpointLSN(filename);
            env->Flush(true);
        }
    } else {
        std::unique_ptr<CWallet> wallet = OpenWallet(name, command == "create", error);
        if (wallet) {
            if (command == "info") {
                Info(*wallet);
                result = EXIT_SUCCESS;
            } else if (command == "addresses") {
                Addresses(*wallet, gArgs.GetBoolArg("-allkeys", false));
                result = EXIT_SUCCESS;
            } else if (Create(*wallet, error)) {
                Info(*wallet);
                result = EXIT_SUCCESS;
            }
            wallet->Flush(true);
        }
    }

    if (result != EXIT_SUCCESS)
        fprintf(stderr, "Error: %s\n", error.empty() ? "failed" : error.c_str());
    verifyHandle.reset();
    ECC_Stop();
    return result;
}
