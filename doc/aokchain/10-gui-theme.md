# 10. GUI — theme, fonts, page layout

The appearance of the Qt wallet (`aokchain-qt`) is defined in one place. It changed in two rounds. The first round tidied up the theme structure and the fonts; the second (the current state of this document) changed the page layout to match the approved design mock-up.

## The pages now

| Item | Contents |
|---|---|
| Shell | A **left sidebar** (232px) instead of the tab bar at the top. Logo, page list (Overview, Send, Receive, Transactions, Tokens, Explorer; below a line Address book, Sign, Settings; with `-advancedui`, Create Tokens and Manage Tokens come first below the line), staking switch, sync status. If the window is too low for the whole list, only the list scrolls. **The menu bar is hidden on Windows and Linux** (see "Menu bar" below) |
| Status display | The status bar is hidden. Sync status, time remaining, progress bar, number of peers, network name and wallet lock state are shown at the bottom of the sidebar. Clicking the sync line opens the sync information window; clicking the peer line turns the network on/off |
| Node warnings | The large band above the pages was removed. Warnings appear as two lines of small text at the very bottom of the sidebar, with the full text in the tooltip. The pre-release build notice is in the normal colour, other warnings in the warning colour |
| Page header | Above each page a title (24px bold) and secondary information on the right. Overview shows one line of information only while syncing, Send shows the available balance, Transactions has the CSV export |
| Theme | **Light only** |
| Fonts | Body text Noto Sans / Noto Sans KR; **amounts, addresses, transaction IDs, dates in tables and block heights in Noto Sans Mono** |
| Overview | At the top a **search box** (transaction ID, address, block height — a lookup switches to the explorer and shows the result there) and the **chain status strip** (the same component, `StatsStrip`, as at the top of the explorer: block height, difficulty, network stake weight, connected peers, transactions waiting in the mempool — clicking the mempool cell opens the explorer). Below that the balance card, the token card (root tokens only) and the recent transactions card. The margins were reduced so that it fits in a 1280x800 window |
| Send | **AOK / Tokens tabs** at the top. On the left the recipient cards and the fee, on the right the summary. See "Send page" below |
| Receive | On the left the payment request form (marked "optional"), on the right the **receiving address and QR code** (Copy QR code, Copy address), below the request history |
| Transactions | Type selection (All, Received, Sent, Staked), search, **asset selection list** (All assets, AOK, root tokens), period (default **All time**), a table inside a card, **paging** below the table (50, 100 or 200 per page) |
| Tokens | **Root tokens** as cards (logo, name, chips, amount, decimal places and whether it is reissuable, Send and Receive buttons, and a "N sub tokens" button if there are sub tokens). Only tokens held by the wallet are shown (other tokens on the chain are found in the explorer) |
| Explorer | Lookup by transaction ID, address, block height or block hash; chain status; mempool |
| Address book | **Receiving addresses / Sending addresses** tabs. Search (label, address), new address, copy, edit, QR code (receiving addresses), Send to this address and delete (sending addresses), CSV export. Addresses are shown in full in the mono font |
| Sign | Task cards (Prove my address, Check a signature, Multisig) and the list of my multisig wallets. For details see [11-multisig.md](11-multisig.md) |
| Settings | At the very top a **"Language"** row (globe icon, each language shown in its own language; a choice is saved immediately and a "Restart now" button appears. The first-run dialog has the same selector). Below that the options as cards (General, Network — including the fast sync addresses and the folder selection, Wallet, Display), the options overridden on the command line, **Wallet management** (encrypt, change passphrase, backup), **Information** (version, debug window, command-line options, about the program, about Qt, open URI, exit). In the bottom bar: Open Configuration File, Reset Options, Revert, Save |

## Structure

