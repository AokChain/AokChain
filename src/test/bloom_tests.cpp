// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bloom.h"

#include "base58.h"
#include "clientversion.h"
#include "key.h"
#include "merkleblock.h"
#include "primitives/block.h"
#include "random.h"
#include "script/standard.h"
#include "script/script.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "test/test_aokchain.h"

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bloom_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(bloom_create_insert_serialize_test)
    {
        BOOST_TEST_MESSAGE("Running Bloom Create Insert Serialize Test");

        CBloomFilter filter(3, 0.01, 0, BLOOM_UPDATE_ALL);

        filter.insert(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8"));
        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter doesn't contain just-inserted object!");
        // One bit different in first byte
        BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("19108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter contains something it shouldn't!");

        filter.insert(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee"));
        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee")), "Bloom filter doesn't contain just-inserted object (2)!");

        filter.insert(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5"));
        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5")), "Bloom filter doesn't contain just-inserted object (3)!");

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << filter;

        std::vector<unsigned char> vch = ParseHex("03614e9b050000000000000001");
        std::vector<char> expected(vch.size());

        for (unsigned int i = 0; i < vch.size(); i++)
            expected[i] = (char) vch[i];

        BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());

        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter doesn't contain just-inserted object!");
        filter.clear();
        BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter should be empty!");
    }

    BOOST_AUTO_TEST_CASE(bloom_create_insert_serialize_with_tweak_test)
    {
        BOOST_TEST_MESSAGE("Running Bloom Create Insert Serialize With Tweak Test");

        // Same test as bloom_create_insert_serialize, but we add a nTweak of 100
        CBloomFilter filter(3, 0.01, 2147483649UL, BLOOM_UPDATE_ALL);

        filter.insert(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8"));
        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter doesn't contain just-inserted object!");
        // One bit different in first byte
        BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("19108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter contains something it shouldn't!");

        filter.insert(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee"));
        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee")), "Bloom filter doesn't contain just-inserted object (2)!");

        filter.insert(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5"));
        BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5")), "Bloom filter doesn't contain just-inserted object (3)!");

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << filter;

        std::vector<unsigned char> vch = ParseHex("03ce4299050000000100008001");
        std::vector<char> expected(vch.size());

        for (unsigned int i = 0; i < vch.size(); i++)
            expected[i] = (char) vch[i];

        BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());
    }

    BOOST_AUTO_TEST_CASE(bloom_create_insert_key_test)
    {
        BOOST_TEST_MESSAGE("Running Bloom Create Insert Key Test");

        std::string strSecret = std::string("9aYZ6EAnoNZ5r8WGXS6LB8omyWyJ9NS8Wu8oHDv2ocvhaS8XAYN");
        CAokChainSecret vchSecret;
        BOOST_CHECK(vchSecret.SetString(strSecret));

        CKey key = vchSecret.GetKey();
        CPubKey pubkey = key.GetPubKey();
        std::vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());

        CBloomFilter filter(2, 0.001, 0, BLOOM_UPDATE_ALL);
        filter.insert(vchPubKey);
        uint160 hash = pubkey.GetID();
        filter.insert(std::vector<unsigned char>(hash.begin(), hash.end()));

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << filter;

        std::vector<unsigned char> vch = ParseHex("038fc16b080000000000000001");
        std::vector<char> expected(vch.size());

        for (unsigned int i = 0; i < vch.size(); i++)
            expected[i] = (char) vch[i];

        BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());
    }

    // The tests below build their transactions and blocks (test_aokchain.h). The original
    // ones parsed serialized Bitcoin transactions and blocks, which are not in the
    // AokChain format.

    static std::vector<unsigned char> Bytes(const uint160& id)
    {
        return std::vector<unsigned char>(id.begin(), id.end());
    }

    static std::vector<unsigned char> Bytes(const uint256& hash)
    {
        return std::vector<unsigned char>(hash.begin(), hash.end());
    }

    BOOST_AUTO_TEST_CASE(bloom_match_test)
    {
        BOOST_TEST_MESSAGE("Running Bloom Match Test");

        const CPubKey pubkeyIn = TestKey(11).GetPubKey();
        const CPubKey pubkeyOut = TestKey(12).GetPubKey();
        const std::vector<unsigned char> sig(71, 0x30);
        uint256 prevHash;
        memset(prevHash.begin(), 0xab, 32);

        // tx pays to the address of pubkeyOut; spendingTx spends that output
        const CTransaction tx(TestTx(COutPoint(prevHash, 0), CScript() << sig << ToByteVector(pubkeyIn), GetScriptForDestination(pubkeyOut.GetID())));
        const CTransaction spendingTx(TestTx(COutPoint(tx.GetHash(), 0), CScript() << sig << ToByteVector(pubkeyOut), GetScriptForDestination(pubkeyIn.GetID())));

        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(tx.GetHash());
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match tx hash");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        // byte-reversed tx hash
        filter.insert(ParseHex(HexStr(tx.GetHash().begin(), tx.GetHash().end())));
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match manually serialized tx hash");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(sig);
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match input signature");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(ToByteVector(pubkeyIn));
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match input pub key");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(Bytes(pubkeyOut.GetID()));
        BOOST_CHECK_MESSAGE(!filter.contains(COutPoint(tx.GetHash(), 0)), "The outpoint is in the filter before the match");
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match output address");
        BOOST_CHECK_MESSAGE(filter.contains(COutPoint(tx.GetHash(), 0)), "Simple Bloom filter didn't add the matched output");
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(spendingTx), "Simple Bloom filter didn't add output");

        // without updates, the transaction that spends the matched output is found by its
        // own data only (here: the public key in its input is not in the filter)
        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_NONE);
        filter.insert(tx.GetHash());
        BOOST_CHECK(filter.IsRelevantAndUpdate(tx));
        BOOST_CHECK_MESSAGE(!filter.contains(COutPoint(tx.GetHash(), 0)), "BLOOM_UPDATE_NONE added an output");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(COutPoint(prevHash, 0));
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match COutPoint");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        COutPoint prevOutPoint(prevHash, 0);
        {
            std::vector<unsigned char> data(32 + sizeof(unsigned int));
            memcpy(data.data(), prevHash.begin(), 32);
            memcpy(data.data() + 32, &prevOutPoint.n, sizeof(unsigned int));
            filter.insert(data);
        }
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match manually serialized COutPoint");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(uint256S("00000009e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436"));
        BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched random tx hash");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(Bytes(TestKey(13).GetPubKey().GetID()));
        BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched random address");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(COutPoint(prevHash, 1));
        BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched COutPoint for an output we didn't care about");

        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(COutPoint(tx.GetHash(), 0));
        BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched the COutPoint of an output of the transaction itself");
        BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(spendingTx), "Simple Bloom filter didn't match the spent COutPoint");
    }

    static void CheckMatches(const CBlock& block, CMerkleBlock& merkleBlock, const std::vector<unsigned int>& expected)
    {
        BOOST_CHECK(merkleBlock.header.GetBlockHash() == block.GetBlockHash());
        BOOST_REQUIRE_EQUAL(merkleBlock.vMatchedTxn.size(), expected.size());
        for (size_t i = 0; i < expected.size(); i++) {
            BOOST_CHECK_EQUAL(merkleBlock.vMatchedTxn[i].first, expected[i]);
            BOOST_CHECK(merkleBlock.vMatchedTxn[i].second == block.vtx[expected[i]]->GetHash());
        }

        // the partial merkle tree proves exactly these transactions
        std::vector<uint256> vMatched;
        std::vector<unsigned int> vIndex;
        BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
        BOOST_REQUIRE_EQUAL(vMatched.size(), expected.size());
        for (size_t i = 0; i < expected.size(); i++) {
            BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);
            BOOST_CHECK_EQUAL(vIndex[i], expected[i]);
        }
    }

    BOOST_AUTO_TEST_CASE(merkle_block_1_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 1 Test");

        CBlock block = TestBlock9();
        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        // Match the last transaction
        filter.insert(block.vtx[8]->GetHash());

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {8});

        // Also match the 8th transaction
        filter.insert(block.vtx[7]->GetHash());
        merkleBlock = CMerkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {7, 8});
    }

    // A block where transaction 2 spends the output of transaction 1, which pays to the
    // address of TestKey(21), and transaction 3 is unrelated.
    static CBlock ChainedBlock()
    {
        const CPubKey pubkey = TestKey(21).GetPubKey();
        const std::vector<unsigned char> sig(71, 0x30);
        uint256 prevHash;
        memset(prevHash.begin(), 0xcd, 32);
        std::vector<CMutableTransaction> txs;
        txs.push_back(TestTx(COutPoint(prevHash, 0), CScript() << sig << ToByteVector(TestKey(22).GetPubKey()), GetScriptForDestination(pubkey.GetID())));
        // (the input has the signature only, as for a pay-to-pubkey output: nothing in it but the outpoint leads to transaction 1)
        txs.push_back(TestTx(COutPoint(txs[0].GetHash(), 0), CScript() << sig, GetScriptForDestination(TestKey(23).GetPubKey().GetID())));
        txs.push_back(TestTx(COutPoint(prevHash, 1), CScript() << sig << ToByteVector(TestKey(24).GetPubKey()), GetScriptForDestination(TestKey(25).GetPubKey().GetID())));
        return TestBlock(txs);
    }

    BOOST_AUTO_TEST_CASE(merkle_block_2_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 2 Test");

        CBlock block = ChainedBlock();
        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        // Match the first transaction
        filter.insert(block.vtx[1]->GetHash());

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {1});

        // Match an output of transaction 1 by its address. This also matches
        // transaction 2, which spends that output: the filter adds the outpoint.
        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(Bytes(TestKey(21).GetPubKey().GetID()));
        merkleBlock = CMerkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {1, 2});
    }

    BOOST_AUTO_TEST_CASE(merkle_block_2_with_update_none_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 2 With Update None Test");

        CBlock block = ChainedBlock();
        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_NONE);
        // Match the output of transaction 1 by its address. The filter is not
        // updated, so the transaction that spends it is not matched.
        filter.insert(Bytes(TestKey(21).GetPubKey().GetID()));

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {1});
        BOOST_CHECK(!filter.contains(COutPoint(block.vtx[1]->GetHash(), 0)));
    }

    BOOST_AUTO_TEST_CASE(merkle_block_3_and_serialize_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 3 and Serialize Test");

        // A block with only the coinbase transaction
        CBlock block = TestBlock({});
        BOOST_CHECK_EQUAL(block.vtx.size(), 1);
        BOOST_CHECK(block.hashMerkleRoot == block.vtx[0]->GetHash());

        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(block.vtx[0]->GetHash());

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {0});

        // what is sent to the peer: the header, the number of transactions, one hash, one flag byte
        CDataStream merkleStream(SER_NETWORK, PROTOCOL_VERSION);
        merkleStream << merkleBlock;
        CDataStream expected(SER_NETWORK, PROTOCOL_VERSION);
        expected << block.GetBlockHeader() << (unsigned int) 1 << std::vector<uint256>{block.vtx[0]->GetHash()} << std::vector<unsigned char>{1};
        BOOST_CHECK_EQUAL_COLLECTIONS(expected.begin(), expected.end(), merkleStream.begin(), merkleStream.end());

        // and it is read back to the same
        CMerkleBlock read;
        merkleStream >> read;
        BOOST_CHECK(read.header.GetBlockHash() == block.GetBlockHash());
        std::vector<uint256> vMatched;
        std::vector<unsigned int> vIndex;
        BOOST_CHECK(read.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
        BOOST_REQUIRE_EQUAL(vMatched.size(), 1);
        BOOST_CHECK(vMatched[0] == block.vtx[0]->GetHash());
    }

    // A block with a pay-to-pubkey output (transaction 1, TestKey(31)) and a
    // pay-to-pubkey-hash output (transaction 2, TestKey(32)), and other transactions.
    static CBlock PubKeyBlock()
    {
        const std::vector<unsigned char> sig(71, 0x30);
        uint256 prevHash;
        memset(prevHash.begin(), 0xef, 32);
        std::vector<CMutableTransaction> txs;
        txs.push_back(TestTx(COutPoint(prevHash, 0), CScript() << sig, CScript() << ToByteVector(TestKey(31).GetPubKey()) << OP_CHECKSIG));
        txs.push_back(TestTx(COutPoint(prevHash, 1), CScript() << sig, GetScriptForDestination(TestKey(32).GetPubKey().GetID())));
        for (unsigned int n = 2; n < 7; n++)
            txs.push_back(TestTx(COutPoint(prevHash, n), CScript() << sig, GetScriptForDestination(TestKey(33 + n).GetPubKey().GetID())));
        return TestBlock(txs);
    }

    BOOST_AUTO_TEST_CASE(merkle_block_4_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 4 Test");

        CBlock block = PubKeyBlock();
        BOOST_CHECK_EQUAL(block.vtx.size(), 8);
        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        // Match the last transaction
        filter.insert(block.vtx[7]->GetHash());

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {7});

        // Also match the transaction in the middle
        filter.insert(block.vtx[4]->GetHash());
        merkleBlock = CMerkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {4, 7});

        // BLOOM_UPDATE_ALL adds the matched outputs of both kinds
        filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
        filter.insert(ToByteVector(TestKey(31).GetPubKey()));
        filter.insert(Bytes(TestKey(32).GetPubKey().GetID()));
        merkleBlock = CMerkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {1, 2});
        BOOST_CHECK(filter.contains(COutPoint(block.vtx[1]->GetHash(), 0)));
        BOOST_CHECK(filter.contains(COutPoint(block.vtx[2]->GetHash(), 0)));
    }

    BOOST_AUTO_TEST_CASE(merkle_block_4_test_p2pubkey_only_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 4 Test P2PubKey Only Test");

        CBlock block = PubKeyBlock();
        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_P2PUBKEY_ONLY);
        // Match the pay-to-pubkey output of transaction 1
        filter.insert(ToByteVector(TestKey(31).GetPubKey()));
        // ... and the pay-to-pubkey-hash output of transaction 2
        filter.insert(Bytes(TestKey(32).GetPubKey().GetID()));

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {1, 2});

        // We should match the pay-to-pubkey outpoint
        BOOST_CHECK(filter.contains(COutPoint(block.vtx[1]->GetHash(), 0)));
        // ... but not the pay-to-pubkey-hash outpoint
        BOOST_CHECK(!filter.contains(COutPoint(block.vtx[2]->GetHash(), 0)));
    }

    BOOST_AUTO_TEST_CASE(merkle_block_4_test_update_none_test)
    {
        BOOST_TEST_MESSAGE("Running Merkle Block 4 Test Update None Test");

        CBlock block = PubKeyBlock();
        CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_NONE);
        filter.insert(ToByteVector(TestKey(31).GetPubKey()));
        filter.insert(Bytes(TestKey(32).GetPubKey().GetID()));

        CMerkleBlock merkleBlock(block, filter);
        CheckMatches(block, merkleBlock, {1, 2});

        // We shouldn't match any outpoints (UPDATE_NONE)
        BOOST_CHECK(!filter.contains(COutPoint(block.vtx[1]->GetHash(), 0)));
        BOOST_CHECK(!filter.contains(COutPoint(block.vtx[2]->GetHash(), 0)));
    }

    static std::vector<unsigned char> RandomData()
    {
        uint256 r = InsecureRand256();
        return std::vector<unsigned char>(r.begin(), r.end());
    }

    BOOST_AUTO_TEST_CASE(rolling_bloom_test)
    {
        BOOST_TEST_MESSAGE("Running Rolling Bloom Test");

        // last-100-entry, 1% false positive:
        CRollingBloomFilter rb1(100, 0.01);

        // Overfill:
        static const int DATASIZE = 399;
        std::vector<unsigned char> data[DATASIZE];
        for (int i = 0; i < DATASIZE; i++)
        {
            data[i] = RandomData();
            rb1.insert(data[i]);
        }
        // Last 100 guaranteed to be remembered:
        for (int i = 299; i < DATASIZE; i++)
        {
            BOOST_CHECK(rb1.contains(data[i]));
        }

        // false positive rate is 1%, so we should get about 100 hits if
        // testing 10,000 random keys. We get worst-case false positive
        // behavior when the filter is as full as possible, which is
        // when we've inserted one minus an integer multiple of nElement*2.
        unsigned int nHits = 0;
        for (int i = 0; i < 10000; i++)
        {
            if (rb1.contains(RandomData()))
                ++nHits;
        }

        // Run test_aokchain with --log_level=message to see BOOST_TEST_MESSAGEs:
        BOOST_TEST_MESSAGE("RollingBloomFilter got " << nHits << " false positives (~100 expected)");

        // Insanely unlikely to get a fp count outside this range:
        BOOST_CHECK(nHits > 25);
        BOOST_CHECK(nHits < 175);

        BOOST_CHECK(rb1.contains(data[DATASIZE - 1]));
        rb1.reset();
        BOOST_CHECK(!rb1.contains(data[DATASIZE - 1]));

        // Now roll through data, make sure last 100 entries
        // are always remembered:
        for (int i = 0; i < DATASIZE; i++)
        {
            if (i >= 100)
                BOOST_CHECK(rb1.contains(data[i - 100]));
            rb1.insert(data[i]);
            BOOST_CHECK(rb1.contains(data[i]));
        }

        // Insert 999 more random entries:
        for (int i = 0; i < 999; i++)
        {
            std::vector<unsigned char> d = RandomData();
            rb1.insert(d);
            BOOST_CHECK(rb1.contains(d));
        }
        // Sanity check to make sure the filter isn't just filling up:
        nHits = 0;
        for (int i = 0; i < DATASIZE; i++)
        {
            if (rb1.contains(data[i]))
                ++nHits;
        }
        // Expect about 5 false positives, more than 100 means
        // something is definitely broken.
        BOOST_TEST_MESSAGE("RollingBloomFilter got " << nHits << " false positives (~5 expected)");
        BOOST_CHECK(nHits < 100);

        // last-1000-entry, 0.01% false positive:
        CRollingBloomFilter rb2(1000, 0.001);
        for (int i = 0; i < DATASIZE; i++)
        {
            rb2.insert(data[i]);
        }
        // ... room for all of them:
        for (int i = 0; i < DATASIZE; i++)
        {
            BOOST_CHECK(rb2.contains(data[i]));
        }
    }

BOOST_AUTO_TEST_SUITE_END()
