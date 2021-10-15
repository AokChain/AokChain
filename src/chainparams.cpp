// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "arith_uint256.h"

#include <assert.h>

#include "chainparamsseeds.h"

void GenesisGenerator(CBlock genesis) {
    printf("Searching for genesis block...\n");

    uint256 hash;
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(genesis.nBits, &fNegative, &fOverflow);

    while(true)
    {
        hash = genesis.GetBlockHash();
        if (UintToArith256(hash) <= bnTarget)
            break;
        if ((genesis.nNonce & 0xFFF) == 0)
        {
            printf("nonce %08X: hash = %s (target = %s)\n", genesis.nNonce, hash.ToString().c_str(), bnTarget.ToString().c_str());
        }
        ++genesis.nNonce;
        if (genesis.nNonce == 0)
        {
            printf("NONCE WRAPPED, incrementing time\n");
            ++genesis.nTime;
        }
    }

    printf("block.nNonce = %u \n", genesis.nNonce);
    printf("block.GetHash = %s\n", genesis.GetBlockHash().ToString().c_str());
    printf("block.MerkleRoot = %s \n", genesis.hashMerkleRoot.ToString().c_str());
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;
    txNew.nTime = nTime;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(const char* pszTimestamp, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

void CChainParams::TurnOffSegwit() {
	consensus.nSegwitEnabled = false;
}

void CChainParams::TurnOffCSV() {
	consensus.nCSVEnabled = false;
}

void CChainParams::TurnOffBIP34() {
	consensus.nBIP34Enabled = false;
}

void CChainParams::TurnOffBIP65() {
	consensus.nBIP65Enabled = false;
}

void CChainParams::TurnOffBIP66() {
	consensus.nBIP66Enabled = false;
}

bool CChainParams::BIP34() {
	return consensus.nBIP34Enabled;
}

bool CChainParams::BIP65() {
	return consensus.nBIP34Enabled;
}

bool CChainParams::BIP66() {
	return consensus.nBIP34Enabled;
}

bool CChainParams::CSVEnabled() const{
	return consensus.nCSVEnabled;
}


/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 525960;
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true;
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = false;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("000000000000ffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 16 * 60; // 16 mins
        consensus.nTargetSpacing = 64;
        consensus.nRuleChangeActivationThreshold = 1814; // Approx 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nTargetTimespan / nTargetSpacing
        consensus.fPowNoRetargeting = false;
        consensus.fPosNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000331d1ab12e374d430be");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x6a23569de30211b3b633d0ba3fb6fb8e64499b9a9f3d4d350ef077f25c32583b");

        // Proof-of-Stake
        consensus.nLastPOWBlock = 1440;
        consensus.nStakeTimestampMask = 0xf; // 15

        // Deployments
        consensus.nTokensDeploymentHeight = 159500;
        consensus.nTokensP2SHDeploymentHeight = 450000;
        consensus.nTokensIPFSDeploymentHeight = 622000;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x3b;
        pchMessageStart[1] = 0xee;
        pchMessageStart[2] = 0xe0;
        pchMessageStart[3] = 0x02;
        nDefaultPort = 33441;
        nPruneAfterHeight = 100000;

        const char* pszTimestamp = "Trump Threatens WHO With Permanent Cutoff of U.S. Funds | May 19, 2020 Bloomberg";

        genesis = CreateGenesisBlock(pszTimestamp, 1589879227, 798, 0x1f3fffff, 1, 0);
        consensus.hashGenesisBlock = genesis.GetBlockHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000c513d39b0e3657b7b0ca79b2d4b9b18c1f03dacb394acf0e71f49064b3306"));
        assert(genesis.hashMerkleRoot == uint256S("0xe212c2a291da3a980cbddfbd663d58f2d4d1faa756deba27f3b77bf8df66df7c"));

        vSeeds.emplace_back("dns.aok.network", false);

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,46);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,108);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,255);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = true;

        checkpointData = (CCheckpointData) {
            {
                { 10, uint256S("0x000e2bc4510332de2bc53002d5740f1c88b5147804a07af97c2c33873956275d")},
                { 1440, uint256S("0xc0ada4a3eb592a12cd3a733d464ea01ca32a8e4801f0d35e0376c94173c55bba")},
                { 100000, uint256S("0xcd8d0280e845e99cc24ba5592583a65b266cbc1b8c75ab539c457d7ff7b5664a")},
                { 150000, uint256S("0x6e833229a550ade91d52edb85c3f3bedee1548cee17acee8612cfc7a44a41421")},
                { 159500, uint256S("0x31723a8b8c68ef66ba3b55f823e65bb3f1346b7b07a6a34f6269fe3c3c0f1ca5")},
                { 450000, uint256S("0xa5e0a4b29875100cfdc95eb83ae31ead1952c5f581dd88e33fdd46af347c3d8b")},
                { 500000, uint256S("0xf0ab563c346b480257a79fcc21335160397557c4ef497a5a3cdcd785d8f7b750")},
                { 550000, uint256S("0x6a23569de30211b3b633d0ba3fb6fb8e64499b9a9f3d4d350ef077f25c32583b")},
                { 600000, uint256S("0x5a7500eea9a150530f51bd7cf75aff4224b0a42a7aed50193099841d7c1d47fe")},
            }
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 5a7500eea9a150530f51bd7cf75aff4224b0a42a7aed50193099841d7c1d47fe
            1630295312,         // * UNIX timestamp of last known number of transactions
            2776389,            // * total number of transactions between genesis and that timestamp
                                //   (the tx=... number in the SetBestChain debug.log lines)
            0.03212183168029697 // * estimated number of transactions per second after that timestamp
        };

        /** TOKENS START **/
        // Fee Amounts
        nFeeAmountRoot = 10000 * COIN;
        nFeeAmountReissue = 10000 * COIN;
        nFeeAmountUnique = 100 * COIN;
        nFeeAmountSub = 100 * COIN;
        nFeeAmountUsername = 1 * COIN;

        // Fee Addresse
        strTokenFeeAddress = "KdZFAFeKV8C5JzdEdgcPYDZku92KkD3fHy";

        nMaxReorganizationDepth = 500; // Around 8 hours
        /** TOKENS END **/
    }
};

