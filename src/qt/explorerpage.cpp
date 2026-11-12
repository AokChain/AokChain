// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "pastblocks.h"
#include "config/aokchain-config.h"
#endif

#include "explorerpage.h"

#include "addresstablemodel.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "pagewidgets.h"
#include "platformstyle.h"
#include "walletmodel.h"

#include "amount.h"
#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "utiltime.h"
#include "validation.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <univalue.h>

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegExp>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <map>

namespace {

/** Most inputs, outputs, transactions of a block, memory pool entries that are listed. */
const int MAX_ROWS = 40;
const int MAX_BLOCK_TXS = 200;
const int MAX_POOL_ROWS = 30;
const int MAX_ADDRESS_TXS = 50;

/** Call an RPC handler of this node. Returns false when it reports an error. */
bool callRPC(const std::string& method, const std::vector<std::string>& args, UniValue& result, std::string *error = nullptr)
{
    try {
        JSONRPCRequest req;
        req.params = RPCConvertValues(method, args);
        req.strMethod = method;
#ifdef ENABLE_WALLET
        if (!vpwallets.empty()) {
            // as the console does: the first wallet
            QByteArray encodedName = QUrl::toPercentEncoding(QString::fromStdString(vpwallets[0]->GetName()));
            req.URI = "/wallet/" + std::string(encodedName.constData(), encodedName.length());
        }
#endif
        result = tableRPC.execute(req);
        return true;
    } catch (const UniValue& objError) {
        if (error) {
            try {
                *error = find_value(objError, "message").get_str();
            } catch (const std::exception&) {
                *error = objError.write();
            }
        }
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
    } catch (...) {
        if (error)
            *error = "unknown error";
    }
    return false;
}

int64_t intOf(const UniValue& object, const std::string& key, int64_t fallback = 0)
{
    const UniValue v = find_value(object, key);
    if (v.isNum()) {
        try {
            return v.get_int64();
        } catch (const std::exception&) {
        }
    }
    return fallback;
}

bool boolOf(const UniValue& object, const std::string& key)
{
    const UniValue v = find_value(object, key);
    if (v.isBool())
        return v.get_bool();
    return v.isNum() && v.getValStr() != "0";
}

std::string strOf(const UniValue& object, const std::string& key)
{
    const UniValue v = find_value(object, key);
    if (v.isStr() || v.isNum())
        return v.getValStr();
    return std::string();
}

/** Value of an output in the smallest unit. decoderawtransaction gives only the decimal "value". */
CAmount satOf(const UniValue& vout)
{
    const UniValue sat = find_value(vout, "valueSat");
    if (sat.isNum())
        return intOf(vout, "valueSat");
    try {
        return AmountFromValue(find_value(vout, "value"));
    } catch (...) {
        return 0;
    }
}

bool isMine(const std::string& address, std::map<std::string, bool>& cache)
{
    if (address.empty())
        return false;
    std::map<std::string, bool>::const_iterator it = cache.find(address);
    if (it != cache.end())
        return it->second;
    UniValue info;
    bool mine = false;
    if (callRPC("validateaddress", {address}, info) && info.isObject()) {
        const UniValue v = find_value(info, "ismine");
        mine = v.isBool() ? v.get_bool() : (v.isNum() && v.get_int() != 0);
    }
    cache[address] = mine;
    return mine;
}

/** What one output of a transaction transfers, for the lists of inputs and outputs. */
UniValue describeOutput(const UniValue& vout, std::map<std::string, bool>& mineCache)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("value", strOf(vout, "value"));
    entry.pushKV("sat", (int64_t)satOf(vout));
    const UniValue script = find_value(vout, "scriptPubKey");
    std::string address;
    if (script.isObject()) {
        entry.pushKV("type", strOf(script, "type"));
        const UniValue addresses = find_value(script, "addresses");
        if (addresses.isArray() && addresses.size() > 0 && addresses[0].isStr())
            address = addresses[0].get_str();
        const UniValue token = find_value(script, "token");
        if (token.isObject()) {
            entry.pushKV("token", strOf(token, "name"));
            entry.pushKV("tokenAmount", strOf(token, "amount"));
        }
    }
    entry.pushKV("address", address);
    entry.pushKV("mine", isMine(address, mineCache));
    return entry;
}

/** A transaction in the verbose form of getrawtransaction; from the wallet when the node has no index for it. */
bool fetchTransaction(const std::string& txid, UniValue& tx, bool *inWallet = nullptr)
{
    if (inWallet)
        *inWallet = false;
    bool found = callRPC("getrawtransaction", {txid, "1"}, tx) && tx.isObject();
#ifdef ENABLE_WALLET
    if (!found || inWallet) {
        UniValue wtx;
        if (!vpwallets.empty() && callRPC("gettransaction", {txid}, wtx) && wtx.isObject()) {
            if (inWallet)
                *inWallet = true;
            if (!found) {
                UniValue decoded;
                if (callRPC("decoderawtransaction", {strOf(wtx, "hex")}, decoded) && decoded.isObject()) {
                    tx = decoded;
                    tx.pushKV("hex", strOf(wtx, "hex"));
                    tx.pushKV("confirmations", intOf(wtx, "confirmations"));
                    tx.pushKV("time", intOf(wtx, "blocktime", intOf(wtx, "time")));
                    const std::string blockhash = strOf(wtx, "blockhash");
                    if (!blockhash.empty()) {
                        tx.pushKV("blockhash", blockhash);
                        UniValue header;
                        if (callRPC("getblockheader", {blockhash}, header) && header.isObject())
                            tx.pushKV("height", intOf(header, "height", -1));
                    }
                    found = true;
                }
            }
        }
    }
#endif
    return found;
}

