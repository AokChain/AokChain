// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_SENDCONFIRMDIALOG_H
#define AOKCHAIN_QT_SENDCONFIRMDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

class QPushButton;

/** What a send is about to do, as the confirmation shows it. Amounts are plain numbers ("1234.5"). */
struct SendConfirmData
{
    struct Recipient {
        QString label;      //!< of the address book, may be empty
        QString address;
        QString amount;     //!< in units of the asset
        QString note;       //!< such as "locked until ..."
        bool known;         //!< in the address book or an address of this wallet
        Recipient() : known(true) {}
    };

    bool token;             //!< a token is sent; false: the coin
    QString asset;          //!< token name; for the coin its unit name ("AOK")
    QString source;         //!< where the coins come from when it is not this wallet (a multisig wallet)
    QList<Recipient> recipients;
    QString total;          //!< sum of the recipients, in units of the asset
    QString fee;            //!< in the coin; empty when there is none
    QString feeUnit;        //!< unit name of the coin
    QString feeNote;        //!< such as the size of the transaction, or "estimate"
    QString assetDeducted;  //!< what leaves the balance of the asset (for the coin: amount and fee)
    QString assetAfter;     //!< balance of the asset afterwards
    QString coinAfter;      //!< token sends: balance of the coin after the fee
    bool wholeBalance;      //!< the send empties the balance of the asset
    QString sendText;       //!< text of the confirming button, default "Send"
    QStringList extraWarnings;

    SendConfirmData() : token(false), wholeBalance(false) {}
};

/**
 * The question before coins or tokens leave: which asset, how much, to whom,
 * at what fee. One dialog for every way of sending. The button that sends is
 * disabled for the first seconds and is never the default button, so neither
 * a quick click nor the Enter key sends by accident.
 */
class SendConfirmDialog : public QDialog
{
    Q_OBJECT

public:
    SendConfirmDialog(const SendConfirmData &data, QWidget *parent);

    /** Ask; true when the user chose to send */
    static bool confirm(const SendConfirmData &data, QWidget *parent);
    /** After a successful send: what was sent and the transaction id, with a button to copy it */
    static void showSent(const SendConfirmData &data, const QString &txid, QWidget *parent);
    /** After a failed send */
    static void showFailed(const QString &reason, QWidget *parent);

    /** An address in groups of four characters, easier to compare */
    static QString groupedAddress(const QString &address);

private Q_SLOTS:
    void countDown();

private:
    QPushButton *sendButton;
    QString sendText;
    QTimer timer;
    int remaining;
};

#endif // AOKCHAIN_QT_SENDCONFIRMDIALOG_H