- **Colours:** `GUIUtil::theme()` (`src/qt/guiutil.cpp`) returns the colours of the theme (background, card, border, text, primary button `fill`, emphasis `accent`, received `positive`, sent `negative`, confirmed `ok`, error `error`, warning, chip, sidebar colours). The `COLOR_*` and `STYLE_*` macros in `guiconstants.h` and the colour functions of `PlatformStyle` all refer to this.
- **Style sheet:** `src/qt/res/css/theme.qss`. `GUIUtil::applyTheme()` replaces tokens such as `@accent` with the theme colours and applies the sheet to the whole application. The look of a widget is chosen with properties: buttons `primary`, `look` (`large`, `send`, `ghost`, `dashed`, `soft`, `link`, `seg`, `fee`), input fields `look` (`field`, `search`, `bare`), labels and frames `role` (`card`, `tile`, `pageTitle`, `title`, `caption`, `fieldLabel`, `warning`, `pill`, `notice` etc.).
- **Fonts:** `GUIUtil::appFont(px, weight)` creates the body font, `GUIUtil::monoFont(px, weight)` the fixed-width font. `GUIUtil::fixedPitchFont()` also returns the fixed-width font. To make only some columns of a table fixed-width use `GUIUtil::MonoColumnDelegate`; inside HTML use `GUIUtil::monoHtml()`.
- **Icons:** the line icons of the sidebar and the buttons are drawn by `GUIUtil::lineIcon(name, size, colour)` (no image files). The transaction direction tile is `GUIUtil::paintDirectionTile()`.
- **Shell:** `src/qt/sidebar.{h,cpp}` (sidebar, switch, progress bar). `AokChainGUI` updates the sidebar in the places where it used to update the old status bar. The page header is attached by `WalletView::addPage()`.

When making a new page, do not write colour values or font names directly.

## Rules

### Displaying amounts (common to all pages)
An amount is drawn with **the decimal point and the digits after it at 60% of the font size of the integer part**, on the same baseline, in the fixed-width font. Only one implementation is used: `AmountLabel` (`src/qt/pagewidgets.h`) for widgets, `GUIUtil::paintAmount()`/`amountWidth()` for the delegates of lists and tables, and the amount column of `GUIUtil::MonoColumnDelegate` for ordinary tables. The balances on Overview, token amounts, recent transactions, the Transactions table, the summary and header of Send, the explorer, the request history of Receive and coin control all use it. Input fields stay at one size. So that the fractional part does not get too small, the integer part in lists is 15px and the minimum size of the fractional part is 9px.

### Weight
Bold text was reduced. **Bold is used only for page titles and large balances (total balance, the total in the summary).** Card titles and primary buttons are SemiBold (600); body text, list titles, amounts in lists and tables, chips and badges are Medium (500) or lighter. A Medium weight was added for the fixed-width font as well.

### Fixed-width font
Amounts, addresses, transaction IDs, block hashes, block heights and dates inside tables are drawn with `monoFont()`. Overview, the transaction table, the explorer and the Send summary show thousands separators (`GUIUtil::withThousands()`, display only). Parsing of input values and the values for copy and export were not changed.

### Shortened addresses
`GUIUtil::shortAddress()` shortens a valid address to **the first 4 characters + … + the last 4 characters**. It is used only in narrow lists (recent transactions on Overview, the Send summary, inputs and outputs in the explorer, the transaction table when there is not enough room). Labels are not shortened. Hovering over the shortened text shows the **full address as a tooltip** (`GUIUtil::monoToolTip()`, in lists the `helpEvent()` of the delegate).

### Wallets with many sub tokens (showing only root tokens)
One wallet can hold tens of thousands of sub tokens (`ROOT/SUB`) and unique tokens (`ROOT#TAG`). Therefore **no widget is created per held token, and the sub tokens are not all put into a combo box.** Only root tokens appear on the pages, and the tokens below them are chosen by searching or from a separate list window. The lists are model/view (`QListView` + delegate), so only the visible rows are drawn. The implementation is in `src/qt/tokenlists.{h,cpp}`.

- `HeldTokenIndex`: follows the token table of the wallet (`TokenTableModel`), keeps the held tokens sorted by name and groups them by root (the amount of the root itself, whether the owner token is held, the number of tokens held below it). A root appears if any of the root itself, the owner token or a token below it is held.
- `TokenListModel` + `TokenRowDelegate`: one row of the list (logo, name, chips, amount).
- `TokenListDialog`: a window with a search box and a list. Without a root it is the list of root tokens, and typing also finds sub and unique tokens by part of their name. With a root it is the list of the sub and unique tokens of that root. It shows at most 200 rows at a time and says "Type more to narrow the list."

