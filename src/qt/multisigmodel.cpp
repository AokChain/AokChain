// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pastblocks.h"
#include <QApplication>
#include "multisigmodel.h"

#include "explorerpage.h"   // NodeRPC::call
#include "walletmodel.h"

#include "base58.h"
#include "coins.h"
#include "core_io.h"
#include "hash.h"
#include "policy/policy.h"
#include "rpc/server.h"
#include "script/ismine.h"
#include "utilmoneystr.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/standard.h"
#include "txmempool.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "wallet/coincontrol.h"
#include "wallet/fees.h"
#include "wallet/wallet.h"

#include <univalue.h>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <set>

namespace {

const char *KEY_PREFIX = "aokms-key1:";
const char *WALLET_PREFIX = "aokms-wallet1:";
const char *REQUEST_PREFIX = "aokms-req1:";

// destdata keys in the wallet file
const std::string DATA_WALLET = "msigwallet";       // on the multisig address: the wallet as JSON
const std::string DATA_REQUEST = "msigreq_";        // on the multisig address: + id, a request as JSON
const std::string DATA_KEY = "msigkey";             // on an address of this wallet: "unused" or "used"

const char *BLOCK_BEGIN = "-----BEGIN AOKCHAIN SIGNED MESSAGE-----";
const char *BLOCK_SIGNATURE = "-----BEGIN SIGNATURE-----";
const char *BLOCK_END = "-----END AOKCHAIN SIGNED MESSAGE-----";

QString hexOf(const std::vector<unsigned char>& bytes)
{
    return QString::fromStdString(HexStr(bytes.begin(), bytes.end()));
}

QString hexOf(const CScript& script)
{
    return QString::fromStdString(HexStr(script.begin(), script.end()));
}

CScript scriptOf(const QString& hex)
{
    const std::vector<unsigned char> bytes = ParseHex(hex.toStdString());
    return CScript(bytes.begin(), bytes.end());
}

/** The amount as a decimal string for JSON: no floating point on the way. */
QString amountText(CAmount amount)
{
    return QString("%1.%2").arg(amount / COIN).arg(amount % COIN, 8, 10, QChar('0'));
}

bool amountOf(const QString& text, CAmount& amount)
{
    return ParseMoney(text.toStdString(), amount) && MoneyRange(amount);
}

/** m, the public keys and n of a "m <keys> n OP_CHECKMULTISIG" script. */
bool readRedeemScript(const CScript& script, int& required, std::vector<CPubKey>& keys)
{
    keys.clear();
    CScript::const_iterator pc = script.begin();
    opcodetype op;
    std::vector<unsigned char> data;
    if (!script.GetOp(pc, op, data) || op < OP_1 || op > OP_16)
        return false;
    required = CScript::DecodeOP_N(op);
    while (true) {
        if (!script.GetOp(pc, op, data))
            return false;
        if (op >= OP_1 && op <= OP_16)
            break;
        CPubKey key(data);
        if (data.size() != 33 || !key.IsFullyValid())
            return false;
        keys.push_back(key);
    }
    if (CScript::DecodeOP_N(op) != (int)keys.size() || required < 1 || required > (int)keys.size())
        return false;
    if (!script.GetOp(pc, op, data) || op != OP_CHECKMULTISIG || pc != script.end())
        return false;
    return true;
}

QString addressOf(const CScript& redeemScript)
{
    return QString::fromStdString(EncodeDestination(CScriptID(redeemScript)));
}

/** Size in bytes of the fully signed spend; test/functional/wallet_multisig_flow.py has the same sum. */
unsigned int signedSize(int inputs, int outputs, int required, int keys)
{
    const int redeem = 3 + 34 * keys;
    const int push = redeem < 76 ? 1 : redeem < 256 ? 2 : 3;
    const int scriptSig = 1 + required * 73 + push + redeem;
    const int varint = scriptSig < 253 ? 1 : 3;
    return 4 + 4 + 1 + inputs * (32 + 4 + varint + scriptSig + 4) + 1 + outputs * (8 + 1 + 25) + 4;
}

/** Hash of the transaction without its signatures: the same for every state of a request. */
QString requestId(const CMutableTransaction& tx)
{
    CMutableTransaction bare(tx);
    for (CTxIn& in : bare.vin)
        in.scriptSig.clear();
    return QString::fromStdString(bare.GetHash().GetHex());
}

QJsonObject walletJson(const MultisigWallet& wallet)
{
    QJsonObject object;
    object["v"] = 1;
    object["name"] = wallet.name;
    object["m"] = wallet.required;
    QJsonArray keys;
    for (const MultisigKey& key : wallet.keys) {
        QJsonObject k;
        k["name"] = key.name;
        k["pubkey"] = key.pubkey;
        keys.append(k);
    }
    object["keys"] = keys;
    object["address"] = wallet.address;
    object["redeemScript"] = wallet.redeemScript;
    return object;
}

QJsonObject requestJson(const MultisigRequest& request, bool forStorage)
{
    QJsonObject object;
    object["v"] = 1;
    object["wallet"] = request.walletName;
    object["address"] = request.address;
    object["redeemScript"] = request.redeemScript;
    object["memo"] = request.memo;
    object["to"] = request.statedTo;
    object["amount"] = amountText(request.statedAmount);
    object["tx"] = request.txHex;
    QJsonArray prevouts;
    for (const MultisigPrevout& prevout : request.prevouts) {
        QJsonObject p;
        p["txid"] = prevout.txid;
        p["vout"] = prevout.vout;
        p["scriptPubKey"] = prevout.scriptPubKey;
        p["amount"] = amountText(prevout.amount);
        prevouts.append(p);
    }
    object["prevouts"] = prevouts;
    object["created"] = QString::number(request.created);
    if (forStorage)
        object["txid"] = request.txid;
    return object;
}

bool readRequestJson(const QJsonObject& object, MultisigRequest& request)
{
    request = MultisigRequest();
    request.walletName = object["wallet"].toString().left(80);
    request.address = object["address"].toString();
    request.redeemScript = object["redeemScript"].toString().toLower();
    request.memo = object["memo"].toString().left(200);
    request.statedTo = object["to"].toString();
    amountOf(object["amount"].toString(), request.statedAmount);
    request.txHex = object["tx"].toString().toLower();
    for (const QJsonValue& value : object["prevouts"].toArray()) {
        const QJsonObject p = value.toObject();
        MultisigPrevout prevout;
        prevout.txid = p["txid"].toString().toLower();
        prevout.vout = p["vout"].toInt();
        prevout.scriptPubKey = p["scriptPubKey"].toString().toLower();
        amountOf(p["amount"].toString(), prevout.amount);
        request.prevouts.append(prevout);
    }
    request.created = object["created"].toString().toLongLong();
    request.txid = object["txid"].toString();
    CMutableTransaction tx;
    if (!IsHex(request.txHex.toStdString()) || !IsHex(request.redeemScript.toStdString()) || !DecodeHexTx(tx, request.txHex.toStdString()))
        return false;
    request.id = requestId(tx);
    return true;
}

QString encoded(const char *prefix, const QJsonObject& object)
{
    return QString(prefix) + QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64());
}