UniValue lookupTransaction(const std::string& txid, bool& found)
{
    UniValue result(UniValue::VOBJ);
    UniValue tx;
    bool inWallet = false;
    found = fetchTransaction(txid, tx, &inWallet);
    if (!found)
        return result;

    std::map<std::string, bool> mineCache;
    std::map<std::string, UniValue> previous;
    result.pushKV("kind", "tx");
    result.pushKV("txid", strOf(tx, "txid"));
    result.pushKV("confirmations", intOf(tx, "confirmations"));
    result.pushKV("height", intOf(tx, "height", -1));
    result.pushKV("blockhash", strOf(tx, "blockhash"));
    result.pushKV("time", intOf(tx, "time", intOf(tx, "timestamp")));
    result.pushKV("size", intOf(tx, "size"));
    result.pushKV("hex", strOf(tx, "hex"));
    result.pushKV("mine", inWallet);

    const UniValue vin = find_value(tx, "vin");
    const UniValue vout = find_value(tx, "vout");
    CAmount totalIn = 0, totalOut = 0;
    bool allInputsKnown = true, coinbase = false;

    UniValue inputs(UniValue::VARR);
    if (vin.isArray()) {
        for (size_t i = 0; i < vin.size(); ++i) {
            const UniValue& in = vin[i];
            UniValue entry(UniValue::VOBJ);
            if (!find_value(in, "coinbase").isNull()) {
                coinbase = true;
                entry.pushKV("coinbase", true);
            } else {
                const std::string prevTxid = strOf(in, "txid");
                const int64_t n = intOf(in, "vout", -1);
                if (!previous.count(prevTxid)) {
                    UniValue prev;
                    // the value of an input is in the transaction it spends
                    if (i >= (size_t)MAX_ROWS * 4 || !fetchTransaction(prevTxid, prev))
                        prev = UniValue(UniValue::VNULL);
                    previous[prevTxid] = prev;
                }
                const UniValue& prev = previous[prevTxid];
                const UniValue prevOut = prev.isObject() ? find_value(prev, "vout") : NullUniValue;
                if (prevOut.isArray() && n >= 0 && (size_t)n < prevOut.size()) {
                    entry = describeOutput(prevOut[n], mineCache);
                    totalIn += satOf(prevOut[n]);
                } else {
                    entry.pushKV("unknown", true);
                    allInputsKnown = false;
                }
                entry.pushKV("txid", prevTxid);
                entry.pushKV("n", n);
            }
            if (i < (size_t)MAX_ROWS)
                inputs.push_back(entry);
        }
        result.pushKV("ninputs", (int64_t)vin.size());
    }
    result.pushKV("inputs", inputs);

    UniValue outputs(UniValue::VARR);
    if (vout.isArray()) {
        for (size_t i = 0; i < vout.size(); ++i) {
            totalOut += satOf(vout[i]);
            if (i < (size_t)MAX_ROWS)
                outputs.push_back(describeOutput(vout[i], mineCache));
        }
        result.pushKV("noutputs", (int64_t)vout.size());
    }
    result.pushKV("outputs", outputs);

    if (!coinbase && allInputsKnown) {
        if (totalIn >= totalOut)
            result.pushKV("fee", ValueFromAmount(totalIn - totalOut).getValStr());
        else // a coinstake pays out more than it spends
            result.pushKV("reward", ValueFromAmount(totalOut - totalIn).getValStr());
    }
    result.pushKV("coinbase", coinbase);
    return result;
}

UniValue lookupBlock(const std::string& hash, bool& found)
{
    UniValue result(UniValue::VOBJ);
    UniValue block;
    found = callRPC("getblock", {hash}, block) && block.isObject();
    if (!found)
        return result;
    result.pushKV("kind", "block");
    for (const char *key : {"hash", "previousblockhash", "nextblockhash", "flags"})
        result.pushKV(key, strOf(block, key));
    for (const char *key : {"confirmations", "height", "time", "size"})
        result.pushKV(key, intOf(block, key));
    const UniValue txs = find_value(block, "tx");
    UniValue list(UniValue::VARR);
    if (txs.isArray()) {
        for (size_t i = 0; i < txs.size() && i < (size_t)MAX_BLOCK_TXS; ++i)
            list.push_back(txs[i]);
        result.pushKV("ntx", (int64_t)txs.size());
    }
    result.pushKV("tx", list);
    return result;
}

UniValue lookupAddress(const std::string& address)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("kind", "address");
    result.pushKV("address", address);
    UniValue info;
    if (callRPC("validateaddress", {address}, info) && info.isObject()) {
        for (const char *key : {"ismine", "iswatchonly"}) {
            const UniValue v = find_value(info, key);
            result.pushKV(key, v.isBool() ? v.get_bool() : (v.isNum() && v.get_int() != 0));
        }
        result.pushKV("label", strOf(info, "account"));
    }
    result.pushKV("indexed", fAddressIndex);
    if (fAddressIndex) {
        UniValue balance;
        std::string error;
        if (callRPC("getaddressbalance", {address}, balance, &error) && balance.isObject()) {
            result.pushKV("balance", ValueFromAmount(intOf(balance, "balance")).getValStr());
            result.pushKV("received", ValueFromAmount(intOf(balance, "received")).getValStr());
        } else {
            result.pushKV("error", error);
        }
        UniValue txids;
        if (callRPC("getaddresstxids", {address}, txids) && txids.isArray()) {
            result.pushKV("ntx", (int64_t)txids.size());
            UniValue list(UniValue::VARR);
            // newest first
            for (size_t i = txids.size(); i > 0 && list.size() < (size_t)MAX_ADDRESS_TXS; --i)
                list.push_back(txids[i - 1]);
            result.pushKV("txids", list);
        }
    }
    return result;
}

UniValue errorResult(const std::string& kind, const std::string& message = std::string())
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("kind", kind);
    result.pushKV("message", message);
    result.pushKV("txindex", fTxIndex);
    return result;
}

} // namespace

bool NodeRPC::call(const std::string& method, const std::vector<std::string>& args, UniValue& result, std::string *error)
{
    return callRPC(method, args, result, error);
}

void ExplorerWorker::lookup(const QString& query)
{
    const QString q = query.trimmed();
    UniValue result;
    bool found = false;
    if (QRegExp("[0-9]{1,9}").exactMatch(q)) {
        UniValue hash;
        std::string error;
        if (callRPC("getblockhash", {q.toStdString()}, hash, &error) && hash.isStr())
            result = lookupBlock(hash.get_str(), found);
        if (!found)
            result = errorResult("noblock", error);
    } else if (QRegExp("[0-9a-fA-F]{64}").exactMatch(q)) {
        const std::string id = q.toLower().toStdString();
        result = lookupTransaction(id, found);
        if (!found)
            result = lookupBlock(id, found);
        if (!found)
            result = errorResult("notx");
    } else if (IsValidDestinationString(q.toStdString())) {
        result = lookupAddress(q.toStdString());
    } else {
        result = errorResult("invalid");
    }
    Q_EMIT lookupDone(query, QString::fromStdString(result.write()));
}

