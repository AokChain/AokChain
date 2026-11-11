// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_SENDCOINSENTRY_H
#define AOKCHAIN_QT_SENDCOINSENTRY_H

#include "walletmodel.h"

#include "amount.h"

#include <QStackedWidget>

class WalletModel;

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE
class PlatformStyle;

namespace Ui {
    class SendCoinsEntry;
}

/**
 * A single entry in the dialog for sending AOK.
 * Stacked widget, with different UIs for payment requests
 * with a strong payee identity.
 */
class SendCoinsEntry : public QStackedWidget
{
    Q_OBJECT

public:
    explicit SendCoinsEntry(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SendCoinsEntry();

    void setModel(WalletModel *model);
    bool validate();
    SendCoinsRecipient getValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();

    void setValue(const SendCoinsRecipient &value);
    void setAddress(const QString &address);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
     *  (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setFocus();

    /** What the summary of the send page shows */
    QString addressText() const;
    QString labelText() const;
    CAmount amount() const;
    bool subtractsFee() const;
    /** Used by "use the whole balance" */
    void setAmount(const CAmount &amount);
    void checkSubtractFeeFromAmount();

public Q_SLOTS:
    void clear();

Q_SIGNALS:
    void removeEntry(SendCoinsEntry *entry);
    void payAmountChanged();
    void subtractFeeFromAmountChanged();
    void recipientChanged();
    void useAvailableBalance(SendCoinsEntry *entry);

private Q_SLOTS:
    void deleteClicked();
    void on_payTo_textChanged(const QString &address);
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();
    void updateDisplayUnit();
    void toggleTimeLock(bool checked);
    void useAvailableBalanceClicked();

private:
    SendCoinsRecipient recipient;
    Ui::SendCoinsEntry *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;

    QLabel *addressHint;

    bool updateLabel(const QString &address);
    void buildLayout();
    void updateAddressHint(const QString &address);
};

#endif // AOKCHAIN_QT_SENDCOINSENTRY_H
