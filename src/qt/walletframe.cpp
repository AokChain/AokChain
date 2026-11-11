// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletframe.h"

#include "aokchaingui.h"
#include "walletview.h"

#include <cassert>
#include <cstdio>

#include "guiutil.h"
#include "util.h"
#include "walletmodel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

WalletFrame::WalletFrame(const PlatformStyle *_platformStyle, AokChainGUI *_gui) :
    QFrame(_gui),
    gui(_gui),
    platformStyle(_platformStyle)
{
    // Leave HBox hook for adding a list view later
    QVBoxLayout *walletFrameLayout = new QVBoxLayout(this);
    setContentsMargins(0,0,0,0);
    walletFrameLayout->setContentsMargins(0,0,0,0);
    walletFrameLayout->setSpacing(0);

    // A wallet without a passphrase: a warning above the pages until it is encrypted
    // (or closed, for this run of the program).
    encryptBanner = new QFrame(this);
    encryptBanner->setObjectName("encryptBanner");
    encryptBanner->setStyleSheet(QString("QFrame#encryptBanner { background-color: %1; border: none; border-bottom: 1px solid %2; } QFrame#encryptBanner QLabel { color: %3; background: transparent; }")
        .arg(GUIUtil::theme().warnBg.name(), GUIUtil::theme().warnBorder.name(), GUIUtil::theme().warnText.name()));
    QHBoxLayout *bannerLayout = new QHBoxLayout(encryptBanner);
    bannerLayout->setContentsMargins(36, 8, 20, 8);
    bannerLayout->setSpacing(12);
    QLabel *bannerIcon = new QLabel(encryptBanner);
    bannerIcon->setPixmap(GUIUtil::lineIcon("lock", 16, GUIUtil::theme().warnText, 2.0));
    bannerLayout->addWidget(bannerIcon);
    QLabel *bannerText = new QLabel(tr("This wallet is not encrypted. Anyone who copies the wallet file or uses this computer can send the coins."), encryptBanner);
    bannerText->setWordWrap(true);
    bannerLayout->addWidget(bannerText, 1);
    QPushButton *bannerEncrypt = new QPushButton(tr("Encrypt"), encryptBanner);
    bannerEncrypt->setProperty("primary", true);
    bannerEncrypt->setFixedHeight(32);
    bannerEncrypt->setCursor(Qt::PointingHandCursor);
    bannerLayout->addWidget(bannerEncrypt);
    QPushButton *bannerClose = new QPushButton(encryptBanner);
    bannerClose->setProperty("look", "icon");
    bannerClose->setIcon(QIcon(GUIUtil::lineIcon("remove", 14, GUIUtil::theme().warnText, 2.0)));
    bannerClose->setFixedSize(28, 28);
    bannerClose->setToolTip(tr("Hide until the next start"));
    bannerClose->setCursor(Qt::PointingHandCursor);
    bannerLayout->addWidget(bannerClose);
    encryptBanner->hide();
    walletFrameLayout->addWidget(encryptBanner);
    connect(bannerEncrypt, &QPushButton::clicked, this, [this] { encryptWallet(true); });
    connect(bannerClose, &QPushButton::clicked, this, [this] { bannerDismissed = true; encryptBanner->hide(); });

    walletStack = new QStackedWidget(this);
    walletFrameLayout->addWidget(walletStack, 1);

    QLabel *noWallet = new QLabel(tr("No wallet has been loaded."));
    noWallet->setAlignment(Qt::AlignCenter);
    walletStack->addWidget(noWallet);
}

WalletFrame::~WalletFrame()
{
}

void WalletFrame::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