static UniValue readChainStatus()
{
    UniValue result(UniValue::VOBJ);
    UniValue value;
    if (callRPC("getblockchaininfo", {}, value) && value.isObject())
        result.pushKV("height", intOf(value, "blocks"));
    if (callRPC("getstakinginfo", {}, value) && value.isObject()) {
        result.pushKV("difficulty", strOf(value, "difficulty"));
        result.pushKV("netweight", strOf(value, "netstakeweight"));
    }
    if (callRPC("getconnectioncount", {}, value) && value.isNum())
        result.pushKV("peers", value.get_int64());
    if (callRPC("getmempoolinfo", {}, value) && value.isObject()) {
        result.pushKV("poolsize", intOf(value, "size"));
        result.pushKV("poolbytes", intOf(value, "bytes"));
        result.pushKV("poolminfee", strOf(value, "mempoolminfee"));
    }
    return result;
}

void ExplorerWorker::chainStatus()
{
    Q_EMIT statusDone(QString::fromStdString(readChainStatus().write()));
}

void ExplorerPage::fillChainStats(StatsStrip *strip, const QString& json)
{
    UniValue result;
    if (!result.read(json.toStdString()) || !result.isObject())
        return;
    strip->setValue(0, GUIUtil::withThousands(QString::number(intOf(result, "height"))));
    bool ok = false;
    const double difficulty = QString::fromStdString(strOf(result, "difficulty")).toDouble(&ok);
    if (ok)
        strip->setValue(1, difficulty >= 1000 ? GUIUtil::withThousands(QString::number(difficulty, 'f', 2)) : QString::number(difficulty, 'g', 6));
    const double weight = QString::fromStdString(strOf(result, "netweight")).toDouble(&ok);
    if (ok)
        strip->setValue(2, GUIUtil::withThousands(QString::number(weight / COIN, 'f', 0)), "AOK");
    strip->setValue(3, QString::number(intOf(result, "peers")));
    strip->setValue(4, GUIUtil::withThousands(QString::number(intOf(result, "poolsize"))));
}

void ExplorerWorker::refresh()
{
    UniValue result = readChainStatus();
    UniValue value;
    if (callRPC("getnetworkinfo", {}, value) && value.isObject())
        result.pushKV("relayfee", strOf(value, "relayfee"));

    UniValue entries(UniValue::VARR);
    if (callRPC("getrawmempool", {"true"}, value) && value.isObject()) {
        // newest first
        std::vector<std::pair<int64_t, std::string>> order;
        const std::vector<std::string>& keys = value.getKeys();
        for (const std::string& txid : keys)
            order.push_back(std::make_pair(-intOf(value[txid], "time"), txid));
        std::sort(order.begin(), order.end());
        for (size_t i = 0; i < order.size() && i < (size_t)MAX_POOL_ROWS; ++i) {
            const std::string& txid = order[i].second;
            const UniValue& info = value[txid];
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("txid", txid);
            entry.pushKV("time", intOf(info, "time"));
            entry.pushKV("size", intOf(info, "size"));
            entry.pushKV("fee", strOf(info, "fee"));
            UniValue tx;
            CAmount total = 0;
            if (callRPC("getrawtransaction", {txid, "1"}, tx) && tx.isObject()) {
                const UniValue vout = find_value(tx, "vout");
                for (size_t n = 0; vout.isArray() && n < vout.size(); ++n)
                    total += satOf(vout[n]);
            }
            entry.pushKV("amount", ValueFromAmount(total).getValStr());
            bool mine = false;
#ifdef ENABLE_WALLET
            UniValue wtx;
            mine = !vpwallets.empty() && callRPC("gettransaction", {txid}, wtx);
#endif
            entry.pushKV("mine", mine);
            entries.push_back(entry);
        }
    }
    result.pushKV("entries", entries);
    result.pushKV("now", GetTime());
    Q_EMIT refreshDone(QString::fromStdString(result.write()));
}

static QLabel *caption(const QString& text)
{
    QLabel *label = new QLabel(text);
    label->setProperty("role", "caption");
    label->setFont(GUIUtil::appFont(12));
    return label;
}

static QFrame *line(bool vertical = false)
{
    QFrame *frame = new QFrame();
    frame->setProperty("role", vertical ? "vline" : "hline");
    if (vertical)
        frame->setFixedWidth(1);
    else
        frame->setFixedHeight(1);
    return frame;
}

static QString amountText(const std::string& value)
{
    return GUIUtil::withThousands(QString::fromStdString(value));
}

static QString bytesText(qint64 bytes)
{
    return ExplorerPage::tr("%1 bytes").arg(GUIUtil::withThousands(QString::number(bytes)));
}

