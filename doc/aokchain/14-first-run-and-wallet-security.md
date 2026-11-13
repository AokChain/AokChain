# 14. First run, wallet security, restore from recovery words, send confirmation

GUI round 8. What changed in the Qt wallet, why, and what was and was not verified.
Screens are described in [10-gui-theme.md](10-gui-theme.md); this document covers the parts that
touch keys and coins.

## First run

The first start is one sequence of three screens with the same header (`WizardHeader`,
`src/qt/firstrun.{h,cpp}`): logo, title, the steps with the current one marked.

1. **Start** (`Intro`): language and data folder. The theme is now applied before this dialog.
2. **Wallet passphrase** (`AskPassphraseDialog` in `Encrypt` mode with `setOnboarding()`): shown
   by `AokChainApplication::initializeResult()` before the main window when the wallet is
   unencrypted and has no transactions (`WalletModel::isFreshUnencrypted()`). The passphrase is
   entered twice, with a rough strength hint and a box to tick ("I wrote it down and understand
   that it cannot be recovered"). The only other way out is "Quit", which ends the program and
   leaves the wallet as it was.
3. **Backup** (`BackupPromptDialog`): `EncryptWallet` needs a restart in this code base (the
   wallet file is rewritten on shutdown). The program restarts by itself
   (`GUIUtil::requestRestart({"-backupprompt"})`, the same mechanism as the language switch) and
   the new process offers to save a copy of the wallet file. `-backupprompt` is removed from the
   arguments of any later restart.

An unencrypted wallet that already has transactions is not forced. `WalletFrame` shows a warning
band above the pages with an "Encrypt" button; the "x" hides it until the next start.

`-skipencryptprompt` (not listed in the help) turns off both the passphrase step and the band. It
is for tests and for scripts that drive the window. `aokchaind` and the RPC interface are
unchanged: they still create and run unencrypted wallets.

A test network that was never started takes the language of the main settings, because the first
screen stores the language there.

## Staking with an encrypted wallet

`fWalletUnlockStakingOnly` existed in the core (RPC `walletpassphrase ... true`) but the GUI
ignored it: the staking switch unlocked the wallet completely and left it that way. Now:

- `WalletModel::EncryptionStatus` has `UnlockedForStaking`.
- The staking switch asks for the passphrase with "For staking only" ticked
  (`AskPassphraseDialog::UnlockStaking`). The side bar says "Unlocked for staking only".
- `WalletModel::requestUnlock()` treats that state as locked: sending, signing and importing ask
  for the passphrase, and the `UnlockContext` returns to "staking only" afterwards instead of
  locking (staking goes on).
- Turning the switch off locks a wallet that was unlocked for staking only.

Known limit: `walletpassphrase` with a timeout, called over RPC or in the console, still locks the
wallet when the timeout runs out, whatever the GUI did in between. Staking then stops until the
switch is used again.

## Restore from recovery words

Settings → Wallet security → "Restore from recovery words" (`MnemonicDialog`,
`src/qt/mnemonicdialog.{h,cpp}`), and the RPC `importmnemonic "mnemonic" ( "passphrase" "address"
rescan )`. Both use `src/wallet/mnemonicrestore.{h,cpp}`.

What it does:

1. Checks the words (English and Korean word lists, checksum) and makes the BIP32 master key.
2. Derives the keys of the usual layouts (`bip39::CandidateLayouts(false)`, about 7,000 keys:
   simple layouts such as `m/0'/0'/k'`, and BIP44/49/84 for the common coin types, accounts 0 and
   1, receiving and change chain). `mnemonicfindaddress` uses the same list with all coin types.
3. Walks the unspent outputs of the chain (the chain state database, after a flush) and notes
   which of these addresses hold coins or tokens now. No address index is needed.
4. For each path with coins, and its sibling (receiving ↔ change), follows the path further (to
   index 200, then to 1000 if the end was not seen) and imports the keys up to 20 places past the
   last one that holds coins.
5. If the user gives one address of that wallet, its path is searched among all coin types (up to
   a minute) and imported even if it holds nothing now.
6. Adds the keys to the wallet (creation time 1, as `importprivkey` does). Only the addresses that
   hold coins get an address book entry (label "Restored" / "restored").
7. Rescans the whole chain in pieces of 500 blocks, so that `cs_main` and `cs_wallet` are released
   in between. In the GUI this runs in a worker thread with a progress bar and a "Stop" button;
   the wallet's own progress window is suppressed meanwhile (`WalletModel::setProgressQuiet`).

Limits, on purpose:

- A wallet whose coins were all spent is not found without an address (step 3 looks at unspent
  outputs only). With an address its path is imported and the rescan brings the history.
- Addresses used beyond index 1000 of a path, or more than 20 unused places after the last one
  with coins, are not imported.
- Only pay-to-pubkey-hash (with or without token data) and pay-to-pubkey outputs are matched.
- The restore is refused while the node is in initial block download: coins in blocks that are not
  here yet would be missed.

Handling of the secret:

- The words and the BIP39 passphrase are never written to disk, to the log or to the settings.
  The GUI turns them into the master key in one step, overwrites and clears the fields, and
  cleanses the intermediate buffers (`SecureString`, `memory_cleanse`). The live check of the words
  cleanses its copies as well. The master key is dropped when the search is done; the keys to
  import are `CKey` objects (locked memory) until they are in the wallet.
- A `QString`/`QPlainTextEdit` cannot be locked in memory. The fields are overwritten before they
  are cleared, which is the same best effort as the passphrase dialog.
- The debug console does not keep `importmnemonic` in its history.
- An encrypted wallet has to be unlocked for the import (the GUI asks; "unlocked for staking only"
  is not enough). The keys are stored encrypted.
- The imported keys are separate from the HD seed of the wallet. The dialog says so before the
  import and offers "Back up now" afterwards.

**Hook for a node without past blocks:** `WalletModel::ensurePastBlocksAvailable(QString*)` is the
one place the GUI asks whether a rescan is possible. It answers false for a pruned node today. A
node that starts from a snapshot has to answer here (or fetch the blocks) when that feature lands;
the dialog shows the reason and offers nothing else.

## Settings page

- The network options (UPnP, incoming connections, proxy, Tor proxy, fast synchronization with
  the list of addresses and the folder button) are in a window opened by "Network settings" in the
  Information card. "Save" does what "Save" of the page does (it submits every option of the
  page); "Cancel" puts the network fields back and leaves other unsaved changes alone.
- The note about options overridden on the command line is at the end of the Information card,
  small, and hidden when there are none.
- "Wallet management" is now "Wallet security", with a short explanation of encryption, backup and
  recovery words.
- The version is at the bottom left of the side bar.

## Address book

Receiving and sending addresses are side by side (title, count, search, new/add, copy, edit, QR
code or send, delete for sending addresses, export). Addresses are shown as 4…4 with the whole
address as tool tip. Below 700 px of page width the two lists are stacked.

## Send confirmation

`SendConfirmDialog` (`src/qt/sendconfirmdialog.{h,cpp}`) replaces the message box of the AOK send,
the token send and is new before a multisig send request is made.

- The asset first: logo, name as the largest text, a "Coin" or "Token" chip; a token send has a
  different card (warning colors). Then the amount in the mono font.
- Every recipient with label and the whole address in groups of four characters; with several
  recipients the amount of each and the total.
- Fee (on a token send: "The fee is paid in AOK"), what leaves the balance, and the balance of
  each affected asset afterwards.
- Warnings: a token is being sent; the whole balance is being sent; addresses that are not in the
  address book and not ours (nothing was sent there before).
- "Send" is disabled for two seconds. There is no default button: Enter does nothing, Escape and
  Space cancel.
- After the send: the transaction id in the mono font with a copy button; a failed broadcast gets
  its own window. Errors before the confirmation (invalid address, not enough coins) are message
  boxes as before, now with icons drawn in the theme.

Issuing and reissuing tokens (advanced view) still use the old confirmation.

## Verified

- Functional tests: `wallet_importmnemonic.py` (new: several paths, gap limit, Korean words with a
  passphrase, an unusual coin type found by address, spending the restored coins, encrypted wallet
  and "staking only"), `rpc_mnemonic.py`, `wallet_encryption.py`, `feature_staking.py`.
- On regtest, by driving the window with xdotool under Xvfb: first run (language switch to Korean,
  data folder, passphrase with weak/mismatch hints, automatic restart, backup saved to a file);
  staking switch with an encrypted wallet (blocks 101 and 102 were proof-of-stake, `sendtoaddress`
  refused with "unlocked for staking only"); restore of 1,327.5 AOK on three addresses of a test
  mnemonic into the encrypted, staking wallet (balance before 200, after 1,527.5; the words are in
  neither debug.log nor the settings file); network window (cancel restores, save writes the
  settings file); address book (add on both sides, edit, delete, narrow window); AOK send to one
  and to three recipients and a token transfer (ARTL issued on regtest; 200,000 arrived at the
  recipient).

## Not verified

- Windows and macOS (native file dialogs, restart of the process on Windows).
- The multisig confirmation was built but not clicked through.
- A real mobile-wallet mnemonic and the duration of the unspent-output walk and of the rescan on
  mainnet.
- Stopping a rescan half way, and a reorganization during the rescan.
