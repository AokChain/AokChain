// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_MULTISIGMODEL_H
#define AOKCHAIN_QT_MULTISIGMODEL_H

#include "amount.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class WalletModel;

/** One participant of a multisig wallet. */
struct MultisigKey
{
    QString name;
    QString pubkey;     //!< hex, compressed
    bool mine;          //!< this wallet has the private key (looked up, never stored)
    MultisigKey() : mine(false) {}
};

/** A multisig address that this wallet takes part in or watches. */
struct MultisigWallet
{
    QString name;
    int required;               //!< signatures needed
    QList<MultisigKey> keys;    //!< in the order of the redeem script (sorted by public key)
    QString address;            //!< pay-to-script-hash address
    QString redeemScript;       //!< hex
    CAmount balance;            //!< confirmed coins of the address
    int needMySignature;        //!< open requests that this wallet has not signed
    int waiting;                //!< open requests that wait for others or for the broadcast

    MultisigWallet() : required(0), balance(0), needMySignature(0), waiting(0) {}
    bool hasMyKey() const;
    /** "2-of-3" */
    QString rule() const { return QString("%1-of-%2").arg(required).arg(keys.size()); }
};

/** A coin that a request spends, with what is needed to sign without the block chain. */
struct MultisigPrevout
{
    QString txid;
    int vout;
    QString scriptPubKey;   //!< hex
    CAmount amount;
    MultisigPrevout() : vout(0), amount(0) {}
};

/** A spend from a multisig wallet that collects signatures ("signing request"). */
struct MultisigRequest
{
    QString id;             //!< hash of the transaction without its signatures
    QString address;        //!< the multisig address
    QString walletName;
    QString redeemScript;   //!< hex
    QString memo;           //!< for the participants; not part of the transaction
    QString statedTo;       //!< what the maker of the request says it does: checked against the transaction
    CAmount statedAmount;
    QString txHex;          //!< the raw transaction with the signatures so far
    QList<MultisigPrevout> prevouts;
    QString txid;           //!< set when this wallet broadcast it
    qint64 created;
    MultisigRequest() : statedAmount(0), created(0) {}
};

/** What a request really does, read from its raw transaction and from the block chain of this node. */
struct MultisigRequestView
{
    struct Output {
        QString address;    //!< empty when the output is not a plain payment
        CAmount amount;
        bool change;        //!< back to the multisig address
    };
    bool decoded;               //!< the raw transaction could be read
    bool walletKnown;           //!< the redeem script is one of the multisig wallets of this wallet
    bool inputsFound;           //!< every input is an unspent coin that this node knows
    bool inputsMatch;           //!< every input is a coin of the multisig address
    bool spent;                 //!< inputs are gone: sent already, or used by another transaction
    QList<Output> outputs;
    CAmount inputTotal;
    CAmount amount;             //!< paid to others
    CAmount change;
    CAmount fee;
    QStringList problems;       //!< reasons not to sign; signing is not offered
    QStringList warnings;       //!< to be read before signing
    QList<bool> signedBy;       //!< per key of the wallet: a valid signature on every input
    int required;
    int signatures;             //!< number of participants who signed
    int invalidSignatures;      //!< signatures that fit no key of the wallet for this transaction
    bool complete;
    bool mineSigned;
    bool canSign;               //!< this wallet has a key that has not signed, and nothing speaks against it

    MultisigRequestView() : decoded(false), walletKnown(false), inputsFound(false), inputsMatch(false), spent(false),
        inputTotal(0), amount(0), change(0), fee(0), required(0), signatures(0), invalidSignatures(0),
        complete(false), mineSigned(false), canSign(false) {}
};

/**
 * Multisig wallets and signing requests of the GUI.
 *
 * Built on what the node has: pay-to-script-hash multisig scripts, watch-only
 * addresses, raw transactions (createrawtransaction, signrawtransaction,
 * combinerawtransaction, sendrawtransaction). Nothing here changes how
 * transactions are validated or how the wallet file is written: the list of
 * multisig wallets and the open requests are kept as "destdata" entries of
 * the multisig address in the wallet file.
 *
 * Reading balances and adding a wallet with a rescan run on a worker thread.
 */
class MultisigModel : public QObject
{
    Q_OBJECT

public:
    explicit MultisigModel(WalletModel *walletModel, QObject *parent = nullptr);
    ~MultisigModel();

