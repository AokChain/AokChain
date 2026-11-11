// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletview.h"

#include "addressbookpage.h"
#include "askpassphrasedialog.h"
#include "aokchaingui.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "overviewpage.h"
#include "platformstyle.h"
#include "receivecoinsdialog.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "transactiontablemodel.h"
#include "tokentablemodel.h"
#include "transactionview.h"
#include "walletmodel.h"
#include "tokensdialog.h"
#include "createtokendialog.h"
#include "explorerpage.h"
#include "reissuetokendialog.h"
#include <validation.h>

#include "ui_interface.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>


WalletView::WalletView(const PlatformStyle *_platformStyle, QWidget *parent):
    QStackedWidget(parent),
    clientModel(0),
    walletModel(0),
    platformStyle(_platformStyle)
{
    // Create the pages. Each one sits below a header with its title.
    overviewPage = new OverviewPage(platformStyle);
    overviewShell = addPage(overviewPage, tr("Overview"), overviewPage->headerWidget());

    transactionView = new TransactionView(platformStyle, this);
    QPushButton *exportButton = new QPushButton(tr("Export to CSV"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    exportButton->setIcon(QIcon(GUIUtil::lineIcon("download", 15, GUIUtil::theme().text, 2.2)));
    exportButton->setIconSize(QSize(15, 15));
    exportButton->setFixedHeight(40);
    exportButton->setCursor(Qt::PointingHandCursor);
    transactionsPage = addPage(transactionView, tr("Transactions"), exportButton);

    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    receiveShell = addPage(receiveCoinsPage, tr("Receive"));
    sendCoinsPage = new SendCoinsDialog(platformStyle);
    sendShell = addPage(sendCoinsPage, tr("Send"), sendCoinsPage->headerWidget());

    /** TOKENS START */
    tokensPage = new TokensDialog(platformStyle);
    tokensShell = addPage(tokensPage, tr("Tokens"));
    createTokensPage = new CreateTokenDialog(platformStyle);
    createTokensShell = addPage(createTokensPage, tr("Create Tokens"));
    manageTokensPage = new ReissueTokenDialog(platformStyle);
    manageTokensShell = addPage(manageTokensPage, tr("Manage Tokens"));
    /** TOKENS END */

    explorerPage = new ExplorerPage(platformStyle);
    explorerShell = addPage(explorerPage, tr("Explorer"), explorerPage->headerWidget());

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));
    connect(overviewPage, SIGNAL(outOfSyncWarningClicked()), this, SLOT(requestedSyncWarningInfo()));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, SIGNAL(clicked()), transactionView, SLOT(exportClicked()));

    // Pass through messages from sendCoinsPage
    connect(sendCoinsPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    // Pass through messages from transactionView
    connect(transactionView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

    /** TOKENS START */
    connect(tokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(createTokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(manageTokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(overviewPage, SIGNAL(tokenSendClicked(QModelIndex)), tokensPage, SLOT(focusToken(QModelIndex)));
    connect(overviewPage, SIGNAL(tokenIssueSubClicked(QModelIndex)), createTokensPage, SLOT(focusSubToken(QModelIndex)));
    connect(overviewPage, SIGNAL(tokenIssueUniqueClicked(QModelIndex)), createTokensPage, SLOT(focusUniqueToken(QModelIndex)));
    connect(overviewPage, SIGNAL(tokenReissueClicked(QModelIndex)), manageTokensPage, SLOT(focusReissueToken(QModelIndex)));

    // Transactions, addresses and blocks can be looked up in the explorer
    connect(transactionView, SIGNAL(explorerRequested(QString)), this, SIGNAL(explorerRequested(QString)));
    /** RNV END */
}

WalletView::~WalletView()
{
}

QWidget *WalletView::addPage(QWidget *page, const QString &title, QWidget *context)
{
    QWidget *shell = new QWidget(this);
    shell->setObjectName("pageShell");
    QVBoxLayout *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(36, 26, 36, 28);
    layout->setSpacing(20);

    QHBoxLayout *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(12);
    QLabel *titleLabel = new QLabel(title, shell);
    titleLabel->setProperty("role", "pageTitle");
    titleLabel->setFont(GUIUtil::appFont(24, QFont::Bold));
    titleLabel->setMinimumHeight(40);
    header->addWidget(titleLabel);
    header->addStretch();
    if (context)
        header->addWidget(context, 0, Qt::AlignVCenter);
    layout->addLayout(header);

    // the margins belong to the shell: drop those of the form and of the layout directly inside it
    if (page->layout()) {
        page->layout()->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < page->layout()->count(); ++i) {
            if (QLayout *inner = page->layout()->itemAt(i)->layout()) {
                inner->setContentsMargins(0, 0, 0, 0);
                if (inner->spacing() > 20)
                    inner->setSpacing(20);
            }
        }
    }
    layout->addWidget(page, 1);
    addWidget(shell);
    return shell;
}

void WalletView::setAokChainGUI(AokChainGUI *gui)
{
    if (gui)
    {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoHistoryPage()));

        // The buttons and links of the overview page
        connect(overviewPage, SIGNAL(sendClicked()), gui, SLOT(gotoSendCoinsPage()));
        connect(overviewPage, SIGNAL(receiveClicked()), gui, SLOT(gotoReceiveCoinsPage()));
        connect(overviewPage, SIGNAL(allTransactionsClicked()), gui, SLOT(gotoHistoryPage()));
        connect(overviewPage, SIGNAL(allTokensClicked()), gui, SLOT(gotoTokensPage()));

        // Look something up in the explorer
        connect(this, SIGNAL(explorerRequested(QString)), gui, SLOT(gotoExplorerPage(QString)));
        connect(explorerPage, SIGNAL(sendToAddress(QString)), gui, SLOT(gotoSendCoinsPage(QString)));

        // Clicking on a token menu item Send
        connect(overviewPage, SIGNAL(tokenSendClicked(QModelIndex)), gui, SLOT(gotoTokensPage()));

        // Clicking on a token menu item Issue Sub
        connect(overviewPage, SIGNAL(tokenIssueSubClicked(QModelIndex)), gui, SLOT(gotoCreateTokensPage()));

        // Clicking on a token menu item Issue Unique
        connect(overviewPage, SIGNAL(tokenIssueUniqueClicked(QModelIndex)), gui, SLOT(gotoCreateTokensPage()));

        // Clicking on a token menu item Reissue
        connect(overviewPage, SIGNAL(tokenReissueClicked(QModelIndex)), gui, SLOT(gotoManageTokensPage()));

        // Receive and report messages
        connect(this, SIGNAL(message(QString,QString,unsigned int)), gui, SLOT(message(QString,QString,unsigned int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Pass through transaction notifications
        connect(this, SIGNAL(incomingTransaction(QString,int,CAmount,QString,QString,QString,QString)), gui, SLOT(incomingTransaction(QString,int,CAmount,QString,QString,QString,QString)));

        // Connect HD enabled state signal 
        connect(this, SIGNAL(hdEnabledStatusChanged(int)), gui, SLOT(setHDStatus(int)));

        // Pass through checkTokens calls to the GUI
        connect(this, SIGNAL(checkTokens()), gui, SLOT(checkTokens()));
    }
}

void WalletView::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    overviewPage->setClientModel(_clientModel);
    sendCoinsPage->setClientModel(_clientModel);
    explorerPage->setClientModel(_clientModel);
}

void WalletView::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;

    // Put transaction list in tabs
    transactionView->setModel(_walletModel);
    overviewPage->setWalletModel(_walletModel);
    receiveCoinsPage->setModel(_walletModel);
    sendCoinsPage->setModel(_walletModel);
    explorerPage->setWalletModel(_walletModel);
    usedReceivingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);
    usedSendingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);

    /** TOKENS START */
    tokensPage->setModel(_walletModel);
    createTokensPage->setModel(_walletModel);
    manageTokensPage->setModel(_walletModel);

    if (_walletModel)
    {
        // Receive and pass through messages from wallet model
        connect(_walletModel, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

        // Handle changes in encryption status
        connect(_walletModel, SIGNAL(encryptionStatusChanged(int)), this, SIGNAL(encryptionStatusChanged(int)));
        updateEncryptionStatus();

        // update HD status
        Q_EMIT hdEnabledStatusChanged(_walletModel->hdEnabled());

        // Balloon pop-up for new transaction
        connect(_walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(processNewTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(_walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));

        // Show progress dialog
        connect(_walletModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int end)
{
    /** Everytime we get an new transaction. We should check to see if tokens are enabled or not */
    overviewPage->showTokens();
    
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    /** TOKENS START */
    // With the addition of token transactions, there can be multiple transaction that need notifications
    // so we need to loop through all new transaction that were added to the transaction table and display
    // notifications for each individual transaction
    QString tokenName = "";
    for (int i = start; i <= end; i++) {
        QString date = ttm->index(i, TransactionTableModel::Date, parent).data().toString();
        qint64 amount = ttm->index(i, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
        QString type = ttm->index(i, TransactionTableModel::Type, parent).data().toString();
        QModelIndex index = ttm->index(i, 0, parent);
        QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
        QString label = ttm->data(index, TransactionTableModel::LabelRole).toString();
        tokenName = ttm->data(index, TransactionTableModel::TokenNameRole).toString();

        Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, label,
                                   tokenName);
    }
    /** TOKENS END */

    
    transactionView->showTokens();
    Q_EMIT checkTokens();

    tokensPage->processNewTransaction();
    createTokensPage->updateTokenList();
    manageTokensPage->updateTokensList();

}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewShell);
    Q_EMIT checkTokens();
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveShell);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendShell);

    if (!addr.isEmpty())
        sendCoinsPage->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::updateEncryptionStatus()
{
    Q_EMIT encryptionStatusChanged(walletModel->getEncryptionStatus());
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt : AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->backupWallet(filename)) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::unlockWallet()
{
    if (!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

bool WalletView::isWalletUnlocked()
{
    if (!walletModel)
        return false;

    return walletModel->getEncryptionStatus() != WalletModel::Locked;
}

unsigned long long WalletView::updateWeight()
{
    if(!walletModel)
        return 0;
    return walletModel->updateWeight();
}

void WalletView::usedSendingAddresses()
{
    if(!walletModel)
        return;

    usedSendingAddressesPage->show();
    usedSendingAddressesPage->raise();
    usedSendingAddressesPage->activateWindow();
}

void WalletView::usedReceivingAddresses()
{
    if(!walletModel)
        return;

    usedReceivingAddressesPage->show();
    usedReceivingAddressesPage->raise();
    usedReceivingAddressesPage->activateWindow();
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void WalletView::requestedSyncWarningInfo()
{
    Q_EMIT outOfSyncWarningClicked();
}

bool fFirstVisit = true;
/** TOKENS START */
void WalletView::gotoTokensPage()
{
    if (fFirstVisit){
        fFirstVisit = false;
        tokensPage->handleFirstSelection();
    }
    setCurrentWidget(tokensShell);
    tokensPage->focusTokenListBox();
}

void WalletView::gotoCreateTokensPage()
{
    setCurrentWidget(createTokensShell);
}

void WalletView::gotoManageTokensPage()
{
    setCurrentWidget(manageTokensShell);
}

void WalletView::gotoExplorerPage(QString query)
{
    setCurrentWidget(explorerShell);
    if (!query.isEmpty())
        explorerPage->lookup(query);
    else
        explorerPage->focusSearch();
}
/** TOKENS END */