/**
 * Testnet (v6)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 525960;
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true;
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = false;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("000000000000ffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 16 * 60; // 16 mins
        consensus.nTargetSpacing = 64;
        consensus.nRuleChangeActivationThreshold = 1814; // Approx 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nTargetTimespan / nTargetSpacing
        consensus.fPowNoRetargeting = true;
        consensus.fPosNoRetargeting = true;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // Proof-of-Stake
        consensus.nLastPOWBlock = 1440 * 1000;
        consensus.nStakeTimestampMask = 0xf; // 15

        // Deployments
        consensus.nTokensDeploymentHeight = 10;
        consensus.nTokensP2SHDeploymentHeight = 15;
        consensus.nTokensIPFSDeploymentHeight = 20;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x43;
        pchMessageStart[1] = 0x4a;
        pchMessageStart[2] = 0x51;
        pchMessageStart[3] = 0x03;
        nDefaultPort = 44551;
        nPruneAfterHeight = 100000;

        const char* pszTimestamp = "Taliban Say International Flights From Kabul to Start Soon | Sep 6, 2021 Bloomberg";

        genesis = CreateGenesisBlock(pszTimestamp, 1630926558, 1560, 0x1f3fffff, 1, 0);
        consensus.hashGenesisBlock = genesis.GetBlockHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0024a6a18447e725d12da868d6d65b8046e1b53cfefc35354d4b2146dfad20d4"));
        assert(genesis.hashMerkleRoot == uint256S("0xb4eeb7c62174581104292ae662bc456b7c3b26ead4c8290d0c27cd2cb29395e0"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,66);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,128);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,143);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = true;

        checkpointData = (CCheckpointData) {
            {

            }
        };

        chainTxData = ChainTxData{
            // Update as we know more about the contents of the AokChain chain
            // Stats as of 000000000000a72545994ce72b25042ea63707fca169ca4deb7f9dab4f1b1798 window size 43200
            0, // * UNIX timestamp of last known number of transactions
            0,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0         // * estimated number of transactions per second after that timestamp
        };

        /** TOKENS START **/
        // Fee Amounts
        nFeeAmountRoot = 10000 * COIN;
        nFeeAmountReissue = 10000 * COIN;
        nFeeAmountUnique = 100 * COIN;
        nFeeAmountSub = 100 * COIN;
        nFeeAmountUsername = 1 * COIN;

        // Fee Addresse
        strTokenFeeAddress = "TufvYmro3vSfDerUAjvjXMjYqUsFw6iWS7";

        nMaxReorganizationDepth = 500; // 60 at 1 minute block timespan is +/- 60 minutes.
        /** TOKENS END **/
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 525960;
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true;
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = true;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 2016 * 60; // 1.4 days
        consensus.nTargetSpacing = 1 * 60;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.fPowNoRetargeting = true;
        consensus.fPosNoRetargeting = true;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        consensus.nLastPOWBlock = 1440 * 1000;
        consensus.nTokensDeploymentHeight = 10;
        consensus.nTokensP2SHDeploymentHeight = 10;

        pchMessageStart[0] = 0x43;
        pchMessageStart[1] = 0x52;
        pchMessageStart[2] = 0x4F;
        pchMessageStart[3] = 0x57;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;

        const char* pszTimestamp = "Study: Our Sun is Less Active than Other Solar-Type Stars | May 1, 2020 Sci News";

        genesis = CreateGenesisBlock(pszTimestamp, 1296688602, 4, 0x207fffff, 1, 0);
        consensus.hashGenesisBlock = genesis.GetBlockHash();
        assert(consensus.hashGenesisBlock == uint256S("0x79f23c0228fcd0c7c1e4f5c32ad3f4f390165a0f618659d627186719212a7e64"));
        assert(genesis.hashMerkleRoot == uint256S("0xe0127f2f72b3486caf1db821e0c54b4643be7bc0037c6c456d2350150cd61b7c"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = (CCheckpointData) {
            {
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};


        /** TOKENS START **/
        // Fee Amounts

        nFeeAmountRoot = 10000 * COIN;
        nFeeAmountReissue = 10000 * COIN;
        nFeeAmountUnique = 100 * COIN;
        nFeeAmountSub = 100 * COIN;
        nFeeAmountUsername = 1 * COIN;

        // Fee Addresse
        strTokenFeeAddress = "n3XzBy9gndByXLeAgz5qG5xJkNNw31ULXy";

        nMaxReorganizationDepth = 60; // 60 at 1 minute block timespan is +/- 60 minutes.
        /** TOKENS END **/
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

const CChainParams &CParams() {
    return Params();
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}

void TurnOffSegwit(){
	globalChainParams->TurnOffSegwit();
}

void TurnOffCSV() {
	globalChainParams->TurnOffCSV();
}

void TurnOffBIP34() {
	globalChainParams->TurnOffBIP34();
}

void TurnOffBIP65() {
	globalChainParams->TurnOffBIP65();
}

void TurnOffBIP66() {
	globalChainParams->TurnOffBIP66();
}