/** The JSON object after the prefix; white space and line breaks of a pasted text are dropped. */
bool decoded(const char *prefix, const QString& text, QJsonObject& object)
{
    QString compact = text;
    compact.remove(QRegExp("\\s"));
    const int start = compact.indexOf(prefix);
    if (start < 0)
        return false;
    const QByteArray json = QByteArray::fromBase64(compact.mid(start + int(strlen(prefix))).toLatin1());
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject() || document.object()["v"].toInt() != 1)
        return false;
    object = document.object();
    return true;
}

/** Run work on a thread of its own and hand the result to the GUI thread. */
template <typename Work, typename Done>
void runAsync(QObject *context, Work work, Done done)
{
    QPointer<QObject> guard(context);
    QThread *thread = QThread::create([guard, work, done] {
        auto result = work();
        if (guard)
            QMetaObject::invokeMethod(guard.data(), [done, result] { done(result); }, Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace

bool MultisigWallet::hasMyKey() const
{
    for (const MultisigKey& key : keys) {
        if (key.mine)
            return true;
    }
    return false;
}

MultisigModel::MultisigModel(WalletModel *_walletModel, QObject *parent) :
    QObject(parent), walletModel(_walletModel), refreshing(false), refreshAgain(false)
{
    // a new block or a wallet transaction can change the balances and the state of requests
    connect(walletModel, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(refresh()));
}

MultisigModel::~MultisigModel()
{
}

// ---------------------------------------------------------------- share strings

QString MultisigModel::keyCode(const QString& pubkeyHex)
{
    return QString(KEY_PREFIX) + pubkeyHex;
}

bool MultisigModel::parseKeyCode(const QString& text, QString& pubkeyHex, QString& error) const
{
    QString hex = text.trimmed();
    if (hex.startsWith(KEY_PREFIX, Qt::CaseInsensitive))
        hex = hex.mid(int(strlen(KEY_PREFIX)));
    hex = hex.trimmed().toLower();
    if (hex.isEmpty()) {
        error = tr("Paste the public key that the participant sent.");
        return false;
    }
    if (!IsHex(hex.toStdString()) || hex.size() != 66) {
        error = tr("This is not a public key. A public key starts with aokms-key1: followed by 66 characters.");
        return false;
    }
    CPubKey key(ParseHex(hex.toStdString()));
    if (!key.IsFullyValid() || !key.IsCompressed()) {
        error = tr("This public key is not valid. Ask the participant to copy it again.");
        return false;
    }
    pubkeyHex = hex;
    return true;
}

QString MultisigModel::walletCode(const MultisigWallet& wallet)
{
    return encoded(WALLET_PREFIX, walletJson(wallet));
}

bool MultisigModel::parseWalletCode(const QString& text, MultisigWallet& wallet, QString& error) const
{
    QJsonObject object;
    if (!decoded(WALLET_PREFIX, text, object)) {
        error = tr("This is not multisig wallet information. It starts with aokms-wallet1:");
        return false;
    }
    QList<MultisigKey> keys;
    for (const QJsonValue& value : object["keys"].toArray()) {
        MultisigKey key;
        key.name = value.toObject()["name"].toString().left(40);
        key.pubkey = value.toObject()["pubkey"].toString();
        keys.append(key);
    }
    // the address is made again from the rule and the keys: what the information says is only compared
    if (!makeWallet(object["name"].toString().left(40), object["m"].toInt(), keys, wallet, error))
        return false;
    if (wallet.address != object["address"].toString() || wallet.redeemScript != object["redeemScript"].toString().toLower()) {
        error = tr("The information does not fit together: its keys give another address than it states. Ask for it again.");
        return false;
    }
    return true;
}

QString MultisigModel::requestCode(const MultisigRequest& request)
{
    return encoded(REQUEST_PREFIX, requestJson(request, false));
}

bool MultisigModel::parseRequestCode(const QString& text, MultisigRequest& request, QString& error) const
{
    QJsonObject object;
    if (!decoded(REQUEST_PREFIX, text, object) || !readRequestJson(object, request)) {
        error = tr("This is not a signing request. It starts with aokms-req1:");
        return false;
    }
    request.txid.clear();
    return true;
}

// ---------------------------------------------------------------- signed messages

bool MultisigModel::signMessage(const QString& address, const QString& message, QString& signature, QString& error)
{
    CTxDestination destination = DecodeDestination(address.trimmed().toStdString());
    if (!IsValidDestination(destination)) {
        error = tr("The address is not valid.");
        return false;
    }
    const CKeyID *keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        error = tr("This kind of address cannot sign a text. Choose an ordinary address of this wallet.");
        return false;
    }
    CKey key;
    if (!walletModel->getWallet()->GetKey(*keyID, key)) {
        error = tr("This wallet does not have the key of this address. Choose one of your own addresses.");
        return false;
    }
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << message.toStdString();
    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        error = tr("Signing failed.");
        return false;
    }
    signature = QString::fromStdString(EncodeBase64(vchSig.data(), vchSig.size()));
    return true;
}

bool MultisigModel::verifyMessage(const QString& address, const QString& message, const QString& signature, QString& reason) const
{
    reason.clear();
    CTxDestination destination = DecodeDestination(address.trimmed().toStdString());
    if (!IsValidDestination(destination)) {
        reason = tr("The address is not a valid AokChain address.");
        return false;
    }
    const CKeyID *keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        reason = tr("This kind of address cannot sign a text.");
        return false;
    }
    bool invalid = false;
    const std::vector<unsigned char> vchSig = DecodeBase64(signature.trimmed().toStdString().c_str(), &invalid);
    if (invalid || vchSig.empty()) {
        reason = tr("The signature is damaged: it could not be read. Copy it again, completely.");
        return false;
    }
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << message.toStdString();
    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig)) {
        reason = tr("The signature does not belong to this text.");
        return false;
    }
    if (!(CTxDestination(pubkey.GetID()) == destination)) {
        reason = tr("The signature was not made by this address, or the text was changed after signing.");
        return false;
    }
    return true;
}

