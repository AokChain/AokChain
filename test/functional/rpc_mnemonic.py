#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mnemonictokeys and mnemonicfindaddress.

The keys are checked against an independent BIP39/BIP32 implementation in
this file (hashlib, hmac and textbook secp256k1 arithmetic).
"""

import hashlib
import hmac
import unicodedata

from test_framework.address import byte_to_base58, key_to_p2pkh
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# secp256k1
P = 2**256 - 2**32 - 977
N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
G = (0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798,
     0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8)
H = 0x80000000


def point_add(a, b):
    if a is None:
        return b
    if b is None:
        return a
    if a[0] == b[0] and (a[1] + b[1]) % P == 0:
        return None
    if a == b:
        lam = 3 * a[0] * a[0] * pow(2 * a[1], P - 2, P) % P
    else:
        lam = (b[1] - a[1]) * pow(b[0] - a[0], P - 2, P) % P
    x = (lam * lam - a[0] - b[0]) % P
    return (x, (lam * (a[0] - x) - a[1]) % P)


def pubkey_of(secret, compressed=True):
    point, addend = None, G
    while secret:
        if secret & 1:
            point = point_add(point, addend)
        addend = point_add(addend, addend)
        secret >>= 1
    if compressed:
        return bytes([2 + (point[1] & 1)]) + point[0].to_bytes(32, 'big')
    return b'\x04' + point[0].to_bytes(32, 'big') + point[1].to_bytes(32, 'big')


def seed_of(mnemonic, passphrase=""):
    norm = lambda s: unicodedata.normalize("NFKD", s).encode("utf8")
    return hashlib.pbkdf2_hmac("sha512", norm(" ".join(mnemonic.split())), b"mnemonic" + norm(passphrase), 2048)


def derive(seed, path):
    """Private key (int) at a list of child numbers below the master key."""
    digest = hmac.new(b"Bitcoin seed", seed, hashlib.sha512).digest()
    key, chain = int.from_bytes(digest[:32], 'big'), digest[32:]
    for n in path:
        data = (b'\x00' + key.to_bytes(32, 'big')) if n & H else pubkey_of(key)
        digest = hmac.new(chain, data + n.to_bytes(4, 'big'), hashlib.sha512).digest()
        key, chain = (int.from_bytes(digest[:32], 'big') + key) % N, digest[32:]
    return key


def wif_of(secret, compressed=True):
    # regtest/testnet secret key prefix
    return byte_to_base58(secret.to_bytes(32, 'big') + (b'\x01' if compressed else b''), 239)


ABANDON = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
# vector 5 of the reference implementation's Korean test vectors, in NFC
KOREAN = unicodedata.normalize("NFC", "가격 가격 가격 가격 가격 가격 가격 가격 가격 가격 가격 가능")


class MnemonicTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def expected(self, mnemonic, path, passphrase="", compressed=True):
        secret = derive(seed_of(mnemonic, passphrase), path)
        return key_to_p2pkh(pubkey_of(secret, compressed)), wif_of(secret, compressed)

    def run_test(self):
        node = self.nodes[0]

        self.log.info("mnemonictokeys: default path, against the independent implementation")
        res = node.mnemonictokeys(ABANDON)
        assert_equal(res["language"], "english")
        assert_equal(len(res["keys"]), 5)
        for i, key in enumerate(res["keys"]):
            address, wif = self.expected(ABANDON, [44 | H, 0 | H, 0 | H, 0, i])
            assert_equal(key["path"], "m/44'/0'/0'/0/%d" % i)
            assert_equal(key["address"], address)
            assert_equal(key["privkey"], wif)
        # the well known first BIP44 key of this mnemonic
        assert_equal(derive(seed_of(ABANDON), [44 | H, 0 | H, 0 | H, 0, 0]),
                     0xe284129cc0922579a535bbf4d1a3b25773090d28c909bc0fed73b5e0222cc372)

        self.log.info("Path, count, start, passphrase; input is tidied up")
        res = node.mnemonictokeys("  " + ABANDON.upper().replace(" ", "   ") + "\n", "m/0h/1'", 2, "TREZOR", 7)
        for i, key in enumerate(res["keys"]):
            address, wif = self.expected(ABANDON, [0 | H, 1 | H, 7 + i], "TREZOR")
            assert_equal(key["path"], "m/0'/1'/%d" % (7 + i))
            assert_equal((key["address"], key["privkey"]), (address, wif))

        self.log.info("Korean mnemonic typed in composed form")
        res = node.mnemonictokeys(KOREAN, "m/44'/0'/0'/0", 1, "비밀번호")
        assert_equal(res["language"], "korean")
        address, wif = self.expected(KOREAN, [44 | H, 0 | H, 0 | H, 0, 0], "비밀번호")
        assert_equal((res["keys"][0]["address"], res["keys"][0]["privkey"]), (address, wif))

        self.log.info("The key can be imported and is then ours")
        key = node.mnemonictokeys(ABANDON, "m/44'/0'/0'/0", 1)["keys"][0]
        assert not node.validateaddress(key["address"])["ismine"]
        node.importprivkey(key["privkey"], "", False)
        assert node.validateaddress(key["address"])["ismine"]
        assert_equal(node.dumpprivkey(key["address"]), key["privkey"])

        self.log.info("Errors")
        bad = ABANDON.replace("about", "abandon")
        assert_raises_rpc_error(-8, "checksum", node.mnemonictokeys, bad)
        assert_raises_rpc_error(-8, "word 12", node.mnemonictokeys, ABANDON + "x")
        assert_raises_rpc_error(-8, "12, 15, 18, 21 or 24 words", node.mnemonictokeys, "abandon about")
        assert_raises_rpc_error(-8, "Invalid path", node.mnemonictokeys, ABANDON, "44'/0'")
        assert_raises_rpc_error(-8, "count", node.mnemonictokeys, ABANDON, "m", 0)
        assert_raises_rpc_error(-8, "cannot be normalized", node.mnemonictokeys, "café " * 12)
        # validation can be skipped, the seed is defined for any text
        res = node.mnemonictokeys(bad, "m/0", 1, "", 0, False)
        assert_equal(res["language"], "")
        assert_equal(res["keys"][0]["address"], self.expected(bad, [0, 0])[0])

        self.log.info("mnemonicfindaddress")
        for path, text in (([44 | H, 175 | H, 0 | H, 0, 3], "m/44'/175'/0'/0/3"),
                           ([44 | H, 1999 | H, 1 | H, 1, 19], "m/44'/1999'/1'/1/19"),
                           ([84 | H, 0 | H, 0 | H, 0, 0], "m/84'/0'/0'/0/0"),
                           ([0 | H, 0 | H, 5 | H], "m/0'/0'/5'"),
                           ([0, 42], "m/0/42")):
            address, wif = self.expected(ABANDON, path, "pw")
            res = node.mnemonicfindaddress(ABANDON, address, "pw")
            assert res["found"], text
            assert_equal((res["path"], res["address"], res["privkey"], res["compressed"]), (text, address, wif, True))
        # address of the uncompressed public key
        address, wif = self.expected(ABANDON, [44 | H, 0 | H, 0 | H, 0, 1], compressed=False)
        res = node.mnemonicfindaddress(ABANDON, address)
        assert_equal((res["found"], res["path"], res["privkey"], res["compressed"]), (True, "m/44'/0'/0'/0/1", wif, False))
        # out of the searched range, and a wrong passphrase
        address = self.expected(ABANDON, [44 | H, 0 | H, 0 | H, 0, 20])[0]
        res = node.mnemonicfindaddress(ABANDON, address)
        assert not res["found"] and res["searched"] > 100000 and "privkey" not in res
        address = self.expected(ABANDON, [44 | H, 0 | H, 0 | H, 0, 0], "pw")[0]
        assert not node.mnemonicfindaddress(ABANDON, address)["found"]
        assert_raises_rpc_error(-5, "pay-to-pubkey-hash", node.mnemonicfindaddress, ABANDON, "notanaddress")


if __name__ == '__main__':
    MnemonicTest().main()