Where it is used:
- **Send → Tokens:** the icon row shows only root tokens (at most 20). "+" opens the search window above. When a root that has sub tokens is chosen, "N sub tokens ›" appears next to the name, and clicking it opens the list of that root. When a sub or unique token is chosen, its full name appears together with the logo of the root. The (hidden) token selection box of the recipient card contains only the one chosen token.
- **Tokens page and Overview:** only roots appear as cards and rows. If the root itself is not held and only tokens below it are, it says "None held". The "N sub tokens" button of the card and the "N below" chip on Overview (or "Show the N sub tokens" in the menu when a row is clicked) open the list window, and clicking "Send" there goes to the Tokens tab of Send with that token selected.
- **Asset filter of Transactions:** contains only All assets, AOK and the root tokens. Choosing a root shows that root and everything below it: names equal to the root, or the root followed by `/`, `#` or `!` (`SERG` matches `SERG`, `SERG!`, `SERG/...` and `SERG#...` but not `SERGX`).

### Display limits and behaviour with large wallets
However many tokens are held, the number shown on a page is fixed by constants (`TokenLimits` in `src/qt/tokenlists.h`).

| Constant | Value | Where it is used |
|---|---|---|
| `LIST_ROWS` | 200 | Number of rows in the token list window and in search results. Above that, "Type more to narrow the list." |
| `ROOT_CARDS` | 120 | Number of root cards on the Tokens page. Above that, "N kinds, the first 120 shown" |
| `ICON_ROW` | 20 | Number of round buttons in Send → Tokens. The rest through the search behind "+" |

In addition, the explorer shows up to 40 rows of inputs and outputs, 200 rows of transactions of a block, 30 rows of the mempool and 50 rows of transactions of an address. Transactions shows 50 rows per page (100 or 200 selectable).

Work that is proportional to the number of held tokens was bounded as follows.
- **Reading the token balances** (`TokenTableModel`): uses the core functions that walk all token outputs of the wallet (`GetAllMyTokenBalances`, `GetAllMyLockedTokenBalances`) and the per-token metadata lookup. Since it is called for every block and every wallet transaction, it runs in a **worker thread**, requests that arrive while it is running are merged into one, and the GUI is notified **only when the contents have changed**. Once, at program start, it is read in the GUI thread.
- **Grouping by root** (`HeldTokenIndex`): the pages share one per wallet (`HeldTokenIndex::forModel()`). It is rebuilt only when the token table changes. It is a vector sorted by name, so the tokens below a root are found by binary search, and a search by part of a name stops once it has 200 results.
- **Recipient card**: reads the balance of the chosen token from the grouping above (previously it walked the wallet every time a token was chosen). Only when outputs were chosen directly with token control does it count as before.
- **Owner token lists of the Create Tokens and Manage Tokens pages**: these used to walk the wallet and refill on every incoming transaction; now this is done only while the page is visible.
- **Cards of the Tokens page**: rebuilt only when the contents of a root have changed.
- **Asset list of Transactions**: looks only at newly added rows. The whole table is read only once at the beginning.

What could not be bounded (left as it is because it is in the core or only for `-advancedui`) is listed under "What was and was not verified". To measure times, start with `-debug=qt`; `GUI timing:` lines are then written to debug.log.

### Token lists
- One row per token name. The owner token `NAME!` does not get a row of its own but is shown as an "Owner" chip on the `NAME` row. If only `NAME!` is held, the `NAME` row shows the chip and "None held".
- Sub tokens `PARENT/CHILD` and unique tokens `PARENT#TAG` do not appear as rows (section above). The parent row gets an "N below" chip, and inside the list window they are distinguished by "Sub token" and "Unique token" chips. Sorted by name.
- Clicking a row shows the same menu as before (send, issue, copy name and amount).

### Token logos
Logos are not on the chain. 13 of them (ARTL, CCA, CCL, DHTB, DZTB, GND, MEC, NMO, NOL, PAPT, PLB, SERG, TONG) are included in the wallet as 96px PNGs (`src/qt/res/tokens/`, resource path `:/tokens/<lowercase name>`). The source is `api.aok.network/static/logo`. `GUIUtil::tokenLogo()` looks them up by the **root name** (the part before `/`, `#`, `!`) and, if there is none, draws a round avatar with the first letter. To add a logo, add the PNG and register it in `aokchain.qrc` and `src/Makefile.qt.include`.