QString MultisigModel::messageBlock(const QString& address, const QString& message, const QString& signature)
{
    return QString("%1\n%2\n%3\n%4\n%5\n%6\n").arg(BLOCK_BEGIN, message, BLOCK_SIGNATURE, address, signature, BLOCK_END);
}

bool MultisigModel::parseMessageBlock(const QString& text, QString& address, QString& message, QString& signature)
{
    QString block = text;
    block.replace("\r\n", "\n");
    const int begin = block.indexOf(BLOCK_BEGIN);
    const int middle = block.lastIndexOf(BLOCK_SIGNATURE);
    const int end = block.lastIndexOf(BLOCK_END);
    if (begin < 0 || middle < begin || end < middle)
        return false;
    QString body = block.mid(begin + int(strlen(BLOCK_BEGIN)), middle - begin - int(strlen(BLOCK_BEGIN)));
    // the line breaks next to the markers are not part of the text
    if (body.startsWith('\n'))
        body.remove(0, 1);
    if (body.endsWith('\n'))
        body.chop(1);
    // After the second marker: the address and the signature. A messenger may have joined the
    // lines, so they are told apart by length: a signature is always 88 characters of base 64.
    QString tail = block.mid(middle + int(strlen(BLOCK_SIGNATURE)), end - middle - int(strlen(BLOCK_SIGNATURE)));
    tail.remove(QRegExp("\\s"));
    if (tail.size() <= 88)
        return false;
    message = body.trimmed().isEmpty() ? QString() : body;
    address = tail.left(tail.size() - 88);
    signature = tail.right(88);
    return true;
}

// ---------------------------------------------------------------- multisig wallets