ExplorerPage::ExplorerPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    clientModel(nullptr),
    walletModel(nullptr),
    refreshPending(false)
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    headerNote = caption(tr("Looked up on the node of this computer. Nothing is sent to an outside server."));

    QVBoxLayout *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(16);

    // search
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->setSpacing(10);
    searchEdit = new QLineEdit();
    searchEdit->setProperty("look", "search");
    searchEdit->setFont(GUIUtil::monoFont(13));
    searchEdit->setPlaceholderText(tr("Transaction ID, address, block height or block hash"));
    searchEdit->setAccessibleName(searchEdit->placeholderText());
    searchEdit->setFixedHeight(48);
    searchEdit->addAction(QIcon(GUIUtil::lineIcon("search", 16, theme.textSecondary, 2.2)), QLineEdit::LeadingPosition);
    searchEdit->setClearButtonEnabled(true);
    searchButton = new QPushButton(tr("Look up"));
    searchButton->setProperty("primary", true);
    searchButton->setProperty("look", "large");
    searchButton->setFont(GUIUtil::appFont(15, QFont::DemiBold));
    searchButton->setFixedHeight(48);
    searchButton->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(searchEdit, 1);
    searchRow->addWidget(searchButton);
    pageLayout->addLayout(searchRow);
    connect(searchButton, SIGNAL(clicked()), this, SLOT(searchClicked()));
    connect(searchEdit, SIGNAL(returnPressed()), this, SLOT(searchClicked()));

    // chain status
    chainStats = new StatsStrip();
    chainStats->addCell(tr("Block height"));
    chainStats->addCell(tr("Difficulty"));
    chainStats->addCell(tr("Network stake weight"));
    chainStats->addCell(tr("Connected peers"));
    pageLayout->addWidget(chainStats);

    QHBoxLayout *cards = new QHBoxLayout();
    cards->setSpacing(20);

    // result
    QFrame *resultCard = new QFrame();
    resultCard->setProperty("role", "card");
    QVBoxLayout *resultOuter = new QVBoxLayout(resultCard);
    resultOuter->setContentsMargins(22, 18, 22, 14);
    resultOuter->setSpacing(10);
    QHBoxLayout *resultHeader = new QHBoxLayout();
    resultTitle = new QLabel(tr("Result"));
    resultTitle->setProperty("role", "title");
    resultTitle->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    resultBadge = new QLabel();
    resultBadge->setFont(GUIUtil::appFont(12, QFont::Medium));
    resultBadge->hide();
    resultHeader->addWidget(resultTitle);
    resultHeader->addStretch();
    resultHeader->addWidget(resultBadge);
    resultOuter->addLayout(resultHeader);
    QScrollArea *resultScroll = new QScrollArea();
    resultScroll->setWidgetResizable(true);
    resultScroll->setFrameShape(QFrame::NoFrame);
    resultScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *resultBody = new QWidget();
    resultBody->setObjectName("explorerResult");
    resultBody->setStyleSheet("QWidget#explorerResult { background: transparent; }");
    resultLayout = new QVBoxLayout(resultBody);
    resultLayout->setContentsMargins(0, 0, 6, 0);
    resultLayout->setSpacing(0);
    resultScroll->setWidget(resultBody);
    resultScroll->viewport()->setObjectName("explorerResultView");
    resultScroll->viewport()->setStyleSheet("QWidget#explorerResultView { background: transparent; }");
    resultOuter->addWidget(resultScroll, 1);
    resultButtons = new QWidget();
    QHBoxLayout *buttonLayout = new QHBoxLayout(resultButtons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    QPushButton *copyButton = new QPushButton(tr("Copy ID"));
    copyButton->setProperty("look", "soft");
    copyButton->setCursor(Qt::PointingHandCursor);
    hexButton = new QPushButton(tr("Show raw (hex)"));
    hexButton->setProperty("look", "seg");
    hexButton->setCursor(Qt::PointingHandCursor);
    buttonLayout->addWidget(copyButton);
    buttonLayout->addWidget(hexButton);
    buttonLayout->addStretch();
    resultButtons->hide();
    resultOuter->addWidget(resultButtons);
    connect(copyButton, SIGNAL(clicked()), this, SLOT(copyShownId()));
    connect(hexButton, SIGNAL(clicked()), this, SLOT(showRawHex()));
    cards->addWidget(resultCard, 3);

    // memory pool
    QFrame *poolCard = new QFrame();
    poolCard->setProperty("role", "card");
    QVBoxLayout *poolOuter = new QVBoxLayout(poolCard);
    poolOuter->setContentsMargins(22, 18, 22, 14);
    poolOuter->setSpacing(12);
    QHBoxLayout *poolHeader = new QHBoxLayout();
    QLabel *poolTitle = new QLabel(tr("Memory pool"));
    poolTitle->setProperty("role", "title");
    poolTitle->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    poolHeader->addWidget(poolTitle);
    poolHeader->addStretch();
    poolHeader->addWidget(caption(tr("Transactions waiting to be confirmed")));
    poolOuter->addLayout(poolHeader);
    QHBoxLayout *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    auto addTile = [&](const QString& name, AmountLabel *&value) {
        QFrame *tile = new QFrame();
        tile->setProperty("role", "tile");
        QVBoxLayout *layout = new QVBoxLayout(tile);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(2);
        layout->addWidget(caption(name));
        value = new AmountLabel(18, QFont::DemiBold);
        value->setAmount(QString::fromUtf8("–"));
        layout->addWidget(value);
        tiles->addWidget(tile, 1);
    };
    addTile(tr("Waiting"), poolCount);
    addTile(tr("Size"), poolSize);
    addTile(tr("Minimum fee"), poolMinFee);
    poolOuter->addLayout(tiles);
    QScrollArea *poolScroll = new QScrollArea();
    poolScroll->setWidgetResizable(true);
    poolScroll->setFrameShape(QFrame::NoFrame);
    poolScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *poolBody = new QWidget();
    poolBody->setObjectName("explorerPool");
    poolBody->setStyleSheet("QWidget#explorerPool { background: transparent; }");
    poolLayout = new QVBoxLayout(poolBody);
    poolLayout->setContentsMargins(0, 0, 6, 0);
    poolLayout->setSpacing(0);
    poolScroll->setWidget(poolBody);
    poolScroll->viewport()->setObjectName("explorerPoolView");
    poolScroll->viewport()->setStyleSheet("QWidget#explorerPoolView { background: transparent; }");
    poolOuter->addWidget(poolScroll, 1);
    poolOuter->addWidget(line());
    const int spacing = Params().GetConsensus().nTargetSpacing;
    poolFooter = caption(tr("Expected to be included in the next block. Blocks are about %1 apart.").arg(GUIUtil::formatDurationStr(spacing)));
    poolFooter->setWordWrap(true);
    poolOuter->addWidget(poolFooter);
    cards->addWidget(poolCard, 2);
    pageLayout->addLayout(cards, 1);

    showMessage(tr("Result"), tr("Enter a transaction ID, an address, a block height or a block hash above. Transaction IDs and addresses shown here can be clicked to look them up."), false);

    // the look-ups run on a worker thread so that the window stays responsive
    ExplorerWorker *worker = new ExplorerWorker();
    worker->moveToThread(&thread);
    connect(&thread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(this, SIGNAL(requestLookup(QString)), worker, SLOT(lookup(QString)));
    connect(this, SIGNAL(requestRefresh()), worker, SLOT(refresh()));
    connect(worker, SIGNAL(lookupDone(QString,QString)), this, SLOT(lookupDone(QString,QString)));
    connect(worker, SIGNAL(refreshDone(QString)), this, SLOT(refreshDone(QString)));
    thread.start();

    timer = new QTimer(this);
    timer->setInterval(5000);
    connect(timer, SIGNAL(timeout()), this, SLOT(refreshNow()));
}

ExplorerPage::~ExplorerPage()
{
    thread.quit();
    thread.wait();
    if (headerNote && !headerNote->parent())
        delete headerNote;
}

void ExplorerPage::setClientModel(ClientModel *_clientModel)
{
    clientModel = _clientModel;
    if (clientModel) {
        // a new block changes the confirmations and the memory pool
        connect(clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(refreshNow()));
    }
}

void ExplorerPage::setWalletModel(WalletModel *_walletModel)
{
    walletModel = _walletModel;
}

void ExplorerPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshNow();
    timer->start();
}