bool WalletFrame::addWallet(const QString& name, WalletModel *walletModel)
{
    if (!gui || !clientModel || !walletModel || mapWalletViews.count(name) > 0)
        return false;

    WalletView *walletView = new WalletView(platformStyle, this);
    walletView->setAokChainGUI(gui);
    walletView->setClientModel(clientModel);
    walletView->setWalletModel(walletModel);
    walletView->showOutOfSyncWarning(bOutOfSync);

     /* TODO we should goto the currently selected page once dynamically adding wallets is supported */
    walletView->gotoOverviewPage();
    walletStack->addWidget(walletView);
    mapWalletViews[name] = walletView;

    // Ensure a walletView is able to show the main window
    connect(walletView, SIGNAL(showNormalIfMinimized()), gui, SLOT(showNormalIfMinimized()));

    connect(walletView, SIGNAL(outOfSyncWarningClicked()), this, SLOT(outOfSyncWarningClicked()));
    connect(walletView, SIGNAL(encryptionStatusChanged(int)), this, SLOT(updateEncryptBanner(int)));
    updateEncryptBanner(walletModel->getEncryptionStatus());

    return true;
}

bool WalletFrame::setCurrentWallet(const QString& name)
{
    if (mapWalletViews.count(name) == 0)
        return false;

    WalletView *walletView = mapWalletViews.value(name);
    walletStack->setCurrentWidget(walletView);
    assert(walletView);
    walletView->updateEncryptionStatus();
    return true;
}

bool WalletFrame::removeWallet(const QString &name)
{
    if (mapWalletViews.count(name) == 0)
        return false;

    WalletView *walletView = mapWalletViews.take(name);
    walletStack->removeWidget(walletView);
    return true;
}

void WalletFrame::removeAllWallets()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        walletStack->removeWidget(i.value());
    mapWalletViews.clear();
}

bool WalletFrame::handlePaymentRequest(const SendCoinsRecipient &recipient)
{
    WalletView *walletView = currentWalletView();
    if (!walletView)
        return false;

    return walletView->handlePaymentRequest(recipient);
}

void WalletFrame::showOutOfSyncWarning(bool fShow)
{
    bOutOfSync = fShow;
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->showOutOfSyncWarning(fShow);
}

void WalletFrame::gotoOverviewPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoOverviewPage();
}

void WalletFrame::gotoHistoryPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoHistoryPage();
}

void WalletFrame::gotoReceiveCoinsPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoReceiveCoinsPage();
}

void WalletFrame::gotoSendCoinsPage(QString addr)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoSendCoinsPage(addr);
}

void WalletFrame::gotoSignMessageTab(QString addr)
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->gotoSignMessageTab(addr);
}

void WalletFrame::gotoVerifyMessageTab(QString addr)
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->gotoVerifyMessageTab(addr);
}

void WalletFrame::encryptWallet(bool status)
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->encryptWallet(status);
}

void WalletFrame::backupWallet()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->backupWallet();
}

void WalletFrame::changePassphrase()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->changePassphrase();
}

void WalletFrame::unlockWallet()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->unlockWallet();
}

void WalletFrame::updateEncryptBanner(int status)
{
    // (-skipencryptprompt: tests and scripts that drive the window)
    encryptBanner->setVisible(status == WalletModel::Unencrypted && !bannerDismissed && !gArgs.GetBoolArg("-skipencryptprompt", false));
}

void WalletFrame::unlockWalletForStaking()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->unlockWalletForStaking();
}

void WalletFrame::lockWalletAfterStaking()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->lockWalletAfterStaking();
}

bool WalletFrame::isWalletUnlocked()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        return walletView->isWalletUnlocked();
    return false;
}

unsigned long long WalletFrame::updateWeight()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        return walletView->updateWeight();
    return 0;
}

void WalletFrame::usedSendingAddresses()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->usedSendingAddresses();
}

void WalletFrame::usedReceivingAddresses()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->usedReceivingAddresses();
}

WalletView *WalletFrame::currentWalletView()
{
    return qobject_cast<WalletView*>(walletStack->currentWidget());
}

void WalletFrame::outOfSyncWarningClicked()
{
    Q_EMIT requestedSyncWarningInfo();
}

/** TOKENS START */
void WalletFrame::gotoTokensPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoTokensPage();
}

void WalletFrame::gotoCreateTokensPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoCreateTokensPage();
}

void WalletFrame::gotoManageTokensPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoManageTokensPage();
}

void WalletFrame::gotoSettingsPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoSettingsPage();
}

void WalletFrame::gotoExplorerPage(QString query)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoExplorerPage(query);
}