bool MultisigModel::myKey(QString& pubkeyHex, QString& error)
{
    CWallet *wallet = walletModel->getWallet();
    {
        LOCK(wallet->cs_wallet);
        for (const std::pair<CTxDestination, CAddressBookData>& entry : wallet->mapAddressBook) {
            const CAddressBookData::StringMap::const_iterator it = entry.second.destdata.find(DATA_KEY);
            if (it == entry.second.destdata.end() || it->second != "unused")
                continue;
            const CKeyID *keyID = boost::get<CKeyID>(&entry.first);
            CPubKey pubkey;
            if (keyID && wallet->GetPubKey(*keyID, pubkey) && pubkey.IsCompressed()) {
                pubkeyHex = hexOf(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
                return true;
            }
        }
    }
    // a fresh key; an encrypted wallet whose key pool is empty has to be unlocked for it
    WalletModel::UnlockContext context(walletModel->requestUnlock());
    if (!context.isValid()) {
        error = tr("The wallet was not unlocked.");
        return false;
    }
    LOCK2(cs_main, wallet->cs_wallet);
    CPubKey pubkey;
    if (!wallet->GetKeyFromPool(pubkey) || !pubkey.IsCompressed()) {
        error = tr("The wallet could not make a new key.");
        return false;
    }
    const CTxDestination destination = pubkey.GetID();
    wallet->SetAddressBook(destination, tr("Multisig key").toStdString(), "receive");
    wallet->AddDestData(destination, DATA_KEY, "unused");
    pubkeyHex = hexOf(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
    return true;
}

bool MultisigModel::makeWallet(const QString& name, int required, const QList<MultisigKey>& keys, MultisigWallet& result, QString& error) const
{
    if (keys.size() < 2 || keys.size() > MAX_KEYS) {
        error = tr("A multisig wallet has 2 to %1 participants.").arg(MAX_KEYS);
        return false;
    }
    if (required < 1 || required > keys.size()) {
        error = tr("The number of signatures must be between 1 and the number of participants.");
        return false;
    }
    QList<MultisigKey> sorted;
    std::set<QString> seen;
    for (const MultisigKey& key : keys) {
        MultisigKey checked;
        checked.name = key.name.trimmed();
        if (!parseKeyCode(key.pubkey, checked.pubkey, error))
            return false;
        if (!seen.insert(checked.pubkey).second) {
            error = tr("The same public key is there twice. Every participant needs a key of their own.");
            return false;
        }
        sorted.append(checked);
    }
    // The order of the keys is part of the address. Sorted by their bytes (as BIP67 does), every
    // participant gets the same address, whatever the order in which the keys were entered.
    std::sort(sorted.begin(), sorted.end(), [](const MultisigKey& a, const MultisigKey& b) { return a.pubkey < b.pubkey; });
    std::vector<CPubKey> pubkeys;
    CWallet *wallet = walletModel->getWallet();
    for (MultisigKey& key : sorted) {
        pubkeys.push_back(CPubKey(ParseHex(key.pubkey.toStdString())));
        key.mine = wallet->HaveKey(pubkeys.back().GetID());
    }
    const CScript redeemScript = GetScriptForMultisig(required, pubkeys);
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE) {
        error = tr("Too many participants for one address.");
        return false;
    }
    result = MultisigWallet();
    result.name = name.trimmed();
    result.required = required;
    result.keys = sorted;
    result.redeemScript = hexOf(redeemScript);
    result.address = addressOf(redeemScript);
    return true;
}

bool MultisigModel::walletFromRedeemScript(const QString& redeemScriptHex, MultisigWallet& result, QString& error) const
{
    int required = 0;
    std::vector<CPubKey> pubkeys;
    if (!IsHex(redeemScriptHex.toStdString()) || !readRedeemScript(scriptOf(redeemScriptHex), required, pubkeys)) {
        error = tr("The request does not describe a multisig address.");
        return false;
    }
    QList<MultisigKey> keys;
    for (size_t i = 0; i < pubkeys.size(); ++i) {
        MultisigKey key;
        key.name = tr("Participant %1").arg(i + 1);
        key.pubkey = hexOf(std::vector<unsigned char>(pubkeys[i].begin(), pubkeys[i].end()));
        keys.append(key);
    }
    if (!makeWallet(QString(), required, keys, result, error))
        return false;
    if (result.redeemScript != redeemScriptHex.toLower()) {
        // the keys of the script are not in the sorted order: another address than this GUI makes
        error = tr("The request is for a multisig address that was not made with this wallet software (its keys are not sorted).");
        return false;
    }
    return true;
}

QList<MultisigWallet> MultisigModel::wallets() const
{
    QList<MultisigWallet> list = storedWallets();
    // open requests
    for (MultisigWallet& item : list) {
        for (const MultisigRequest& request : requests(item.address)) {
            if (!request.txid.isEmpty())
                continue;
            const MultisigRequestView view = analyze(request);
            if (view.spent || !view.decoded)
                continue;
            if (view.canSign)
                ++item.needMySignature;
            else
                ++item.waiting;
        }
    }
    return list;
}

QList<MultisigWallet> MultisigModel::storedWallets() const
{
    QList<MultisigWallet> list;
    CWallet *wallet = walletModel->getWallet();
    std::vector<std::string> stored;
    {
        LOCK(wallet->cs_wallet);
        for (const std::pair<CTxDestination, CAddressBookData>& entry : wallet->mapAddressBook) {
            const CAddressBookData::StringMap::const_iterator it = entry.second.destdata.find(DATA_WALLET);
            if (it != entry.second.destdata.end())
                stored.push_back(it->second);
        }
    }
    for (const std::string& json : stored) {
        const QJsonObject object = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
        QList<MultisigKey> keys;
        for (const QJsonValue& value : object["keys"].toArray()) {
            MultisigKey key;
            key.name = value.toObject()["name"].toString();
            key.pubkey = value.toObject()["pubkey"].toString();
            keys.append(key);
        }
        MultisigWallet item;
        QString error;
        if (!makeWallet(object["name"].toString(), object["m"].toInt(), keys, item, error))
            continue;
        item.balance = (CAmount)object["balance"].toString().toLongLong();
        list.append(item);
    }
    std::sort(list.begin(), list.end(), [](const MultisigWallet& a, const MultisigWallet& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0 || (a.name.compare(b.name, Qt::CaseInsensitive) == 0 && a.address < b.address);
    });
    return list;
}

bool MultisigModel::wallet(const QString& address, MultisigWallet& result) const
{
    for (const MultisigWallet& item : storedWallets()) {
        if (item.address == address) {
            result = item;
            return true;
        }
    }
    return false;
}

void MultisigModel::markKeysUsed(const MultisigWallet& item)
{
    CWallet *wallet = walletModel->getWallet();
    LOCK(wallet->cs_wallet);
    for (const MultisigKey& key : item.keys) {
        const CTxDestination destination = CPubKey(ParseHex(key.pubkey.toStdString())).GetID();
        std::string value;
        if (wallet->GetDestData(destination, DATA_KEY, &value) && value == "unused") {
            wallet->EraseDestData(destination, DATA_KEY);
            wallet->AddDestData(destination, DATA_KEY, "used");
        }
    }
}

void MultisigModel::addWallet(const MultisigWallet& item, bool wantRescan)
{
    // A wallet that started from a chain state snapshot cannot look into the past blocks before it
    // has them: the address is added now and the wallet is scanned when they have been downloaded.
    bool rescan = wantRescan;
    if (rescan && PastBlocks::missing()) {
        rescan = false;
        PastBlocks::offerForWallet(QApplication::activeWindow(), tr("Looking for the earlier transactions of this address"));
    }
    const std::string redeemScript = item.redeemScript.toStdString();
    const std::string label = item.name.toStdString();
    const std::string json = QJsonDocument(walletJson(item)).toJson(QJsonDocument::Compact).toStdString();
    const QString address = item.address;
    CWallet *wallet = walletModel->getWallet();
    const MultisigWallet copy = item;
    runAsync(this, [=]() -> QString {
        // Watch the address: the wallet then knows the redeem script, shows the coins of the address
        // as watch-only and lists its transactions. This is "importaddress <redeem script> <label>
        // <rescan> true".
        UniValue result;
        std::string error;
        if (!NodeRPC::call("importaddress", {redeemScript, label, rescan ? "true" : "false", "true"}, result, &error)) {
            // a wallet that has all the keys itself cannot watch what it can spend: it only needs the script
            const CScript script = scriptOf(QString::fromStdString(redeemScript));
            LOCK2(cs_main, wallet->cs_wallet);
            if (::IsMine(*wallet, GetScriptForDestination(CScriptID(script)), chainActive.Tip()) != ISMINE_SPENDABLE) {
                if (!wallet->AddCScript(script) || ::IsMine(*wallet, GetScriptForDestination(CScriptID(script)), chainActive.Tip()) == ISMINE_NO)
                    return QString::fromStdString(error);
            }
            wallet->SetAddressBook(CScriptID(script), label, "receive");
        }
        LOCK(wallet->cs_wallet);
        const CTxDestination destination = CScriptID(scriptOf(QString::fromStdString(redeemScript)));
        wallet->EraseDestData(destination, DATA_WALLET);
        if (!wallet->AddDestData(destination, DATA_WALLET, json))
            return QString("The wallet file could not be written.");
        return QString();
    }, [this, address, copy](const QString& error) {
        if (error.isEmpty())
            markKeysUsed(copy);
        Q_EMIT walletAdded(address, error);
        refresh();
    });
}

void MultisigModel::removeWallet(const QString& address)
{
    for (const MultisigRequest& item : requests(address))
        removeRequest(item.id);
    CWallet *wallet = walletModel->getWallet();
    {
        LOCK(wallet->cs_wallet);
        wallet->EraseDestData(DecodeDestination(address.toStdString()), DATA_WALLET);
    }
    Q_EMIT changed();
}

void MultisigModel::refresh()
{
    if (refreshing) {
        refreshAgain = true;
        return;
    }
    refreshing = true;
    QStringList addresses;
    for (const MultisigWallet& item : storedWallets())
        addresses.append(item.address);
    if (addresses.isEmpty()) {
        refreshing = false;
        return;
    }
    CWallet *wallet = walletModel->getWallet();
    runAsync(this, [addresses, wallet]() -> bool {
        // the confirmed coins of each address; the sum is kept with the wallet entry
        bool anyChange = false;
        for (const QString& address : addresses) {
            UniValue coins;
            if (!NodeRPC::call("listunspent", {"1", "9999999", "[\"" + address.toStdString() + "\"]"}, coins) || !coins.isArray())
                continue;
            CAmount balance = 0;
            for (size_t i = 0; i < coins.size(); ++i)
                balance += AmountFromValue(find_value(coins[i], "amount"));
            LOCK(wallet->cs_wallet);
            const CTxDestination destination = DecodeDestination(address.toStdString());
            std::string json;
            if (!wallet->GetDestData(destination, DATA_WALLET, &json))
                continue;
            QJsonObject object = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
            if (object["balance"].toString() == QString::number(balance))
                continue;
            object["balance"] = QString::number(balance);
            wallet->EraseDestData(destination, DATA_WALLET);
            wallet->AddDestData(destination, DATA_WALLET, QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
            anyChange = true;
        }
        return anyChange;
    }, [this](bool) {
        refreshing = false;
        Q_EMIT changed();
        if (refreshAgain) {
            refreshAgain = false;
            refresh();
        }
    });
}

// ---------------------------------------------------------------- spending

CAmount MultisigModel::estimateFee(const MultisigWallet& item, int inputs, int outputs, int multiplier) const
{
    const unsigned int size = signedSize(inputs, outputs, item.required, item.keys.size());
    CCoinControl coinControl;
    return GetMinimumFee(size, coinControl, ::mempool, ::feeEstimator, nullptr) * std::max(1, multiplier);
}

namespace {

struct Coin_ {
    QString txid;
    int vout;
    QString scriptPubKey;
    CAmount amount;
};

/** The confirmed coins of the address that no open request uses: largest first, then by txid and output. */
QList<Coin_> spendableCoins(const QString& address, const std::set<std::pair<QString, int>>& reserved)
{
    QList<Coin_> list;
    UniValue coins;
    if (!NodeRPC::call("listunspent", {"1", "9999999", "[\"" + address.toStdString() + "\"]"}, coins) || !coins.isArray())
        return list;
    for (size_t i = 0; i < coins.size(); ++i) {
        Coin_ coin;
        coin.txid = QString::fromStdString(find_value(coins[i], "txid").get_str());
        coin.vout = find_value(coins[i], "vout").get_int();
        coin.scriptPubKey = QString::fromStdString(find_value(coins[i], "scriptPubKey").get_str());
        coin.amount = AmountFromValue(find_value(coins[i], "amount"));
        if (!reserved.count(std::make_pair(coin.txid, coin.vout)))
            list.append(coin);
    }
    std::sort(list.begin(), list.end(), [](const Coin_& a, const Coin_& b) {
        if (a.amount != b.amount)
            return a.amount > b.amount;
        if (a.txid != b.txid)
            return a.txid < b.txid;
        return a.vout < b.vout;
    });
    return list;
}

} // namespace

/** Choose the coins of a spend. chosen, fee and change are set; false with the reason otherwise. */
static bool chooseCoins(const MultisigModel *model, const MultisigWallet& item, const QList<MultisigRequest>& open, CAmount& amount,
                        bool sendAll, int feeMultiplier, QList<Coin_>& chosen, CAmount& fee, CAmount& change, QString& error)
{
    std::set<std::pair<QString, int>> reserved;
    for (const MultisigRequest& request : open) {
        if (!request.txid.isEmpty())
            continue;
        for (const MultisigPrevout& prevout : request.prevouts)
            reserved.insert(std::make_pair(prevout.txid, prevout.vout));
    }
    const QList<Coin_> coins = spendableCoins(item.address, reserved);
    CAmount available = 0;
    for (const Coin_& coin : coins)
        available += coin.amount;
    chosen.clear();
    change = 0;
    if (coins.isEmpty()) {
        error = reserved.empty() ? MultisigModel::tr("The multisig address has no confirmed coins.")
                                 : MultisigModel::tr("All coins of the multisig address are in use by open requests. Finish or cancel a request first.");
        return false;
    }
    if (sendAll) {
        chosen = coins;
        fee = model->estimateFee(item, chosen.size(), 1, feeMultiplier);
        amount = available - fee;
        if (amount <= 0) {
            error = MultisigModel::tr("The coins of the address do not cover the fee.");
            return false;
        }
        return true;
    }
    if (amount <= 0) {
        error = MultisigModel::tr("Enter the amount to send.");
        return false;
    }
    CAmount total = 0;
    for (const Coin_& coin : coins) {
        chosen.append(coin);
        total += coin.amount;
        fee = model->estimateFee(item, chosen.size(), 2, feeMultiplier);
        if (total >= amount + fee) {
            change = total - amount - fee;
            // change that is not worth an output goes to the fee
            const CTxOut changeOut(change, GetScriptForDestination(DecodeDestination(item.address.toStdString())));
            if (change > 0 && IsDust(changeOut, ::dustRelayFee)) {
                fee += change;
                change = 0;
            }
            return true;
        }
    }
    error = !reserved.empty()
        ? MultisigModel::tr("Not enough coins: part of the balance is in use by open requests.")
        : MultisigModel::tr("Not enough coins: the amount and the fee are more than the balance of the multisig address.");
    return false;
}

bool MultisigModel::previewSpend(const QString& address, CAmount amount, bool sendAll, int feeMultiplier, CAmount& fee, CAmount& sent, QString& error) const
{
    MultisigWallet item;
    if (!wallet(address, item)) {
        error = tr("The multisig wallet is not known.");
        return false;
    }
    QList<Coin_> chosen;
    CAmount change = 0;
    sent = amount;
    return chooseCoins(this, item, requests(address), sent, sendAll, feeMultiplier, chosen, fee, change, error);
}

bool MultisigModel::createRequest(const QString& address, const QString& to, CAmount amount, bool sendAll, const QString& memo,
                                  int feeMultiplier, QString& id, QString& error)
{
    MultisigWallet item;
    if (!wallet(address, item)) {
        error = tr("The multisig wallet is not known.");
        return false;
    }
    if (!IsValidDestinationString(to.trimmed().toStdString())) {
        error = tr("The recipient address is not valid.");
        return false;
    }
    QList<Coin_> chosen;
    CAmount fee = 0, change = 0;
    if (!chooseCoins(this, item, requests(address), amount, sendAll, feeMultiplier, chosen, fee, change, error))
        return false;

    // createrawtransaction [{"txid":..,"vout":..},..] {"<recipient>":amount,"<multisig address>":change}
    UniValue inputs(UniValue::VARR);
    for (const Coin_& coin : chosen) {
        UniValue input(UniValue::VOBJ);
        input.pushKV("txid", coin.txid.toStdString());
        input.pushKV("vout", coin.vout);
        inputs.push_back(input);
    }
    QString outputs = QString("{\"%1\":%2").arg(to.trimmed(), amountText(amount));
    if (change > 0 && to.trimmed() != address)
        outputs += QString(",\"%1\":%2").arg(address, amountText(change));
    else if (change > 0)
        outputs = QString("{\"%1\":%2").arg(address, amountText(amount + change));
    outputs += "}";
    UniValue raw;
    std::string rpcError;
    if (!NodeRPC::call("createrawtransaction", {inputs.write(), outputs.toStdString()}, raw, &rpcError) || !raw.isStr()) {
        error = tr("The transaction could not be made: %1").arg(QString::fromStdString(rpcError));
        return false;
    }

    MultisigRequest request;
    request.address = address;
    request.walletName = item.name;
    request.redeemScript = item.redeemScript;
    request.memo = memo.trimmed().left(200);
    request.statedTo = to.trimmed();
    request.statedAmount = amount;
    request.txHex = QString::fromStdString(raw.get_str());
    for (const Coin_& coin : chosen) {
        MultisigPrevout prevout;
        prevout.txid = coin.txid;
        prevout.vout = coin.vout;
        prevout.scriptPubKey = coin.scriptPubKey;
        prevout.amount = coin.amount;
        request.prevouts.append(prevout);
    }
    request.created = QDateTime::currentMSecsSinceEpoch() / 1000;
    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.txHex.toStdString())) {
        error = tr("The transaction could not be read back.");
        return false;
    }
    request.id = requestId(tx);
    storeRequest(request);
    id = request.id;
    Q_EMIT changed();
    return true;
}