## Send page

`SendCoinsDialog` (AOK) and `TokensDialog` (tokens) are the two tabs of one page. Both use the same components (`src/qt/pagewidgets.{h,cpp}`): the summary card `SummaryCard`, the chip `ChipButton`, the choice button `ChoiceButton`, the token chooser `TokenPicker`, and the label and lock pop-ups `PageDialogs`. The sending logic is as before (the same `WalletModel` calls, the same confirmation dialog).

- **Recipient cards:** one card per recipient with a "Recipient N" header and a remove (x) button. Below the last card, "Add recipient".
- **Label:** the label field was removed from the form. The "Label" button to the right of the address opens a pop-up, and once set, the label appears as a chip below the address (click to edit, x to remove). For an address that is in the address book the label appears in the chip automatically.
- **Send with a lock:** instead of the lock row that was always visible, the "Send with a lock" button opens a pop-up (date and time, 1 month, 3 months, 6 months, 1 year). Once set, there is a "Locked until: date" chip. The behaviour is as before (`coinLockTime`/`tokenLockTime`).
- **Send button:** **grey and disabled** until every recipient has a valid address and an amount greater than 0 and the balance is sufficient. From then on it is a primary button that contains the amount ("Send 25 AOK").
- **Clear all:** the x button at the top right of the form card.
- **Fee choice:** there was a problem where the text of the selected item had the same colour as the background and was invisible. On the assumption that style sheet rules overlapped when a button inside a `QDialog` became the default button (`:default`), the choice button was changed to a **widget that paints itself** (`ChoiceButton`) and does not rely on the style sheet. `autoDefault` was also turned off for the buttons of the form.
- **Tokens tab:** the **root tokens** of the wallet appear as round logo buttons (letters of the name if there is no logo, at most 20). "+" is a window for searching and choosing, and sub and unique tokens are found there (section "Wallets with many sub tokens"). The name and available amount of the chosen token are shown, and all recipient cards use that token. Sending an owner token (`NAME!`) is done, as before, only with `-advancedui`, through the selection box inside the card.

## Explorer

`src/qt/explorerpage.{h,cpp}`. It does not use an external server; it calls the **RPC handlers of this node** through `tableRPC.execute()` and renders the results. It only reads. Lookups run in a worker thread (`ExplorerWorker`), so the GUI does not freeze.

| Lookup | RPCs used | What is shown |
|---|---|---|
| Transaction ID | `getrawtransaction`, otherwise the wallet's `gettransaction` + `decoderawtransaction` | Whether it is confirmed and the number of confirmations, block (click to look up the block), time, size, fee (when all inputs can be looked up), inputs and outputs (addresses shortened; token transfers with token name and amount) |
| Block height or hash | `getblockhash`, `getblock` | Hash, height, time, number of transactions, size, proof type, previous and next block, list of transactions |
| Address | `validateaddress`, `getaddressbalance`, `getaddresstxids` | Whether it is a wallet address, label, balance, total received, number of transactions, recent transaction IDs |
| Chain status and mempool | `getblockchaininfo`, `getstakinginfo`, `getconnectioncount`, `getmempoolinfo`, `getnetworkinfo`, `getrawmempool true` | Height, difficulty, network stake weight, number of peers, number, size and minimum fee of waiting transactions, list of waiting transactions (marked "My transaction"). Refreshed every 5 seconds while the page is visible |

**Index requirements**
- **Transactions:** `txindex` is on by default. If it is off (`txindex=0`), only wallet transactions, mempool transactions and transactions that still have unspent outputs can be looked up; for other transactions a notice says "put `txindex=1` into aokchain.conf and run once with `-reindex`".
- **Addresses:** balance and transaction history are shown only with `addressindex=1` (off by default). If it is off, only whether it is a wallet address is shown, together with a notice about `addressindex=1` and `-reindex`.

The context menu entry "View in the explorer" of the transaction history looks up that transaction directly. Transaction IDs, addresses and blocks inside the explorer can be clicked to continue the lookup.

## Font files and licences

They are in `src/qt/res/fonts/`. All are under the SIL Open Font License 1.1, and the full licence texts are in the same folder (`OFL-NotoSans.txt`, `OFL-NotoSansKR.txt`, `OFL-NotoSansMono.txt`).