void ExplorerPage::hideEvent(QHideEvent *event)
{
    timer->stop();
    QWidget::hideEvent(event);
}

void ExplorerPage::refreshNow()
{
    // one request at a time; nothing while the page is not shown
    if (!isVisible() || refreshPending || !clientModel)
        return;
    refreshPending = true;
    Q_EMIT requestRefresh();
}

void ExplorerPage::focusSearch()
{
    searchEdit->setFocus();
    searchEdit->selectAll();
}

void ExplorerPage::searchClicked()
{
    lookup(searchEdit->text());
}

void ExplorerPage::lookup(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return;
    searchEdit->setText(q);
    pendingQuery = q;
    searchButton->setEnabled(false);
    Q_EMIT requestLookup(q);
}

void ExplorerPage::linkActivated(const QString& link)
{
    if (link.startsWith("q:"))
        lookup(link.mid(2));
    else if (link.startsWith("send:"))
        Q_EMIT sendToAddress(link.mid(5));
}

void ExplorerPage::copyShownId()
{
    if (!shownId.isEmpty())
        GUIUtil::setClipboard(shownId);
}

void ExplorerPage::showRawHex()
{
    if (shownHex.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Raw transaction"));
    dialog.resize(640, 360);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QPlainTextEdit *text = new QPlainTextEdit(shownHex);
    text->setReadOnly(true);
    text->setFont(GUIUtil::monoFont(12));
    text->setWordWrapMode(QTextOption::WrapAnywhere);
    layout->addWidget(text);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, SIGNAL(rejected()), &dialog, SLOT(reject()));
    layout->addWidget(box);
    dialog.exec();
}

void ExplorerPage::clearLayout(QVBoxLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void ExplorerPage::setBadge(const QString& text, bool ok)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    resultBadge->setText(text);
    resultBadge->setStyleSheet(QString("QLabel { color: %1; background-color: %2; border-radius: 4px; padding: 4px 10px; }")
        .arg((ok ? t.ok : t.negative).name(), (ok ? t.okSoft : t.negativeSoft).name()));
    resultBadge->setVisible(!text.isEmpty());
}