void MultisigModel::storeRequest(const MultisigRequest& request)
{
    CWallet *wallet = walletModel->getWallet();
    LOCK(wallet->cs_wallet);
    const CTxDestination destination = DecodeDestination(request.address.toStdString());
    const std::string key = DATA_REQUEST + request.id.left(16).toStdString();
    wallet->EraseDestData(destination, key);
    wallet->AddDestData(destination, key, QJsonDocument(requestJson(request, true)).toJson(QJsonDocument::Compact).toStdString());
}

QList<MultisigRequest> MultisigModel::requests(const QString& address) const
{
    QList<MultisigRequest> list;
    CWallet *wallet = walletModel->getWallet();
    std::vector<std::string> stored;
    {
        LOCK(wallet->cs_wallet);
        for (const std::pair<CTxDestination, CAddressBookData>& entry : wallet->mapAddressBook) {
            if (!address.isEmpty() && EncodeDestination(entry.first) != address.toStdString())
                continue;
            for (const std::pair<std::string, std::string>& data : entry.second.destdata) {
                if (data.first.compare(0, DATA_REQUEST.size(), DATA_REQUEST) == 0)
                    stored.push_back(data.second);
            }
        }
    }
    for (const std::string& json : stored) {
        MultisigRequest request;
        if (readRequestJson(QJsonDocument::fromJson(QByteArray::fromStdString(json)).object(), request))
            list.append(request);
    }
    std::sort(list.begin(), list.end(), [](const MultisigRequest& a, const MultisigRequest& b) {
        return a.created > b.created || (a.created == b.created && a.id < b.id);
    });
    return list;
}