| File | Source |
|---|---|
| `NotoSans-{Regular,Medium,SemiBold,Bold}.ttf` | notofonts/notofonts.github.io (hinted TTF) |
| `NotoSansMono-{Regular,Medium,SemiBold,Bold}.ttf` | notofonts/notofonts.github.io (hinted TTF) |
| `NotoSansKR-{Regular,Bold}.ttf` | Static fonts made from the variable font `NotoSansKR[wght].ttf` of google/fonts by fixing the weights 400 and 700 with `fonttools varLib.instancer`. Qt 5's support for variable fonts is incomplete, so static fonts were included |

The 2 Korean fonts are 12 MB and the 4 fixed-width fonts 2.4 MB, so the executable grows by that much.

### Values measured with a large wallet
Measured on regtest with a wallet that had **21,000** unique tokens issued below the root WIDGETCOIN (84 `issueunique` calls of 250 each, taking 27 minutes) and 550 below SERG (21,963 kinds of tokens held, 25,800 wallet transaction rows, Xvfb, the "GUI timing" lines of `-debug=qt` in the debug log). The time until the screen changes was measured by capturing the screen at intervals of 0.08 seconds, so differences below 0.2 seconds cannot be resolved.

| Item | 3.1.0rc7 test build (pre-4.0 numbering) | Now |
|---|---|---|
| From start until the window is drawn | 9.5 s | 8.4~9.2 s |
| Tokens page, sub token window (21,000), Send → Tokens, "+" search window, Transactions | within 0.2 s each | within 0.2 s each |
| One character in the search box | within 0.1 s (filling the list within 5ms) | same |
| Time the GUI thread is blocked by one new block | **2.25 s** + 0.5 s (create/manage lists) | **0.2 s** (up to 0.6 s for the first few blocks afterwards) |
| Of which reading the token balances | 0.6 s (GUI thread) | 0.6 s (worker thread, the GUI does not freeze) |
| Memory (RSS) | 274~278 MiB | 281~305 MiB |

Most of the time that used to be blocked on a new block was not tokens but the **transaction table**. When a block arrives, the transaction table announced that the status of every row had changed (`TransactionTableModel::updateConfirmations`), and the filter proxies re-examined all 25 thousand rows. Now "final" rows with 12 or more confirmations are not announced, only rows that can still change (if such rows are more than 1/4 of the total, everything is announced as before). Transactions shows the rows after filtering and sorting cut into pages (`TransactionPageProxy`). The CSV export writes everything that matches the filter.

Left as it is (core wallet code, not touched):
- At startup the token balances are read once in the GUI thread (0.6 s). The rest of the 8~9 seconds of startup time is reading the wallet file and building the transaction table.
- `GetAllMyTokenBalances` + `GetAllMyLockedTokenBalances` and the metadata lookup take 0.6 s for 21,963 kinds. The `listmytokens` RPC takes 1.76 s.
- Balance calculations such as `CWallet::GetBalance` take 0.07 s per block.
- `issueunique` gets slower as the number of unique tokens grows (one issue of 250 takes about 3 seconds at first and 40 seconds near 20,000).
- The Create Tokens and Manage Tokens pages of `-advancedui` and the selection box of the token control window contain all owner tokens (0.25 s each for the create and manage lists, only when that page is opened).

## Menu bar
On Windows and Linux the menu bar is hidden (macOS keeps using the menu bar at the top of the screen. A window started without a wallet (`-disablewallet`) is unchanged as well). Everything that was in the menus moved to pages, and the actions (QAction) were kept on the window, so the shortcuts work as before.

| Old menu | Now |
|---|---|
| File → Open URI | Settings → Information |
| File → Sign message / Verify message | Sidebar "Sign" |
| File → Sending addresses / Receiving addresses | Sidebar "Address book" (Alt+9) |
| File → Exit | Settings → Information, Ctrl+Q |
| Wallet → Encrypt / Backup / Change passphrase | Settings → Wallet management |
| Wallet → Options | Sidebar "Settings" (Alt+0) |
| Help → Debug window / Command-line options / About / About Qt | Settings → Information (the debug window is Ctrl+Shift+D) |