QLabel *ExplorerPage::monoLabel(const QString& text, int px, bool bold)
{
    QLabel *label = new QLabel(text);
    label->setFont(GUIUtil::monoFont(px, bold ? QFont::Medium : QFont::Normal));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QWidget *ExplorerPage::amountValue(const QString& amount, const QString& unit, int px)
{
    AmountLabel *label = new AmountLabel(px, QFont::Medium);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setAmount(amount, unit);
    return label;
}

QLabel *ExplorerPage::linkLabel(const QString& text, const QString& target, const QString& toolTip, int px)
{
    QLabel *label = new QLabel();
    label->setFont(GUIUtil::monoFont(px, QFont::Medium));
    label->setTextFormat(Qt::RichText);
    label->setText(QString("<a href=\"q:%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(GUIUtil::HtmlEscape(target), GUIUtil::theme().accent.name(), GUIUtil::HtmlEscape(text)));
    if (!toolTip.isEmpty())
        label->setToolTip(toolTip);
    label->setCursor(Qt::PointingHandCursor);
    connect(label, SIGNAL(linkActivated(QString)), this, SLOT(linkActivated(QString)));
    return label;
}

QWidget *ExplorerPage::keyValueRow(const QString& key, QWidget *value)
{
    QWidget *row = new QWidget();
    QVBoxLayout *outer = new QVBoxLayout(row);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(line());
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setContentsMargins(0, 9, 0, 9);
    layout->setSpacing(16);
    layout->addWidget(caption(key));
    layout->addStretch();
    layout->addWidget(value);
    outer->addLayout(layout);
    return row;
}

void ExplorerPage::showMessage(const QString& title, const QString& text, bool notice)
{
    clearLayout(resultLayout);
    resultTitle->setText(title);
    setBadge(QString(), true);
    resultButtons->hide();
    shownId.clear();
    shownHex.clear();
    QLabel *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (notice) {
        label->setProperty("role", "notice");
    } else {
        label->setProperty("role", "secondary");
    }
    resultLayout->addWidget(label);
    resultLayout->addStretch(1);
}

/** One input or output: abbreviated address (the full one in a tool tip) and what it carries. */
QWidget *ExplorerPage::transferRow(const UniValue& entry)
{
    QWidget *row = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 3, 0, 3);
    layout->setSpacing(8);

    const QString address = QString::fromStdString(strOf(entry, "address"));
    const bool mine = boolOf(entry, "mine");
    QWidget *left;
    if (boolOf(entry, "coinbase")) {
        left = caption(tr("Newly generated coins"));
    } else if (boolOf(entry, "unknown")) {
        const QString txid = QString::fromStdString(strOf(entry, "txid"));
        left = linkLabel(GUIUtil::shortHash(txid, 8, 6) + ":" + QString::fromStdString(strOf(entry, "n")), txid, GUIUtil::monoToolTip(txid, tr("Spends an output of this transaction")));
    } else if (!address.isEmpty()) {
        left = linkLabel(GUIUtil::shortAddress(address), address,
                         GUIUtil::monoToolTip(address, mine ? tr("An address of this wallet") : QString()));
    } else {
        const QString type = QString::fromStdString(strOf(entry, "type"));
        left = caption(type == "nulldata" ? tr("Data (no address)") : (type.isEmpty() ? tr("Unknown") : type));
    }
    layout->addWidget(left);
    if (mine) {
        QLabel *chip = new QLabel(tr("Mine"));
        chip->setFont(GUIUtil::appFont(11, QFont::Medium));
        chip->setStyleSheet(QString("QLabel { color: %1; background-color: %2; border-radius: 3px; padding: 1px 7px; }")
            .arg(GUIUtil::theme().chipText.name(), GUIUtil::theme().chip.name()));
        layout->addWidget(chip);
    }
    layout->addStretch();

    const std::string token = strOf(entry, "token");
    if (!token.empty())
    {
        // a long token name is shortened in the middle; the tool tip has it in full
        const QString name = QString::fromStdString(token);
        QWidget *value = amountValue(amountText(strOf(entry, "tokenAmount")), QFontMetrics(GUIUtil::appFont(11)).elidedText(name, Qt::ElideMiddle, 200), 14);
        value->setToolTip(name);
        layout->addWidget(value);
    }
    else if (!boolOf(entry, "unknown") && !boolOf(entry, "coinbase"))
        layout->addWidget(amountValue(amountText(strOf(entry, "value")), QString(), 14));
    return row;
}

void ExplorerPage::showTransaction(const UniValue& result)
{
    clearLayout(resultLayout);
    resultTitle->setText(tr("Transaction"));
    const qint64 confirmations = intOf(result, "confirmations");
    if (confirmations > 0)
        setBadge(tr("Confirmed · %1 confirmations").arg(confirmations), true);
    else
        setBadge(tr("Unconfirmed · in the memory pool"), false);

    shownId = QString::fromStdString(strOf(result, "txid"));
    shownHex = QString::fromStdString(strOf(result, "hex"));
    hexButton->setVisible(!shownHex.isEmpty());
    resultButtons->show();

    QLabel *id = monoLabel(shownId, 12);
    id->setWordWrap(true);
    id->setStyleSheet(QString("QLabel { color: %1; }").arg(GUIUtil::theme().label.name()));
    resultLayout->addWidget(id);
    resultLayout->addSpacing(10);

    const qint64 height = intOf(result, "height", -1);
    if (height >= 0)
        resultLayout->addWidget(keyValueRow(tr("Block"), linkLabel(GUIUtil::withThousands(QString::number(height)), QString::number(height), QString(), 13)));
    else
        resultLayout->addWidget(keyValueRow(tr("Block"), caption(tr("Not in a block yet"))));
    const qint64 time = intOf(result, "time");
    if (time > 0)
        resultLayout->addWidget(keyValueRow(tr("Time"), monoLabel(GUIUtil::isoDateTime(QDateTime::fromTime_t(time), true))));
    resultLayout->addWidget(keyValueRow(tr("Size"), monoLabel(bytesText(intOf(result, "size")))));
    const std::string fee = strOf(result, "fee"), reward = strOf(result, "reward");
    if (!fee.empty())
        resultLayout->addWidget(keyValueRow(tr("Fee"), amountValue(amountText(fee), "AOK")));
    else if (!reward.empty())
        resultLayout->addWidget(keyValueRow(tr("Staking reward"), amountValue(amountText(reward), "AOK")));
    else if (boolOf(result, "coinbase"))
        resultLayout->addWidget(keyValueRow(tr("Fee"), caption(tr("None (generated coins)"))));
    else
        resultLayout->addWidget(keyValueRow(tr("Fee"), caption(tr("Unknown (an input could not be looked up)"))));
    if (boolOf(result, "mine"))
        resultLayout->addWidget(keyValueRow(tr("Wallet"), caption(tr("A transaction of this wallet"))));
    resultLayout->addWidget(line());
    resultLayout->addSpacing(12);

    // Inputs and outputs side by side. A token transfer names the token after the amount, which
    // does not fit into half of the card: such a transaction lists them one below the other.
    bool wide = false;
    for (const char *key : {"inputs", "outputs"}) {
        const UniValue list = find_value(result, key);
        for (size_t i = 0; list.isArray() && i < list.size(); ++i)
            wide = wide || !strOf(list[i], "token").empty();
    }
    QWidget *columns = new QWidget();
    QBoxLayout *columnLayout = new QBoxLayout(wide ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, columns);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(16);
    auto addColumn = [&](const QString& heading, const UniValue& entries, qint64 total) {
        QVBoxLayout *column = new QVBoxLayout();
        column->setSpacing(0);
        QLabel *head = caption(heading);
        head->setFont(GUIUtil::appFont(12, QFont::Medium));
        column->addWidget(head);
        column->addSpacing(6);
        for (size_t i = 0; entries.isArray() && i < entries.size(); ++i)
            column->addWidget(transferRow(entries[i]));
        if (entries.isArray() && total > (qint64)entries.size())
            column->addWidget(caption(tr("and %1 more").arg(total - (qint64)entries.size())));
        column->addStretch();
        columnLayout->addLayout(column, 1);
    };
    const qint64 ninputs = intOf(result, "ninputs"), noutputs = intOf(result, "noutputs");
    addColumn(tr("%1 inputs").arg(ninputs), find_value(result, "inputs"), ninputs);
    addColumn(tr("%1 outputs").arg(noutputs), find_value(result, "outputs"), noutputs);
    resultLayout->addWidget(columns);
    resultLayout->addStretch(1);
}

void ExplorerPage::showBlock(const UniValue& result)
{
    clearLayout(resultLayout);
    resultTitle->setText(tr("Block"));
    const qint64 confirmations = intOf(result, "confirmations");
    if (confirmations > 0)
        setBadge(tr("%1 confirmations").arg(confirmations), true);
    else
        setBadge(tr("Not in the active chain"), false);

    shownId = QString::fromStdString(strOf(result, "hash"));
    shownHex.clear();
    hexButton->hide();
    resultButtons->show();

    QLabel *id = monoLabel(shownId, 12);
    id->setWordWrap(true);
    id->setStyleSheet(QString("QLabel { color: %1; }").arg(GUIUtil::theme().label.name()));
    resultLayout->addWidget(id);
    resultLayout->addSpacing(10);

    const qint64 height = intOf(result, "height");
    resultLayout->addWidget(keyValueRow(tr("Height"), monoLabel(GUIUtil::withThousands(QString::number(height)), 13, true)));
    resultLayout->addWidget(keyValueRow(tr("Time"), monoLabel(GUIUtil::isoDateTime(QDateTime::fromTime_t(intOf(result, "time")), true))));
    const qint64 ntx = intOf(result, "ntx");
    resultLayout->addWidget(keyValueRow(tr("Transactions"), monoLabel(GUIUtil::withThousands(QString::number(ntx)))));
    resultLayout->addWidget(keyValueRow(tr("Size"), monoLabel(bytesText(intOf(result, "size")))));
    const QString flags = QString::fromStdString(strOf(result, "flags"));
    resultLayout->addWidget(keyValueRow(tr("Proof"), caption(flags.contains("stake") ? tr("Proof of stake") : (flags.contains("work") ? tr("Proof of work") : flags))));
    const QString previous = QString::fromStdString(strOf(result, "previousblockhash"));
    if (!previous.isEmpty())
        resultLayout->addWidget(keyValueRow(tr("Previous block"), linkLabel(GUIUtil::shortHash(previous), previous, GUIUtil::monoToolTip(previous))));
    const QString next = QString::fromStdString(strOf(result, "nextblockhash"));
    if (!next.isEmpty())
        resultLayout->addWidget(keyValueRow(tr("Next block"), linkLabel(GUIUtil::shortHash(next), next, GUIUtil::monoToolTip(next))));
    resultLayout->addWidget(line());
    resultLayout->addSpacing(12);

    QLabel *head = caption(tr("%1 transactions").arg(ntx));
    head->setFont(GUIUtil::appFont(12, QFont::Medium));
    resultLayout->addWidget(head);
    resultLayout->addSpacing(6);
    const UniValue txs = find_value(result, "tx");
    for (size_t i = 0; txs.isArray() && i < txs.size(); ++i) {
        const QString txid = QString::fromStdString(txs[i].getValStr());
        QLabel *label = linkLabel(txid, txid);
        label->setContentsMargins(0, 3, 0, 3);
        resultLayout->addWidget(label);
    }
    if (txs.isArray() && ntx > (qint64)txs.size())
        resultLayout->addWidget(caption(tr("and %1 more").arg(ntx - (qint64)txs.size())));
    resultLayout->addStretch(1);
}

void ExplorerPage::showAddress(const UniValue& result)
{
    clearLayout(resultLayout);
    resultTitle->setText(tr("Address"));
    setBadge(QString(), true);

    shownId = QString::fromStdString(strOf(result, "address"));
    shownHex.clear();
    hexButton->hide();
    resultButtons->show();

    QLabel *id = monoLabel(shownId, 13, true);
    resultLayout->addWidget(id);
    resultLayout->addSpacing(10);

    QString owner = tr("Not an address of this wallet");
    if (boolOf(result, "ismine"))
        owner = tr("An address of this wallet");
    else if (boolOf(result, "iswatchonly"))
        owner = tr("A watch-only address of this wallet");
    resultLayout->addWidget(keyValueRow(tr("Wallet"), caption(owner)));
    QString label = QString::fromStdString(strOf(result, "label"));
    if (walletModel && walletModel->getAddressTableModel()) {
        const QString bookLabel = walletModel->getAddressTableModel()->labelForAddress(shownId);
        if (!bookLabel.isEmpty())
            label = bookLabel;
    }
    if (!label.isEmpty())
        resultLayout->addWidget(keyValueRow(tr("Label"), new QLabel(label)));
    {
        QLabel *send = new QLabel();
        send->setTextFormat(Qt::RichText);
        send->setText(QString("<a href=\"send:%1\" style=\"color:%2; text-decoration:none; font-weight:600;\">%3</a>")
            .arg(GUIUtil::HtmlEscape(shownId), GUIUtil::theme().accent.name(), GUIUtil::HtmlEscape(tr("Send to this address"))));
        send->setCursor(Qt::PointingHandCursor);
        connect(send, SIGNAL(linkActivated(QString)), this, SLOT(linkActivated(QString)));
        resultLayout->addWidget(keyValueRow(tr("Send"), send));
    }

    if (!boolOf(result, "indexed")) {
        resultLayout->addWidget(line());
        resultLayout->addSpacing(12);
        QLabel *notice = new QLabel(tr("The balance and the transactions of an address can only be shown when the node keeps an address index. "
                                       "To turn it on, add the line addressindex=1 to aokchain.conf and start the wallet once with the option -reindex. "
                                       "Rebuilding the index takes a while."));
        notice->setProperty("role", "notice");
        notice->setWordWrap(true);
        notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
        resultLayout->addWidget(notice);
        resultLayout->addStretch(1);
        return;
    }

    const std::string balance = strOf(result, "balance");
    if (!balance.empty()) {
        resultLayout->addWidget(keyValueRow(tr("Balance"), amountValue(amountText(balance), "AOK")));
        resultLayout->addWidget(keyValueRow(tr("Total received"), amountValue(amountText(strOf(result, "received")), "AOK")));
    }
    const qint64 ntx = intOf(result, "ntx");
    resultLayout->addWidget(keyValueRow(tr("Transactions"), monoLabel(GUIUtil::withThousands(QString::number(ntx)))));
    resultLayout->addWidget(line());
    resultLayout->addSpacing(12);
    QLabel *head = caption(tr("Recent transactions"));
    head->setFont(GUIUtil::appFont(12, QFont::Medium));
    resultLayout->addWidget(head);
    resultLayout->addSpacing(6);
    const UniValue txids = find_value(result, "txids");
    for (size_t i = 0; txids.isArray() && i < txids.size(); ++i) {
        const QString txid = QString::fromStdString(txids[i].getValStr());
        QLabel *link = linkLabel(txid, txid);
        link->setContentsMargins(0, 3, 0, 3);
        resultLayout->addWidget(link);
    }
    if (txids.isArray() && ntx > (qint64)txids.size())
        resultLayout->addWidget(caption(tr("and %1 more").arg(ntx - (qint64)txids.size())));
    if (ntx == 0)
        resultLayout->addWidget(caption(tr("No transactions.")));
    resultLayout->addStretch(1);
}

void ExplorerPage::lookupDone(const QString& query, const QString& json)
{
    // only the answer to the latest query is shown
    if (query != pendingQuery)
        return;
    searchButton->setEnabled(true);

    UniValue result;
    if (!result.read(json.toStdString()) || !result.isObject()) {
        showMessage(tr("Result"), tr("The look-up failed."), false);
        return;
    }
    const std::string kind = strOf(result, "kind");
    if (kind == "tx") {
        showTransaction(result);
    } else if (kind == "block") {
        showBlock(result);
    } else if (kind == "address") {
        showAddress(result);
    } else if (kind == "notx") {
        if (!boolOf(result, "txindex")) {
            showMessage(tr("Not found"),
                        tr("This node has no transaction or block with this ID in its wallet, memory pool or unspent outputs. "
                           "Other transactions can only be looked up when the node keeps a transaction index. "
                           "To turn it on, add the line txindex=1 to aokchain.conf and start the wallet once with the option -reindex. "
                           "Rebuilding the index takes a while."), true);
        } else if (PastBlocks::missing()) {
            // a wallet that started from a chain state snapshot: old transactions are in blocks that it does not have
            showMessage(tr("Not found"), tr("This node knows no transaction and no block with this ID among the blocks that it has. It started from a snapshot of the chain state: transactions from before the snapshot can be looked up when the past blocks have been downloaded (Settings, Network settings)."), true);
            // (asked once: the ID may as well be mistyped)
            static bool asked = false;
            if (!asked) {
                asked = true;
                PastBlocks::offer(this, tr("Looking up a transaction from before the snapshot"));
            }
        } else {
            showMessage(tr("Not found"), tr("This node knows no transaction and no block with this ID. Check the ID; while the node is still synchronizing, recent ones are not known yet."), false);
        }
    } else if (kind == "noblock") {
        showMessage(tr("Not found"), tr("There is no block at this height. The node is at block %1.").arg(clientModel ? clientModel->getNumBlocks() : 0), false);
    } else {
        showMessage(tr("Result"), tr("This is not a transaction ID or block hash (64 hexadecimal characters), an address or a block height."), false);
    }
}

void ExplorerPage::refreshDone(const QString& json)
{
    refreshPending = false;
    UniValue result;
    if (!result.read(json.toStdString()) || !result.isObject())
        return;
    const GUIUtil::Theme& t = GUIUtil::theme();

    fillChainStats(chainStats, json);

    poolCount->setAmount(GUIUtil::withThousands(QString::number(intOf(result, "poolsize"))));
    poolSize->setAmount(QString::number(intOf(result, "poolbytes") / 1000.0, 'f', 2), "kB");
    double minFee = QString::fromStdString(strOf(result, "relayfee")).toDouble();
    minFee = qMax(minFee, QString::fromStdString(strOf(result, "poolminfee")).toDouble());
    poolMinFee->setAmount(QString::number(minFee, 'g', 4), "/kB");

    clearLayout(poolLayout);
    const UniValue entries = find_value(result, "entries");
    const qint64 now = intOf(result, "now");
    for (size_t i = 0; entries.isArray() && i < entries.size(); ++i) {
        const UniValue& entry = entries[i];
        const QString txid = QString::fromStdString(strOf(entry, "txid"));
        QWidget *row = new QWidget();
        QVBoxLayout *outer = new QVBoxLayout(row);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        outer->addWidget(line());
        QHBoxLayout *layout = new QHBoxLayout();
        layout->setContentsMargins(0, 10, 0, 10);
        layout->setSpacing(12);
        QLabel *dot = new QLabel();
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 4px; }").arg(t.negative.name()));
        layout->addWidget(dot);
        QVBoxLayout *left = new QVBoxLayout();
        left->setSpacing(1);
        left->addWidget(linkLabel(GUIUtil::shortHash(txid), txid, GUIUtil::monoToolTip(txid)));
        QStringList details;
        if (boolOf(entry, "mine"))
            details << tr("My transaction");
        const qint64 age = qMax<qint64>(0, now - intOf(entry, "time"));
        details << tr("%1 ago").arg(GUIUtil::formatNiceTimeOffset(age));
        details << bytesText(intOf(entry, "size"));
        left->addWidget(caption(details.join(QString::fromUtf8(" · "))));
        layout->addLayout(left, 1);
        QVBoxLayout *right = new QVBoxLayout();
        right->setSpacing(1);
        right->addWidget(amountValue(amountText(strOf(entry, "amount")), QString(), 14));
        QHBoxLayout *feeLine = new QHBoxLayout();
        feeLine->setSpacing(4);
        feeLine->addStretch();
        feeLine->addWidget(caption(tr("fee")));
        AmountLabel *fee = new AmountLabel(13, QFont::Normal);
        fee->setColor(t.textSecondary);
        fee->setAmount(amountText(strOf(entry, "fee")));
        feeLine->addWidget(fee);
        right->addLayout(feeLine);
        layout->addLayout(right);
        outer->addLayout(layout);
        poolLayout->addWidget(row);
    }
    const qint64 poolTotal = intOf(result, "poolsize");
    if (entries.isArray() && entries.size() == 0) {
        QLabel *empty = caption(tr("No transactions are waiting."));
        empty->setContentsMargins(0, 10, 0, 10);
        poolLayout->addWidget(line());
        poolLayout->addWidget(empty);
    } else if (entries.isArray() && poolTotal > (qint64)entries.size()) {
        poolLayout->addWidget(caption(tr("and %1 more").arg(poolTotal - (qint64)entries.size())));
    }
    poolLayout->addStretch(1);
}

QWidget *ExplorerPage::headerWidget() const
{
    return headerNote;
}