bool MultisigModel::request(const QString& id, MultisigRequest& result) const
{
    for (const MultisigRequest& item : requests()) {
        if (item.id == id) {
            result = item;
            return true;
        }
    }
    return false;
}

MultisigRequestView MultisigModel::analyze(const MultisigRequest& request) const
{
    MultisigRequestView view;
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.txHex.toStdString()) || mtx.vin.empty() || mtx.vout.empty()) {
        view.problems << tr("The transaction of the request cannot be read.");
        return view;
    }
    view.decoded = true;
    const CTransaction tx(mtx);

    // the multisig wallet: the redeem script of the request must be one that this wallet knows
    const CScript redeemScript = scriptOf(request.redeemScript);
    const QString address = addressOf(redeemScript);
    const CScript scriptPubKey = GetScriptForDestination(CScriptID(redeemScript));
    MultisigWallet item;
    view.walletKnown = wallet(address, item);
    int required = 0;
    std::vector<CPubKey> pubkeys;
    if (!readRedeemScript(redeemScript, required, pubkeys)) {
        view.problems << tr("The request does not describe a multisig address.");
        return view;
    }
    view.required = required;
    if (!view.walletKnown)
        view.problems << tr("This multisig wallet is not in your wallet. Add it first, and check its participants.");
    else if (address != request.address)
        view.warnings << tr("The request names another multisig address than its script belongs to.");

    // the inputs: unspent coins of the multisig address, as this node knows them
    view.inputsFound = true;
    view.inputsMatch = true;
    std::vector<CAmount> amounts;
    {
        LOCK2(cs_main, ::mempool.cs);
        CCoinsViewMemPool viewMempool(pcoinsTip.get(), ::mempool);
        for (const CTxIn& in : tx.vin) {
            Coin coin;
            if (!viewMempool.GetCoin(in.prevout, coin) || coin.IsSpent() || ::mempool.isSpent(in.prevout)) {
                view.inputsFound = false;
                amounts.push_back(0);
                continue;
            }
            if (coin.out.scriptPubKey != scriptPubKey)
                view.inputsMatch = false;
            amounts.push_back(coin.out.nValue);
            view.inputTotal += coin.out.nValue;
        }
    }
    if (!view.inputsFound) {
        view.spent = true;
        view.problems << tr("The coins of this request are no longer there: it was sent already, or the coins were used by another transaction. (Or this wallet is not synchronized yet.)");
    } else if (!view.inputsMatch) {
        view.problems << tr("The request spends coins that do not belong to the multisig address.");
    }

    // the outputs: what the transaction really pays
    for (const CTxOut& out : tx.vout) {
        MultisigRequestView::Output output;
        output.amount = out.nValue;
        output.change = out.scriptPubKey == scriptPubKey;
        CTxDestination destination;
        if (ExtractDestination(out.scriptPubKey, destination) && out.scriptPubKey == GetScriptForDestination(destination))
            output.address = QString::fromStdString(EncodeDestination(destination));
        if (output.change)
            view.change += out.nValue;
        else
            view.amount += out.nValue;
        view.outputs.append(output);
    }
    int payments = 0;
    bool plain = true;
    for (const MultisigRequestView::Output& output : view.outputs) {
        if (!output.change)
            ++payments;
        if (output.address.isEmpty())
            plain = false;
    }
    if (!plain)
        view.problems << tr("The transaction has an output that is not a plain payment to an address (data or tokens). This wallet does not sign such requests.");
    if (payments > 1)
        view.warnings << tr("The transaction pays to %1 addresses. Check every one of them below.").arg(payments);
    if (payments == 1) {
        for (const MultisigRequestView::Output& output : view.outputs) {
            if (!output.change && (output.address != request.statedTo || output.amount != request.statedAmount))
                view.warnings << tr("The request says something else than its transaction does. What you see here is what the transaction does: compare the recipient and the amount with what you agreed on.");
        }
    } else if (!request.statedTo.isEmpty()) {
        view.warnings << tr("The request says something else than its transaction does. What you see here is what the transaction does: compare the recipients and the amounts with what you agreed on.");
    }
    if (view.inputsFound) {
        view.fee = view.inputTotal - view.amount - view.change;
        if (view.fee < 0)
            view.problems << tr("The transaction pays out more than its coins are worth.");
        else if (view.walletKnown && view.fee > 10 * estimateFee(item, tx.vin.size(), tx.vout.size(), 1) && view.fee > COIN / 10)
            view.warnings << tr("The fee is unusually high. Whatever is not paid to an address goes to the fee.");
    }

    // the signatures: who signed is read from the transaction itself
    std::vector<int> count(pubkeys.size(), 0);
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        const CScript& scriptSig = tx.vin[i].scriptSig;
        if (scriptSig.empty())
            continue;
        std::vector<std::vector<unsigned char>> pushes;
        CScript::const_iterator pc = scriptSig.begin();
        opcodetype op;
        std::vector<unsigned char> data;
        bool readable = true;
        while (pc < scriptSig.end()) {
            if (!scriptSig.GetOp(pc, op, data) || op > OP_PUSHDATA4) {
                readable = false;
                break;
            }
            pushes.push_back(data);
        }
        if (!readable || pushes.size() < 2 || CScript(pushes.back().begin(), pushes.back().end()) != redeemScript) {
            ++view.invalidSignatures;
            continue;
        }
        std::vector<bool> seen(pubkeys.size(), false);
        for (size_t s = 0; s + 1 < pushes.size(); ++s) {
            const std::vector<unsigned char>& sig = pushes[s];
            if (sig.empty())
                continue; // a place holder for a signature that is still missing
            bool fits = false;
            if (sig.back() == SIGHASH_ALL) {
                const uint256 hash = SignatureHash(redeemScript, tx, i, SIGHASH_ALL, amounts[i], SIGVERSION_BASE);
                const std::vector<unsigned char> der(sig.begin(), sig.end() - 1);
                for (size_t k = 0; k < pubkeys.size(); ++k) {
                    if (!seen[k] && pubkeys[k].Verify(hash, der)) {
                        seen[k] = true;
                        fits = true;
                        break;
                    }
                }
            }
            if (!fits)
                ++view.invalidSignatures;
        }
        for (size_t k = 0; k < pubkeys.size(); ++k) {
            if (seen[k])
                ++count[k];
        }
    }
    CWallet *pwallet = walletModel->getWallet();
    bool haveUnsignedKey = false;
    for (size_t k = 0; k < pubkeys.size(); ++k) {
        const bool signedAll = count[k] == (int)tx.vin.size();
        view.signedBy.append(signedAll);
        if (signedAll)
            ++view.signatures;
        if (pwallet->HaveKey(pubkeys[k].GetID())) {
            if (signedAll)
                view.mineSigned = true;
            else
                haveUnsignedKey = true;
        }
    }
    if (view.invalidSignatures > 0)
        view.problems << tr("The request carries a signature that does not fit this transaction. The transaction may have been changed after someone signed it. Ask the sender before you sign.");
    view.complete = view.signatures >= view.required;
    view.canSign = view.problems.isEmpty() && haveUnsignedKey && !view.complete;
    return view;
}