    static const int MAX_KEYS = 15;     //!< a redeem script of 15 compressed keys still fits a P2SH input
    static const int QR_MAX_CHARS = 2000;

    // --- share strings -------------------------------------------------
    /** "aokms-key1:<hex>" */
    static QString keyCode(const QString& pubkeyHex);
    /** Accepts the share string or a bare hex public key. Returns the hex of a valid compressed key. */
    bool parseKeyCode(const QString& text, QString& pubkeyHex, QString& error) const;
    /** "aokms-wallet1:<base64 JSON>": what another participant needs to add the same multisig wallet */
    static QString walletCode(const MultisigWallet& wallet);
    bool parseWalletCode(const QString& text, MultisigWallet& wallet, QString& error) const;
    /** "aokms-req1:<base64 JSON>": a signing request to pass on */
    static QString requestCode(const MultisigRequest& request);
    bool parseRequestCode(const QString& text, MultisigRequest& request, QString& error) const;

    // --- signed messages -------------------------------------------------
    /** Sign a text with the key of one of the addresses of the wallet. */
    bool signMessage(const QString& address, const QString& message, QString& signature, QString& error);
    /** The reason is empty when the signature is valid. */
    bool verifyMessage(const QString& address, const QString& message, const QString& signature, QString& reason) const;
    /** Address, text and signature as one block of text to copy */
    static QString messageBlock(const QString& address, const QString& message, const QString& signature);
    static bool parseMessageBlock(const QString& text, QString& address, QString& message, QString& signature);

    // --- multisig wallets -------------------------------------------------
    /** A key of this wallet that is not part of a multisig wallet yet; made on first use. */
    bool myKey(QString& pubkeyHex, QString& error);
    /** Sort the keys, make the redeem script and the address. The wallet is not stored. */
    bool makeWallet(const QString& name, int required, const QList<MultisigKey>& keys, MultisigWallet& wallet, QString& error) const;
    /** The multisig wallet that a redeem script describes (participants without names). */
    bool walletFromRedeemScript(const QString& redeemScriptHex, MultisigWallet& wallet, QString& error) const;
    QList<MultisigWallet> wallets() const;
    bool wallet(const QString& address, MultisigWallet& wallet) const;
    /** Watch the address and remember the wallet. Runs on a worker thread; walletAdded() follows. */
    void addWallet(const MultisigWallet& wallet, bool rescan);
    /** Forget the multisig wallet and its requests. The address stays watched. */
    void removeWallet(const QString& address);

    // --- spending -------------------------------------------------
    /** Fee of a spend with this many inputs and outputs at the fee rate of the wallet */
    CAmount estimateFee(const MultisigWallet& wallet, int inputs, int outputs, int multiplier) const;
    /** What a spend of this amount would cost; false with the reason when it cannot be made. */
    bool previewSpend(const QString& address, CAmount amount, bool sendAll, int feeMultiplier, CAmount& fee, CAmount& sent, QString& error) const;
    /** Build the request, sign it with the key of this wallet when it has one, and store it. */
    bool createRequest(const QString& address, const QString& to, CAmount amount, bool sendAll, const QString& memo,
                       int feeMultiplier, QString& id, QString& error);
    QList<MultisigRequest> requests(const QString& address = QString()) const;
    bool request(const QString& id, MultisigRequest& request) const;
    MultisigRequestView analyze(const MultisigRequest& request) const;
    /** Store a request that was received; signatures are merged when it is known already. */
    bool importRequest(const MultisigRequest& received, QString& id, QString& error);
    /** Add the signature of this wallet. The caller unlocks the wallet. */
    bool signRequest(const QString& id, QString& error);
    bool broadcastRequest(const QString& id, QString& txid, QString& error);
    void removeRequest(const QString& id);

public Q_SLOTS:
    /** Read the balances again; changed() follows. */
    void refresh();

Q_SIGNALS:
    /** Wallets, balances or requests changed */
    void changed();
    void walletAdded(const QString& address, const QString& error);

private:
    WalletModel *walletModel;
    bool refreshing;
    bool refreshAgain;

    /** The multisig wallets without the count of their open requests */
    QList<MultisigWallet> storedWallets() const;
    void storeRequest(const MultisigRequest& request);
    void markKeysUsed(const MultisigWallet& wallet);
};

#endif // AOKCHAIN_QT_MULTISIGMODEL_H
