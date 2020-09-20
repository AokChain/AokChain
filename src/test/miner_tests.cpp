// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "validation.h"
#include "miner.h"
#include "policy/policy.h"
#include "pubkey.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"

#include "test/test_aokchain.h"

#include <memory>

#include <boost/test/unit_test.hpp>

#include "util.h"

BOOST_FIXTURE_TEST_SUITE(miner_tests, TestingSetup)

    static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

    static BlockAssembler AssemblerForTest(const CChainParams &params)
    {
        BlockAssembler::Options options;

        options.nBlockMaxWeight = MAX_BLOCK_WEIGHT;
        options.blockMinFeeRate = blockMinFeeRate;
        return BlockAssembler(params, options);
    }

    static
    struct
    {
        unsigned char extranonce;
        unsigned int nonce;
    } blockinfo[] = {
            {3, 0x1147b7d},  {1, 0x115c9f0},  {6, 0x11c6644},  {1, 0x1377d45},
            {1, 0x1050f1c},  {6, 0x1092cdb},  {5, 0x1201fdf},  {4, 0x1081b87},
            {1, 0x10ecd8e},  {5, 0x1124186},  {2, 0x16556d5},  {1, 0x1445631},
            {4, 0x13b8cb3},  {6, 0x13bfe16},  {3, 0x154cec8},  {6, 0x11e277f},
            {1, 0x11152bb},  {4, 0x11a76cf},  {5, 0x1074661},  {3, 0x1166c7c},
            {6, 0x144ee88},  {6, 0x1058371},  {2, 0x13ba092},  {6, 0x121b632},
            {6, 0x12ba776},  {4, 0x1a420e9},  {1, 0x1614e2e},  {5, 0x12906bd},
            {4, 0x19ae0cd},  {1, 0x125a839},  {4, 0x10a6629},  {1, 0x115bd94},
            {2, 0x1166e27},  {5, 0x141e5c6},  {6, 0x120891c},  {3, 0x11e4885},
            {6, 0x124e64a},  {3, 0x159ea34},  {5, 0x1253029},  {3, 0x131b888},
            {4, 0x10f551c},  {4, 0x14b7246},  {3, 0x14d063f},  {4, 0x14330f3},
            {2, 0x10888a7},  {1, 0x121551c},  {5, 0x15ef2f0},  {1, 0x107aaf1},
            {3, 0x10d202d},  {5, 0x143fe76},  {3, 0x1325a0d},  {2, 0x11359eb},
            {1, 0x11c50c4},  {5, 0x10d9873},  {3, 0x164f8e0c}, {2, 0x3bd9ac4e},
            {5, 0x1037a74},  {6, 0x3ccf649b}, {2, 0x4e25d77e}, {1, 0x154c529c},
            {3, 0x46f58432}, {4, 0x3ffdf30e}, {2, 0x4f0bac61}, {1, 0x5c70a786},
            {2, 0x42e087c9}, {4, 0x6555bc31}, {1, 0x5621e559}, {2, 0x429289e},
            {3, 0x26957429}, {2, 0x2681e907}, {2, 0xc3d2520},  {2, 0x2489ef68},
            {4, 0x1758ab44}, {5, 0x62f4db26}, {4, 0x4509de4e}, {5, 0x3ac81dd8},
            {5, 0x176905d1}, {3, 0x41e699ee}, {4, 0x583cdb33}, {3, 0x583b8153},
            {3, 0x3df0e83d}, {2, 0x56499554}, {4, 0x606f9c0d}, {3, 0x3cd17b27},
            {1, 0x4b3f7c80}, {5, 0x2ce8a5d9}, {2, 0x5010aa75}, {2, 0x21ef0c5},
            {2, 0x2195d8b7}, {2, 0x114003f1}, {3, 0x19964e33}, {1, 0x4e102751},
            {3, 0x2cb19b5e}, {6, 0x39c86a0d}, {1, 0x6072547a}, {5, 0x40da07ce},
            {5, 0x2231a2f},  {2, 0x206e39a9}, {1, 0x2d991bfa}, {1, 0x30d6ed20},
            {4, 0x3bd3d8b5}, {3, 0x1b7c2855}, {1, 0x522e52d6}, {2, 0x1875a243},
            {6, 0x6576cb49}, {5, 0x247cd4e8}, {1, 0x4d33241f}, {3, 0x3bf5b0da},
            {1, 0x28bfe02a}, {1, 0x4a09177f}, {2, 0x35bbf3ca}, {1, 0x39c5e746},
            {4, 0x3d41628b}, {4, 0x38cab9ee}, {6, 0x57456063}, {4, 0x33ba1d4f},
            {3, 0x1d640bc8}, {3, 0x2fa9314f}, {5, 0x81df701},  {5, 0x62c533c},
            {4, 0x548f5a2d}, {4, 0x55e2972},  {4, 0x25bc97a1}, {4, 0x35297e44},
            {4, 0x3a463b3b}, {5, 0x31af86fb}, {6, 0x23922425}, {2, 0xb2cd8fc},
            {4, 0x1fe51f98}, {5, 0x55235249}, {2, 0xe53df69},  {5, 0x1a8d15dc},
            {1, 0x32b2291b}, {5, 0x3ad9d3ef}, {3, 0x39d56c3e}, {3, 0x65715d6c},
            {6, 0x32ab99eb}, {5, 0x1f6aa3e0}, {6, 0x1a862c41}, {2, 0x5d3a0f85},
            {1, 0x342c70e},  {6, 0x1eb1e6a9}, {4, 0x43f47ea4}, {1, 0x4c4f81c8},
            {1, 0x3bee57c5}, {4, 0x119016c1}, {1, 0x55303b0a}, {1, 0x2b931a62},
            {3, 0x39c3c451}, {6, 0x4e5ccf29}, {4, 0x41e5dd33}, {6, 0x11b39159},
            {2, 0x12c7b4f},  {6, 0x2171be30}, {3, 0x1347762c}, {1, 0x5e6c1c37},
            {3, 0x444ffebc}, {5, 0x3d06d2d8}, {3, 0x1977b94a}, {4, 0x48fcf44a},
            {4, 0x17a62f24}, {2, 0x6464598f}, {5, 0x657763dc}, {1, 0x1e6e1805},
            {5, 0x3f200c6a}, {4, 0x532060fd}, {2, 0x37c1db66}, {6, 0x4573db3f},
            {3, 0x104d20ce}, {5, 0x1b608c2a}, {1, 0x39cd05dc}, {2, 0x56419aa2},
            {2, 0x584f47db}, {5, 0x321ed5e},  {4, 0x14521404}, {5, 0x33ffb969},
            {4, 0x2681e001}, {5, 0x552d6669}, {6, 0x65627917}, {3, 0xc43da},
            {2, 0x227471b5}, {3, 0x2888fa1f}, {2, 0x48629531}, {1, 0x51f9f4a},
            {3, 0x5d4b1f00}, {2, 0x595a42be}, {4, 0x31cd88f0}, {4, 0x4a0e493c},
            {4, 0x421534d4}, {4, 0xd5f42ce},  {2, 0x215c5e5},  {6, 0xb44f80f},
            {2, 0x42146c2a}, {2, 0x2786c0b7}, {2, 0x2bb24ff1}, {3, 0x4f57f70d},
            {5, 0x5d4ca8a0}, {5, 0x195db0f},  {4, 0x481be297}, {2, 0x5113ba88},
            {2, 0x5f614de3}, {2, 0x4a0816de}, {1, 0x2126b0b},  {5, 0x2cf20fe8},
            {5, 0x3ffcdab3}, {6, 0x4b049bad}, {5, 0x2caf61fb}, {2, 0x553a6500},
            {2, 0x572ea366}, {4, 0x39fa68d6}, {3, 0xe7176fa},  {6, 0x42fb9000},
            {6, 0x626282a1}, {4, 0xb43bd7e},  {5, 0x32b7ad00}, {6, 0x63e0bc9c},
            {1, 0xa4d7c32},  {5, 0x20acffea}, {1, 0x1a6e9a93}, {5, 0x27859dd9},
            {6, 0x1ead7df8}, {6, 0x2095b25},  {5, 0x30a83c6b}, {1, 0x1c6f809a},
            {1, 0x43099992}, {3, 0x48a12952}, {3, 0xc3afe97},  {1, 0x55236689},
            {3, 0x5011747e}, {3, 0x18c8e0c0}, {5, 0x2ff14559}, {1, 0x82e1010},
            {2, 0xa3ee349},  {5, 0x1b84097d}, {3, 0x3f09452e}, {6, 0x2fd4e4e3},
            {5, 0x3bd75aef}, {6, 0x21935d51}, {1, 0x2a05fafe}, {5, 0x1dcd7965},
            {1, 0x61481baf}, {1, 0x2cb6ffd0}, {4, 0x51112dca}, {3, 0x1246de00},
            {2, 0x4c190fab}, {4, 0x4d2b63ea}, {6, 0x460179d8}, {2, 0x33d360b1},
            {4, 0xc2e4aad},  {1, 0x4b2691ef}, {2, 0x4d2b3347}, {3, 0x43492a03},
            {2, 0x4c536fd2}, {1, 0x1348d148}, {3, 0x5e3f96de}, {4, 0x146ba9b8},
            {5, 0x5371a23c}, {2, 0x57350d63}, {5, 0x42e6d8df}, {1, 0x493397bf},
            {5, 0x30be40b0}, {3, 0x5631f839}, {3, 0x487cd05},  {1, 0x1037946f},
            {5, 0x553826ac}, {3, 0x1d9575e3}, {6, 0x4f144a11}, {4, 0x16557a4e},
            {5, 0x33b0179a}, {5, 0x31c8cfd4}, {5, 0x2ea631c9}, {3, 0x31761c9},
            {6, 0xe328cd9},  {1, 0x2a955362}, {4, 0x448e8dba}, {5, 0x185cffb5},
            {5, 0x25d6a30f}, {6, 0x542f1f6d}, {1, 0x156bd825}, {4, 0x30a9c82a},
            {2, 0xb287821},  {6, 0x1765a117}, {6, 0x5b3bc96a}, {2, 0x38c18d0b},
            {5, 0x21d51ca7}, {1, 0x5734f5dc}, {6, 0x33590939}, {5, 0x4a210ea9},
            {3, 0x269df7bb}, {5, 0x1f8bfc07}, {6, 0x343f3b3b}, {3, 0xfe7f2},
            {4, 0x4f0c9f21}, {5, 0x2078a181}, {1, 0xb2e77a1},  {3, 0x34fefbaf},
            {1, 0x17513bab}, {5, 0x35c0cf53}, {4, 0x41e50f99}, {2, 0x55470a99},
            {6, 0x429c825f}, {6, 0x19383d3e}, {1, 0x48391fbb}, {3, 0x3be90129},
            {2, 0x54201ea5}, {6, 0x289a33f7}, {5, 0x237a03fd}, {6, 0x460a6395},
            {2, 0xd2c3ecd},  {5, 0x42e1fe6a}, {4, 0x3dee9a10}, {1, 0x1cb6c2ef},
            {6, 0x24a577d2}, {5, 0x43da6b9},  {4, 0x4bff9311}, {1, 0x154c0b03},
            {5, 0x136179ed}, {4, 0x1fafecf0}, {4, 0x33b453bc}, {2, 0x113be200},
            {4, 0x2e3511a6}, {4, 0x60618f0d}, {2, 0x48044c09}, {2, 0x30ac1a90},
            {1, 0x64578dfb}, {3, 0x63fcccab}, {2, 0x1b6a6cb2}, {6, 0x3cce8604},
            {3, 0x2da43247}, {6, 0x1c70ab3b}, {2, 0xf38a3c0},  {3, 0x5c917cbe},
            {1, 0x541bae3e}, {4, 0x112e7ec},  {5, 0x440f08ef}, {5, 0x3ed599e7},
            {6, 0x4a135a6a}, {1, 0x6097d9},   {2, 0x57379c00}, {3, 0x6518eec7},
            {5, 0x52570c9b}, {4, 0x4a605a0c}, {3, 0x511a59df}, {2, 0x1044545},
            {4, 0x49164b13}, {5, 0x31d9f3c8}, {2, 0x22a65438}, {3, 0x1244df94},
            {6, 0xd6b4ae2},  {6, 0x16c76fc6}, {1, 0x113d9ca2}, {3, 0x5219e5bd},
            {4, 0x5f5be7e5}, {2, 0x4f4e106d}, {3, 0x5253d40a}, {5, 0x46f8af46},
            {1, 0x32f32dc},  {2, 0x55256c2c}, {2, 0x185bafc6}, {5, 0x268525fd},
            {5, 0x30c76cb9}, {5, 0x2f045e40}, {5, 0x5857479e}, {1, 0x33d5a215},
            {1, 0x11610502}, {6, 0x2fd7145c}, {3, 0x442b9891}, {5, 0x1da4de52},
            {1, 0x1693e77a}, {4, 0x3aecd3c8}, {3, 0x2ac07f4e}, {5, 0x3ce13f5c},
            {6, 0x21730c2},  {1, 0x5d61d76f}, {5, 0x6579b666}, {3, 0x60bf9944},
            {3, 0x35c370ae}, {4, 0xb7d2d99},  {6, 0x4c1ef701}, {3, 0x39c59e0f},
            {1, 0x531a9400}, {2, 0x3be44044}, {2, 0x5b335555}, {1, 0xc66ac80},
            {2, 0x103d444e}, {1, 0x445f840d}, {6, 0x30b41dbc}, {5, 0x531fb3f0},
            {2, 0x1763bbff}, {6, 0xa749665},  {1, 0x58346cdc}, {1, 0x2fb0e8d8},
            {3, 0x628acf5},  {2, 0x2b99fdd0}, {6, 0x196b3765}, {1, 0x36de5152},
            {1, 0x288aac81}, {5, 0x2ea74688}, {6, 0x10c15368}, {4, 0x553a5052},
            {4, 0x113e1a14}, {6, 0xf59df78},  {1, 0x44ea272e}, {6, 0x5254aa32},
            {1, 0xf448ca0},  {5, 0x2bcac04b}, {6, 0x4a04651e}, {6, 0xb4be03a},
            {4, 0x38ef9569}, {5, 0x1e7a1130}, {2, 0x55352321}, {2, 0x6261af8b},
            {4, 0x490d3f21}, {4, 0x1d99468d}, {6, 0x1f6ad569}, {3, 0x5d8df13d},
            {5, 0x6560b11f}, {3, 0x56675bc0}, {5, 0x5c45c62a}, {2, 0x239b329c},
            {1, 0x38cf1221}, {4, 0x38ce517e}, {2, 0x234eeec},  {2, 0x233d6f74},
            {4, 0x45f03db2}, {6, 0x4901d57b}, {3, 0x32cd4a47}, {6, 0x5e628617},
            {1, 0x3e882544}, {1, 0x2fb7c17e}, {3, 0x3af1461c}, {5, 0x31dc024b},
            {3, 0x23b1d0fb}, {6, 0x637fa04a}, {2, 0x36d43eb2}, {6, 0x46ee75b0},
            {5, 0x5c448fe1}, {6, 0x5d6c8c8e}, {3, 0x5645015c}, {1, 0x3ad2d9ae},
            {1, 0x125add93}, {3, 0x64686cc2}, {3, 0x21f43601}, {3, 0x28bcc0b0},
            {1, 0x3bd174b3}, {1, 0x3de01db1}, {4, 0x2385f3ca}, {4, 0x55371d14},
    };

    CBlockIndex CreateBlockIndex(int nHeight)
    {
        CBlockIndex index;
        index.nHeight = nHeight;
        index.pprev = chainActive.Tip();
        return index;
    }

    bool TestSequenceLocks(const CTransaction &tx, int flags)
    {
        LOCK(mempool.cs);
        return CheckSequenceLocks(tx, flags);
    }

    // Test suite for ancestor feerate transaction selection.
    // Implemented as an additional function, rather than a separate test case,
    // to allow reusing the blockchain created in CreateNewBlock_validity.
    void TestPackageSelection(const CChainParams &chainparams, CScript scriptPubKey, std::vector<CTransactionRef> &txFirst)
    {
        // Test the ancestor feerate transaction selection.
        TestMemPoolEntryHelper entry;

        // Test that a medium fee transaction will be selected after a higher fee
        // rate package with a low fee rate parent.
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 5000000000LL - 1000;
        // This tx has a low fee: 1000 satoshis
        uint256 hashParentTx = tx.GetHash(); // save this txid for later use
        mempool.addUnchecked(hashParentTx, entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

        // This tx has a medium fee: 10000 satoshis
        tx.vin[0].prevout.hash = txFirst[1]->GetHash();
        tx.vout[0].nValue = 5000000000LL - 10000;
        uint256 hashMediumFeeTx = tx.GetHash();
        mempool.addUnchecked(hashMediumFeeTx, entry.Fee(10000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

        // This tx has a high fee, but depends on the first transaction
        tx.vin[0].prevout.hash = hashParentTx;
        tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 50k satoshi fee
        uint256 hashHighFeeTx = tx.GetHash();
        mempool.addUnchecked(hashHighFeeTx, entry.Fee(50000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));

        std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashParentTx);
        BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashHighFeeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashMediumFeeTx);

        // Test that a package below the block min tx fee doesn't get included
        tx.vin[0].prevout.hash = hashHighFeeTx;
        tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 0 fee
        uint256 hashFreeTx = tx.GetHash();
        mempool.addUnchecked(hashFreeTx, entry.Fee(0).FromTx(tx));
        size_t freeTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

        // Calculate a fee on child transaction that will put the package just
        // below the block min tx fee (assuming 1 child tx of the same size).
        CAmount feeToUse = blockMinFeeRate.GetFee(2 * freeTxSize) - 1;

        tx.vin[0].prevout.hash = hashFreeTx;
        tx.vout[0].nValue = 5000000000LL - 1000 - 50000 - feeToUse;
        uint256 hashLowFeeTx = tx.GetHash();
        mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse).FromTx(tx));
        pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
        // Verify that the free tx and the low fee tx didn't get selected
        for (size_t i = 0; i < pblocktemplate->block.vtx.size(); ++i)
        {
            BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx);
            BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx);
        }

        // Test that packages above the min relay fee do get included, even if one
        // of the transactions is below the min relay fee
        // Remove the low fee transaction and replace with a higher fee transaction
        mempool.removeRecursive(tx);
        tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
        hashLowFeeTx = tx.GetHash();
        mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse + 2).FromTx(tx));
        pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashLowFeeTx);

        // Test that transaction selection properly updates ancestor fee
        // calculations as ancestor transactions get included in a block.
        // Add a 0-fee transaction that has 2 outputs.
        tx.vin[0].prevout.hash = txFirst[2]->GetHash();
        tx.vout.resize(2);
        tx.vout[0].nValue = 5000000000LL - 100000000;
        tx.vout[1].nValue = 100000000; // 1AOK output
        uint256 hashFreeTx2 = tx.GetHash();
        mempool.addUnchecked(hashFreeTx2, entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

        // This tx can't be mined by itself
        tx.vin[0].prevout.hash = hashFreeTx2;
        tx.vout.resize(1);
        feeToUse = blockMinFeeRate.GetFee(freeTxSize);
        tx.vout[0].nValue = 5000000000LL - 100000000 - feeToUse;
        uint256 hashLowFeeTx2 = tx.GetHash();
        mempool.addUnchecked(hashLowFeeTx2, entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
        pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);

        // Verify that this tx isn't selected.
        for (size_t i = 0; i < pblocktemplate->block.vtx.size(); ++i)
        {
            BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx2);
            BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx2);
        }

        // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
        // as well.
        tx.vin[0].prevout.n = 1;
        tx.vout[0].nValue = 100000000 - 10000; // 10k satoshi fee
        mempool.addUnchecked(tx.GetHash(), entry.Fee(10000).FromTx(tx));
        pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHash() == hashLowFeeTx2);
    }

BOOST_AUTO_TEST_SUITE_END()
