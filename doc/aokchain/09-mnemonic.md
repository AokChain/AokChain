# 09. Extracting private keys from a mnemonic (recovery words)

The AokChain Core wallet does not use a mnemonic (random HD seed, path `m/0'/0'/k'`). Two RPCs were added (`cc0c6ff`) so that private keys can be extracted from a **BIP39 mnemonic created by another wallet**, such as a mobile wallet, and imported into the Core wallet. They work without a wallet (`-disablewallet`) and need no network connection. Nothing is stored.

## Usage

In the GUI run them in `Help → Debug window → Console`, on a server with `aokchain-cli`.

**1. When the path is unknown — find it with one address of that wallet**
```
mnemonicfindaddress "word1 word2 ... word12" "an AOK address of that wallet"
```
- If found, it returns `path` (the path) and `privkey` (the private key). It takes up to about a minute (about 250,000 keys are tried).
- Search range: BIP44 `m/44'/coin'/account'/chain/index` (coin 0~2047, account 0~1, chain 0~1, index 0~19), BIP49/84 paths with common coin numbers, simple paths such as `m/0'/0'/k'` and `m/0/k` (index 0~99). Addresses of both compressed and uncompressed public keys are examined.
- `found: false` means that the mnemonic or the address is different, that there is a passphrase, or that the path is outside the range.

**2. When the path is known — list the keys**
```
mnemonictokeys "word1 ... word12" "m/44'/0'/0'/0" 20
```
- The path is the `path` found above without its last number. Example: `m/44'/0'/0'/0/3` → `m/44'/0'/0'/0`. Change addresses are usually on the path that ends in `/1` (`m/44'/0'/0'/1`).
- Arguments: `mnemonic`, `path` (default `m/44'/0'/0'/0`), `count` (default 5), `passphrase` (BIP39 passphrase, default none), `start` (default 0), `validate` (default true).

**3. Import into the Core wallet**
```
importprivkey "private key" "label" true
```
The last argument `true` is the rescan that walks the historical transactions again (it takes time). With several keys, use `true` only for the last key.

## Cautions
- **Anyone who has seen the mnemonic or the private keys can take the coins.** Do not paste them into chats, e-mail or websites. If possible, run the commands on a PC that is disconnected from the network.
- The GUI console does not keep these two commands in its command history (handled like `importprivkey`). When run with `aokchain-cli` they remain in the shell history (`~/.bash_history`), so delete them.
- The words and the checksum are validated against the **English and Korean** word lists. Typos, missing words and swapped order are caught here. Lists in other languages can be skipped with `validate=false`, but characters other than Hangul and ASCII are rejected because normalization (NFKD) is not implemented for them.
- The imported keys are individual keys unrelated to the HD seed of the Core wallet. A new wallet backup has to be made.

## Verification
- Unit test `bip39_tests`: the seeds and BIP32 root keys match for 24 English and 24 Korean vectors from the official vectors of the BIP39 reference implementation (trezor/python-mnemonic).
- Functional test `rpc_mnemonic.py`: addresses and private keys match a Python implementation written separately inside the test (hashlib + textbook secp256k1), and a key extracted this way becomes my address when imported with `importprivkey`. The well-known vector (`m/44'/0'/0'/0/0` of `abandon … about`) matches too.
- **What could not be verified:** it was not tested with a real mnemonic of the AOK mobile wallet (com.aokey). The derivation path of that wallet is not published, so it has to be found with `mnemonicfindaddress`, and if the wallet uses a scheme that is not standard BIP39 (its own seed conversion or the like), it will not be found.
