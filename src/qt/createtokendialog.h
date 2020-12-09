// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_CREATETOKENDIALOG_H
#define AOKCHAIN_QT_CREATETOKENDIALOG_H

#include "walletmodel.h"

#include <QDialog>

class PlatformStyle;
class WalletModel;
class ClientModel;

namespace Ui {
    class CreateTokenDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
class QStringListModel;
class QSortFilterProxyModel;
class QCompleter;
QT_END_NAMESPACE

/** Dialog showing transaction details. */
class CreateTokenDialog : public QDialog
{
Q_OBJECT

public:
    explicit CreateTokenDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~CreateTokenDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

    int type;
    QString format;


    void setupCoinControlFrame(const PlatformStyle *platformStyle);
    void setupTokenDataView(const PlatformStyle *platformStyle);
    void setupFeeControl(const PlatformStyle *platformStyle);

    void updateTokenList();

    void clear();
    void selectTypeName(int type, QString name);

    QStringListModel* stringModel;
    QSortFilterProxyModel* proxy;
    QCompleter* completer;

private:
    Ui::CreateTokenDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    bool fFeeMinimized;
    const PlatformStyle *platformStyle;

    bool checkedAvailablity = false;

    void setUpValues();
    void showMessage(QString string);
    void showValidMessage(QString string);
    void hideMessage();
    void disableCreateButton();
    void enableCreateButton();
    void CheckFormState();
    void updatePresentedTokenName(QString name);
    QString GetSpecialCharacter();
    QString GetTokenName();
    void UpdateTokenNameMaxSize();
    void UpdateTokenNameToUpper();
    void setUniqueSelected();
    void clearSelected();

    //CoinControl
    // Update the passed in CCoinControl with state from the GUI
    void updateCoinControlState(CCoinControl& ctrl);

    //Fee
    void updateFeeMinimizedLabel();
    void minimizeFeeSection(bool fMinimize);

private Q_SLOTS:
    void checkAvailabilityClicked();
    void onNameChanged(QString name);
    void onAddressNameChanged(QString address);
    void onCreateTokenClicked();
    void onUnitChanged(int value);
    void onChangeAddressChanged(QString changeAddress);
    void onTokenTypeActivated(int index);
    void onTokenListActivated(int index);
    void onClearButtonClicked();

    //CoinControl
    void coinControlFeatureChanged(bool);
    void coinControlButtonClicked();
    void coinControlChangeChecked(int);
    void coinControlChangeEdited(const QString &);
    void coinControlClipboardQuantity();
    void coinControlClipboardAmount();
    void coinControlClipboardFee();
    void coinControlClipboardAfterFee();
    void coinControlClipboardBytes();
    void coinControlClipboardLowOutput();
    void coinControlClipboardChange();
    void coinControlUpdateLabels();

    //Fee
    void on_buttonChooseFee_clicked();
    void on_buttonMinimizeFee_clicked();
    void setMinimumFee();
    void updateFeeSectionControls();
    void updateMinFeeLabel();
    void updateSmartFeeLabel();
    void feeControlFeatureChanged(bool);

    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);
    void updateDisplayUnit();


    void focusSubToken(const QModelIndex& index);
    void focusUniqueToken(const QModelIndex& index);

protected:
    bool eventFilter( QObject* sender, QEvent* event);


Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};

#endif // AOKCHAIN_QT_CREATETOKENDIALOG_H