The Address book and Settings pages are not new screens; the existing dialogs (`AddressBookPage`, `OptionsDialog`) were put inside a page (`embedAsPage()`) and restyled. The Sign page was replaced in the 3.1.0rc9 test build (pre-4.0 numbering) by the newly written `SignHubPage` (`SignVerifyMessageDialog` remains in the code but is not used on screen). Options take effect only after "Save" is clicked, and when an option that needs a restart is changed, a notice appears in the bottom bar. The address chooser window of Send and Sign is the same `AddressBookPage` shown as a window. The remaining dialogs (passphrase entry, address editing, transaction details, coin control, open URI etc.) only got wider margins and lost their button icons; their layout is unchanged.

## Differences from the mock-up and the requests

- The three-way fee choice was connected to the existing fee settings: "Normal" is the wallet's default confirmation target, "Slow" is a target two steps slower than that, "Custom" is a user-specified fee. The other settings appear when "Fee settings" is expanded. The estimated fee in the summary is an estimate for a transaction with 1 input. The fee settings of the Tokens tab are, as before, visible only when the custom fee features option is turned on in the options.
- The details of a transaction in Transactions are opened with a double click. The minimum amount, other types and watch-only filters are expanded with the button to the right of the filter row.
- Clicking "Request payment" on Receive does not open a dialog; the address and QR code appear on the right of the page. The old dialog with the URI and saving the image is opened with "Show" in the request history or with a double click.
- Tokens held by watch-only addresses count as held by the wallet (that is how the wallet's token balance calculation works).
- The amount in the mempool list is the sum of the outputs of that transaction (including change).
- Fractional sizes such as 12.5px were written as 12px in Qt.

## What was and was not verified

- **Wallets with many sub tokens:** verified on regtest by issuing 400 sub tokens and 150 unique tokens below one root (SERG). Only the root appears in the icon row of Send, on the Tokens page, in the Overview list and in the asset filter of Transactions, and the sub token window and the search window open within 0.2 seconds and filter as you type. The time to open a page is unchanged at around 0.2 seconds. The measurement at the scale of 20 thousand is under "Values measured with a large wallet" above.
- **Verified:** the Linux build was started on a virtual display (Xvfb) and all pages were looked at in screenshots in Korean. On Send, enabling of the button according to the address and amount input, the label and lock pop-ups and chips, 2 recipients, Clear all and the fee choice were clicked through, and **AOK and a token were actually sent once each**, with the transactions checked through RPC (regtest). On Receive, creating a request and the QR code and address appearing; on Transactions, the asset list and export to a CSV file; on the Tokens page, the cards; on Overview, the watch-only balance row (`importaddress`) were verified. In the explorer, a wallet transaction, a block height, a transaction ID that does not exist (`txindex` on/off), an address (`addressindex` off) and the mempool list were verified. The Windows build was started under wine to check the fonts, the theme and the fee choice buttons.
- **Address book, Sign, Settings, paging:** on regtest the following were verified with screenshots: the list of receiving addresses; adding and searching a sending address and "Send to this address"; the address chooser window of Send; signing a message with a new address → verification succeeds → editing the message makes verification fail; changing the language option, the restart notice appearing, and "Revert"; pages 1/61 and 61/61 and page 1 after filtering in a wallet with 3,042 transactions; the sidebar list scrolling in a 1280x800 window with `-advancedui`; sending a token on the Tokens tab (balance and mempool checked through RPC). The Windows build was started under wine to look at the Settings, Address book and Sign pages.
- **Performance:** there was a problem where opening the Transactions page for the first time took several seconds in a wallet with 3,000 transactions. The table was measuring rows in order to fit the column widths to the contents (`ResizeToContents`); this was removed and the column widths and row height were fixed. In the same wallet the first opening takes 0.2 seconds (measured under Xvfb). The time before the change was not measured again.
- **Not verified:** real Windows and macOS screens, high-resolution (HiDPI) scaling, address lookups on a node with `addressindex` on, the staking switch with an encrypted wallet, the image put on the clipboard by "Copy QR code" (there was no tool to check it), token selection in a wallet with more than 20 root tokens and the coin control dialog were not checked by eye. The Create Tokens and Manage Tokens pages were only fitted into the shell.