bool MultisigModel::importRequest(const MultisigRequest& received, QString& id, QString& error)
{
    MultisigRequest incoming = received;
    // the address is that of the script, whatever the request says
    incoming.address = addressOf(scriptOf(incoming.redeemScript));
    MultisigWallet item;
    if (!wallet(incoming.address, item)) {
        error = tr("This multisig wallet is not in your wallet. Add it first.");
        return false;
    }
    incoming.walletName = item.name;
    MultisigRequest existing;
    if (request(incoming.id, existing)) {
        if (!existing.txid.isEmpty()) {
            id = existing.id;
            return true;
        }
        // the same transaction: bring the signatures together
        if (existing.txHex != incoming.txHex) {
            UniValue combined;
            std::string rpcError;
            const std::string list = "[\"" + existing.txHex.toStdString() + "\",\"" + incoming.txHex.toStdString() + "\"]";
            if (!NodeRPC::call("combinerawtransaction", {list}, combined, &rpcError) || !combined.isStr()) {
                error = tr("The signatures could not be put together: %1").arg(QString::fromStdString(rpcError));
                return false;
            }
            existing.txHex = QString::fromStdString(combined.get_str());
            storeRequest(existing);
        }
        id = existing.id;
        Q_EMIT changed();
        return true;
    }
    if (incoming.created <= 0)
        incoming.created = QDateTime::currentMSecsSinceEpoch() / 1000;
    storeRequest(incoming);
    id = incoming.id;
    Q_EMIT changed();
    return true;
}

bool MultisigModel::signRequest(const QString& id, QString& error)
{
    MultisigRequest item;
    if (!request(id, item)) {
        error = tr("The request is not known.");
        return false;
    }
    const MultisigRequestView before = analyze(item);
    if (!before.canSign) {
        error = before.problems.isEmpty() ? tr("This wallet has nothing to sign in this request.") : before.problems.first();
        return false;
    }
    // signrawtransaction <hex> [{"txid","vout","scriptPubKey","redeemScript","amount"},..]
    // The coins are those this node found for the inputs, not what the request says.
    CMutableTransaction mtx;
    DecodeHexTx(mtx, item.txHex.toStdString());
    UniValue prevtxs(UniValue::VARR);
    {
        LOCK2(cs_main, ::mempool.cs);
        CCoinsViewMemPool viewMempool(pcoinsTip.get(), ::mempool);
        for (const CTxIn& in : mtx.vin) {
            Coin coin;
            if (!viewMempool.GetCoin(in.prevout, coin)) {
                error = tr("The coins of this request are no longer there.");
                return false;
            }
            UniValue prevout(UniValue::VOBJ);
            prevout.pushKV("txid", in.prevout.hash.GetHex());
            prevout.pushKV("vout", (int)in.prevout.n);
            prevout.pushKV("scriptPubKey", HexStr(coin.out.scriptPubKey.begin(), coin.out.scriptPubKey.end()));
            prevout.pushKV("redeemScript", item.redeemScript.toStdString());
            prevout.pushKV("amount", ValueFromAmount(coin.out.nValue));
            prevtxs.push_back(prevout);
        }
    }
    UniValue result;
    std::string rpcError;
    if (!NodeRPC::call("signrawtransaction", {item.txHex.toStdString(), prevtxs.write()}, result, &rpcError) || !result.isObject()) {
        error = tr("Signing failed: %1").arg(QString::fromStdString(rpcError));
        return false;
    }
    MultisigRequest signedItem = item;
    signedItem.txHex = QString::fromStdString(find_value(result, "hex").get_str());
    CMutableTransaction after;
    if (!DecodeHexTx(after, signedItem.txHex.toStdString()) || requestId(after) != item.id) {
        error = tr("Signing changed the transaction. Nothing was stored.");
        return false;
    }
    const MultisigRequestView view = analyze(signedItem);
    if (!view.mineSigned || view.signatures <= before.signatures) {
        error = tr("The signature of this wallet was not added. Is the wallet unlocked?");
        return false;
    }
    storeRequest(signedItem);
    Q_EMIT changed();
    return true;
}

bool MultisigModel::broadcastRequest(const QString& id, QString& txid, QString& error)
{
    MultisigRequest item;
    if (!request(id, item)) {
        error = tr("The request is not known.");
        return false;
    }
    const MultisigRequestView view = analyze(item);
    if (!view.complete || !view.problems.isEmpty()) {
        error = view.problems.isEmpty() ? tr("Signatures are still missing.") : view.problems.first();
        return false;
    }
    UniValue result;
    std::string rpcError;
    if (!NodeRPC::call("sendrawtransaction", {item.txHex.toStdString()}, result, &rpcError) || !result.isStr()) {
        error = tr("The network did not accept the transaction: %1").arg(QString::fromStdString(rpcError));
        return false;
    }
    txid = QString::fromStdString(result.get_str());
    item.txid = txid;
    storeRequest(item);
    Q_EMIT changed();
    return true;
}

void MultisigModel::removeRequest(const QString& id)
{
    MultisigRequest item;
    if (!request(id, item))
        return;
    CWallet *wallet = walletModel->getWallet();
    {
        LOCK(wallet->cs_wallet);
        wallet->EraseDestData(DecodeDestination(item.address.toStdString()), DATA_REQUEST + item.id.left(16).toStdString());
    }
    Q_EMIT changed();
}
