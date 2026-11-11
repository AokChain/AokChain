// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "aokchaingui.h"

#include "pastblocks.h"
#include "snapshot.h"
#include "bootstrap.h"

#include "aokchainunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "modaloverlay.h"
#include "networkstyle.h"
#include "notificator.h"
#include "openuridialog.h"
#include "optionsdialog.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "rpcconsole.h"
#include "utilitydialog.h"

#ifdef ENABLE_WALLET
#include "walletframe.h"
#include "sidebar.h"
#include "walletmodel.h"
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "chainparams.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "core_io.h"
#include "wallet/wallet.h"

#include <iostream>

#include <QDebug>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QToolButton>
#include <QPushButton>
#include <QPainter>
#include <QWidgetAction>
#include <QAction>
#include <QFileDialog>
#include <QApplication>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDragEnterEvent>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#if QT_VERSION < 0x050000
#include <QTextDocument>
#include <QUrl>
#else
#include <QUrlQuery>
#include <validation.h>
#include <tinyformat.h>
#include <QFontDatabase>

#endif

const std::string AokChainGUI::DEFAULT_UIPLATFORM =
#if defined(Q_OS_MAC)
        "macosx"
#elif defined(Q_OS_WIN)
        "windows"
#else
        "other"
#endif
        ;

/** Display name for default wallet name. Uses tilde to avoid name
 * collisions in the future with additional wallets */
const QString AokChainGUI::DEFAULT_WALLET = "~Default";

double GetPoSKernelPS();

AokChainGUI::AokChainGUI(const PlatformStyle *_platformStyle, const NetworkStyle *networkStyle, QWidget *parent) :
    QMainWindow(parent),
    enableWallet(false),
    clientModel(0),
    walletFrame(0),
    labelWalletEncryptionIcon(0),
    labelWalletHDStatusIcon(0),
    connectionsControl(0),
    labelBlocksIcon(0),
    progressBarLabel(0),
    labelStakingIcon(0),
    progressBar(0),
    progressDialog(0),
    appMenuBar(0),
    overviewAction(0),
    historyAction(0),
    quitAction(0),
    sendCoinsAction(0),
    sendCoinsMenuAction(0),
    usedSendingAddressesAction(0),
    usedReceivingAddressesAction(0),
    signMessageAction(0),
    verifyMessageAction(0),
    aboutAction(0),
    receiveCoinsAction(0),
    receiveCoinsMenuAction(0),
    optionsAction(0),
    toggleHideAction(0),
    encryptWalletAction(0),
    backupWalletAction(0),
    changePassphraseAction(0),
    aboutQtAction(0),
    openRPCConsoleAction(0),
    openAction(0),
    showHelpMessageAction(0),
    transferTokenAction(0),
    createTokenAction(0),
    manageTokenAction(0),
    explorerAction(0),
    footerWidget(0),
    stakingButton(0),
    sidebar(0),
    trayIcon(0),
    trayIconMenu(0),
    notificator(0),
    rpcConsole(0),
    helpMessageDialog(0),
    modalOverlay(0),
    bootstrapActive(false),
    prevBlocks(0),
    spinnerFrame(0),
    nWeight(0),
    nStaking(false),
    fSyncing(false),
    platformStyle(_platformStyle)

{
    QSettings settings;
    if (!restoreGeometry(settings.value("MainWindowGeometry").toByteArray())) {
        // Restore failed (perhaps missing setting), center the window
        move(QApplication::desktop()->availableGeometry().center() - frameGeometry().center());
    }

    QString windowTitle = tr(PACKAGE_NAME) + " - ";
#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    if(enableWallet)
    {
        windowTitle += tr("Wallet");
    } else {
        windowTitle += tr("Node");
    }
    windowTitle += " " + networkStyle->getTitleAddText();
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
    // MacDockIconHandler::instance()->setIcon(QIcon(":icons/about_aok"));
#endif
    setWindowTitle(windowTitle);

#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras.
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    rpcConsole = new RPCConsole(_platformStyle, 0);
    helpMessageDialog = new HelpMessageDialog(this, false);
#ifdef ENABLE_WALLET
    if(enableWallet)
    {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame(_platformStyle, this);
        setCentralWidget(walletFrame);
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
    }

    // Accept D&D of URIs
    setAcceptDrops(true);

    loadFonts();


    footerWidget = new QWidget();

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    createTrayIcon(networkStyle);

    // Create status bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);
    // With a wallet the side bar shows the status; the widgets of the status bar stay alive
    if (walletFrame)
        statusBar()->hide();

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    labelWalletEncryptionIcon = new QLabel();
    labelWalletHDStatusIcon = new QLabel();
    connectionsControl = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();
    labelStakingIcon = new GUIUtil::ClickableLabel();

    QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
    connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
    timerStakingIcon->start(30 * 1000);
    this->nStaking = gArgs.GetBoolArg("-staking", false);
    updateStakingIcon();

    if(enableWallet)
    {
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(labelWalletEncryptionIcon);
        frameBlocksLayout->addWidget(labelWalletHDStatusIcon);
    }

    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(connectionsControl);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);


    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    connect(connectionsControl, SIGNAL(clicked(QPoint)), this, SLOT(toggleNetworkActive()));
    connect(labelStakingIcon, SIGNAL(clicked(QPoint)), this, SLOT(toggleStakingActive()));

    modalOverlay = new ModalOverlay(this->centralWidget());
    connect(modalOverlay, SIGNAL(bootstrapSkipRequested()), this, SLOT(skipBootstrap()));
    connect(modalOverlay, SIGNAL(bootstrapFolderRequested()), this, SLOT(chooseBootstrapFolder()));
    QTimer *bootstrapTimer = new QTimer(this);
    connect(bootstrapTimer, SIGNAL(timeout()), this, SLOT(updateBootstrap()));
    bootstrapTimer->start(1000);
    bootstrapSamplesAreBlocks = false;
    pastBlocksAsked = false;
    pastBlocksWereFetching = false;
#ifdef ENABLE_WALLET
    if(enableWallet) {
        connect(walletFrame, SIGNAL(requestedSyncWarningInfo()), this, SLOT(showModalOverlay()));
        connect(labelBlocksIcon, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
        connect(progressBar, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
    }
#endif
}

AokChainGUI::~AokChainGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void AokChainGUI::loadFonts()
{
    // The fonts are registered once for the application, see GUIUtil::loadAppFonts()
}


void AokChainGUI::createActions()
{
    // size and weight of the page tabs come from the style sheet
    QFont font = GUIUtil::appFont(14, QFont::Medium);

    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/overview", ":/icons/overview"), tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    overviewAction->setFont(font);
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/send", ":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to an AokChain address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    sendCoinsAction->setFont(font);
    tabGroup->addAction(sendCoinsAction);

    sendCoinsMenuAction = new QAction(platformStyle->TextColorIcon(":/icons/send"), sendCoinsAction->text(), this);
    sendCoinsMenuAction->setStatusTip(sendCoinsAction->statusTip());
    sendCoinsMenuAction->setToolTip(sendCoinsMenuAction->statusTip());

    receiveCoinsAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/receiving_addresses", ":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(tr("Request payments (generates QR codes and aokchain: URIs)"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    receiveCoinsAction->setFont(font);
    tabGroup->addAction(receiveCoinsAction);

    receiveCoinsMenuAction = new QAction(platformStyle->TextColorIcon(":/icons/receiving_addresses"), receiveCoinsAction->text(), this);
    receiveCoinsMenuAction->setStatusTip(receiveCoinsAction->statusTip());
    receiveCoinsMenuAction->setToolTip(receiveCoinsMenuAction->statusTip());

    historyAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/history", ":/icons/history"), tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    historyAction->setFont(font);
    tabGroup->addAction(historyAction);

    /** TOKENS START */
    transferTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/token_transfer", ":/icons/token_transfer"), tr("T&okens"), this);
    transferTokenAction->setStatusTip(tr("Transfer tokens to AOK addresses"));
    transferTokenAction->setToolTip(transferTokenAction->statusTip());
    transferTokenAction->setCheckable(true);
    transferTokenAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    transferTokenAction->setFont(font);
    tabGroup->addAction(transferTokenAction);

    createTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/token_create", ":/icons/token_create"), tr("&Create Tokens"), this);
    createTokenAction->setStatusTip(tr("Create new main/sub/unique tokens"));
    createTokenAction->setToolTip(createTokenAction->statusTip());
    createTokenAction->setCheckable(true);
    createTokenAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    createTokenAction->setFont(font);
    tabGroup->addAction(createTokenAction);

    manageTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/token_manage", ":/icons/token_manage"), tr("&Manage Tokens"), this);
    manageTokenAction->setStatusTip(tr("Manage tokens you are the administrator of"));
    manageTokenAction->setToolTip(manageTokenAction->statusTip());
    manageTokenAction->setCheckable(true);
    manageTokenAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
    manageTokenAction->setFont(font);
    tabGroup->addAction(manageTokenAction);

    /** TOKENS END */

    explorerAction = new QAction(tr("&Explorer"), this);
    explorerAction->setStatusTip(tr("Look up transactions, addresses and blocks on this node"));
    explorerAction->setToolTip(explorerAction->statusTip());
    explorerAction->setCheckable(true);
    explorerAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_8));
    explorerAction->setFont(font);
    tabGroup->addAction(explorerAction);

    // Pages for what the menus of the window offered
    addressBookAction = new QAction(tr("&Address book"), this);
    addressBookAction->setStatusTip(tr("Show the receiving and the sending addresses and their labels"));
    addressBookAction->setToolTip(addressBookAction->statusTip());
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_9));
    addressBookAction->setFont(font);
    tabGroup->addAction(addressBookAction);

    signPageAction = new QAction(tr("Si&gn message"), this);
    signPageAction->setStatusTip(tr("Sign messages with your addresses and verify signed messages"));
    signPageAction->setToolTip(signPageAction->statusTip());
    signPageAction->setCheckable(true);
    signPageAction->setFont(font);
    tabGroup->addAction(signPageAction);

    settingsPageAction = new QAction(tr("Se&ttings"), this);
    settingsPageAction->setStatusTip(tr("Options, wallet management and information"));
    settingsPageAction->setToolTip(settingsPageAction->statusTip());
    settingsPageAction->setCheckable(true);
    settingsPageAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_0));
    settingsPageAction->setFont(font);
    tabGroup->addAction(settingsPageAction);

#ifdef ENABLE_WALLET
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
    connect(signPageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(signPageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(settingsPageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(settingsPageAction, SIGNAL(triggered()), this, SLOT(gotoSettingsPage()));
    // These showNormalIfMinimized are needed because Send Coins and Receive Coins
    // can be triggered from the tray menu, and need to show the GUI to be useful.
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(transferTokenAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(transferTokenAction, SIGNAL(triggered()), this, SLOT(gotoTokensPage()));
    connect(createTokenAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(createTokenAction, SIGNAL(triggered()), this, SLOT(gotoCreateTokensPage()));
    connect(manageTokenAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(manageTokenAction, SIGNAL(triggered()), this, SLOT(gotoManageTokensPage()));
    connect(explorerAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(explorerAction, SIGNAL(triggered()), this, SLOT(gotoExplorerPage()));
    // TODO add messaging actions to go to messaging page when clicked
    // TODO add voting actions to go to voting page when clicked
#endif // ENABLE_WALLET

    quitAction = new QAction(platformStyle->TextColorIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(platformStyle->TextColorIcon(":/icons/about_aok"), tr("&About %1").arg(tr(PACKAGE_NAME)), this);
    aboutAction->setStatusTip(tr("Show information about %1").arg(tr(PACKAGE_NAME)));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);
    aboutQtAction = new QAction(platformStyle->TextColorIcon(":/icons/about_qt"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(platformStyle->TextColorIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for %1").arg(tr(PACKAGE_NAME)));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);
    toggleHideAction = new QAction(platformStyle->TextColorIcon(":/icons/about_aok"), tr("&Show / Hide"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

    encryptWalletAction = new QAction(platformStyle->TextColorIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(platformStyle->TextColorIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(platformStyle->TextColorIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));

    signMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your AokChain addresses to prove you own them"));
    verifyMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/verify"), tr("&Verify message..."), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified AokChain addresses"));

    openRPCConsoleAction = new QAction(platformStyle->TextColorIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    // initially disable the debug window menu item
    openRPCConsoleAction->setEnabled(false);

    usedSendingAddressesAction = new QAction(platformStyle->TextColorIcon(":/icons/address-book"), tr("&Sending addresses..."), this);
    usedSendingAddressesAction->setStatusTip(tr("Show the list of used sending addresses and labels"));
    usedReceivingAddressesAction = new QAction(platformStyle->TextColorIcon(":/icons/address-book"), tr("&Receiving addresses..."), this);
    usedReceivingAddressesAction->setStatusTip(tr("Show the list of used receiving addresses and labels"));

    openAction = new QAction(platformStyle->TextColorIcon(":/icons/open"), tr("Open &URI..."), this);
    openAction->setStatusTip(tr("Open a aokchain: URI or payment request"));

    showHelpMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/info"), tr("&Command-line options"), this);
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    showHelpMessageAction->setStatusTip(tr("Show the %1 help message to get a list with possible AokChain command-line options").arg(tr(PACKAGE_NAME)));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(showHelpMessageAction, SIGNAL(triggered()), this, SLOT(showHelpMessageClicked()));
    connect(openRPCConsoleAction, SIGNAL(triggered()), this, SLOT(showDebugWindow()));
    // prevents an open debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));

#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        connect(encryptWalletAction, SIGNAL(triggered(bool)), walletFrame, SLOT(encryptWallet(bool)));
        connect(backupWalletAction, SIGNAL(triggered()), walletFrame, SLOT(backupWallet()));
        connect(changePassphraseAction, SIGNAL(triggered()), walletFrame, SLOT(changePassphrase()));
        connect(signMessageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
        connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
        connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
        connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
        connect(usedSendingAddressesAction, SIGNAL(triggered()), this, SLOT(gotoSendingAddressesPage()));
        connect(usedReceivingAddressesAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
        connect(openAction, SIGNAL(triggered()), this, SLOT(openClicked()));
    }
#endif // ENABLE_WALLET

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this, SLOT(showDebugWindowActivateConsole()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this, SLOT(showDebugWindow()));
}

void AokChainGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    if(walletFrame)
    {
        file->addAction(openAction);
        file->addAction(signMessageAction);
        file->addAction(verifyMessageAction);
        file->addSeparator();
        file->addAction(usedSendingAddressesAction);
        file->addAction(usedReceivingAddressesAction);
        file->addSeparator();
    }
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Wallet"));
    if(walletFrame)
    {
        settings->addAction(encryptWalletAction);
        settings->addAction(backupWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    if(walletFrame)
    {
        help->addAction(openRPCConsoleAction);
    }
    help->addAction(showHelpMessageAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);

#ifndef Q_OS_MAC
    // The wallet window has no menu bar: the side bar has the pages, and the settings page has
    // what else the menus offered. The actions stay on the window so that their shortcuts work.
    // (macOS keeps its menu bar at the top of the screen; a window without a wallet keeps it too.)
    if (walletFrame) {
        appMenuBar->hide();
        for (QMenu *menu : {file, settings, help})
            addActions(menu->actions());
    }
#endif
}

QList<QAction*> AokChainGUI::settingsActions(int group) const
{
    if (group == 0)
        return {encryptWalletAction, changePassphraseAction, backupWalletAction};
    return {openRPCConsoleAction, showHelpMessageAction, aboutAction, aboutQtAction, openAction, quitAction};
}

void AokChainGUI::createToolBars()
{
    bool displayAdvanced = gArgs.GetBoolArg("-advancedui", false);
    if(walletFrame)
    {
        // The pages are chosen in a side bar; it also shows what the status bar used to show
        sidebar = new Sidebar(this);
        sidebar->addPage(overviewAction, "overview");
        sidebar->addPage(sendCoinsAction, "send");
        sidebar->addPage(receiveCoinsAction, "receive");
        sidebar->addPage(historyAction, "history");
        sidebar->addPage(transferTokenAction, "tokens");
        sidebar->addPage(explorerAction, "explorer");
        // below a line: what used to be in the menus
        sidebar->addSeparator();
        if (displayAdvanced) {
            sidebar->addPage(createTokenAction, "create");
            sidebar->addPage(manageTokenAction, "manage");
        }
        sidebar->addPage(addressBookAction, "addressbook");
        sidebar->addPage(signPageAction, "sign");
        sidebar->addPage(settingsPageAction, "settings");

        // The actions carry the Alt+number shortcuts of the pages
        addAction(overviewAction);
        addAction(sendCoinsAction);
        addAction(receiveCoinsAction);
        addAction(historyAction);
        addAction(transferTokenAction);
        if (displayAdvanced) {
            addAction(createTokenAction);
            addAction(manageTokenAction);
        }
        addAction(explorerAction);
        addAction(addressBookAction);
        addAction(signPageAction);
        addAction(settingsPageAction);

        overviewAction->setChecked(true);

        connect(sidebar, SIGNAL(stakingToggled()), this, SLOT(toggleStakingActive()));
        connect(sidebar, SIGNAL(networkClicked()), this, SLOT(toggleNetworkActive()));
        connect(sidebar, SIGNAL(syncClicked()), this, SLOT(showModalOverlay()));

        // The former footer with the staking button is kept, hidden, for code that still updates it
        footerWidget->setObjectName("footerWidget");
        footerWidget->setParent(this);
        footerWidget->setVisible(false);
        stakingButton = new QPushButton(footerWidget);
        stakingButton->setText(tr("Start Staking"));

        QWidget* mainWalletWidget = new QWidget();
        mainWalletWidget->setObjectName("mainWalletWidget");
        mainWalletWidget->setAttribute(Qt::WA_StyledBackground, true);
        QHBoxLayout* layout = new QHBoxLayout(mainWalletWidget);
        layout->setSpacing(0);
        layout->setContentsMargins(QMargins());
        layout->addWidget(sidebar);
        layout->addWidget(walletFrame, 1);
        setCentralWidget(mainWalletWidget);
    }
}

void AokChainGUI::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
    if(_clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        createTrayIconMenu();

        // Keep up to date with client
        updateNetworkState();
        connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));
        connect(_clientModel, SIGNAL(networkActiveChanged(bool)), this, SLOT(setNetworkActive(bool)));

        modalOverlay->setKnownBestHeight(_clientModel->getHeaderTipHeight(), QDateTime::fromTime_t(_clientModel->getHeaderTipTime()));
        setNumBlocks(_clientModel->getNumBlocks(), _clientModel->getLastBlockDate(), _clientModel->getVerificationProgress(nullptr), false);
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(setNumBlocks(int,QDateTime,double,bool)));

        // Receive and report messages from client model
        connect(_clientModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        // Warnings of the node go to the bottom of the side bar
        connect(_clientModel, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(_clientModel->getStatusBarWarnings());

        // Show progress dialog
        connect(_clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        rpcConsole->setClientModel(_clientModel);
#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->setClientModel(_clientModel);
        }
#endif // ENABLE_WALLET
        
        OptionsModel* optionsModel = _clientModel->getOptionsModel();
        if(optionsModel)
        {
            // be aware of the tray icon disable state change reported by the OptionsModel object.
            connect(optionsModel,SIGNAL(hideTrayIconChanged(bool)),this,SLOT(setTrayIconVisible(bool)));
        
            // initialize the disable state of the tray icon with the current value in the model.
            setTrayIconVisible(optionsModel->getHideTrayIcon());
        }
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled(false);
        if(trayIconMenu)
        {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame)
        {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
    }
}

#ifdef ENABLE_WALLET
bool AokChainGUI::addWallet(const QString& name, WalletModel *walletModel)
{
    if(!walletFrame)
        return false;
    setWalletActionsEnabled(true);
    return walletFrame->addWallet(name, walletModel);
}

bool AokChainGUI::setCurrentWallet(const QString& name)
{
    if(!walletFrame)
        return false;
    return walletFrame->setCurrentWallet(name);
}

void AokChainGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void AokChainGUI::setWalletActionsEnabled(bool enabled)
{
    overviewAction->setEnabled(enabled);
    sendCoinsAction->setEnabled(enabled);
    sendCoinsMenuAction->setEnabled(enabled);
    receiveCoinsAction->setEnabled(enabled);
    receiveCoinsMenuAction->setEnabled(enabled);
    historyAction->setEnabled(enabled);
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);

    /** TOKENS START */
    transferTokenAction->setEnabled(false);
    createTokenAction->setEnabled(false);
    manageTokenAction->setEnabled(false);
    /** TOKENS END */
    explorerAction->setEnabled(enabled);
    addressBookAction->setEnabled(enabled);
    signPageAction->setEnabled(enabled);
    settingsPageAction->setEnabled(enabled);
}

void AokChainGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr("%1 client").arg(tr(PACKAGE_NAME)) + " " + networkStyle->getTitleAddText();
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->hide();
#endif

    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);
}

void AokChainGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-Mac OSes)
    if (!trayIcon)
        return;

    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow(static_cast<QMainWindow*>(this));
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsMenuAction);
    trayIconMenu->addAction(receiveCoinsMenuAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void AokChainGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

void AokChainGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;

#ifdef ENABLE_WALLET
    // with a wallet the options are a page of the window
    if (walletFrame) {
        showNormalIfMinimized();
        gotoSettingsPage();
        return;
    }
#endif

    OptionsDialog dlg(this, enableWallet);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void AokChainGUI::aboutClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, true);
    dlg.exec();
}

void AokChainGUI::showDebugWindow()
{
    rpcConsole->showNormal();
    rpcConsole->show();
    rpcConsole->raise();
    rpcConsole->activateWindow();
}

void AokChainGUI::showDebugWindowActivateConsole()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_CONSOLE);
    showDebugWindow();
}

void AokChainGUI::showHelpMessageClicked()
{
    helpMessageDialog->show();
}

#ifdef ENABLE_WALLET
void AokChainGUI::openClicked()
{
    OpenURIDialog dlg(this);
    if(dlg.exec())
    {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void AokChainGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    if (walletFrame) walletFrame->gotoOverviewPage();
}

void AokChainGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    if (walletFrame) walletFrame->gotoHistoryPage();
}

void AokChainGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoReceiveCoinsPage();
}

void AokChainGUI::gotoSendCoinsPage(QString addr)
{
    sendCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendCoinsPage(addr);
}

void AokChainGUI::gotoSignMessageTab(QString addr)
{
    signPageAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSignMessageTab(addr);
}

void AokChainGUI::gotoVerifyMessageTab(QString addr)
{
    signPageAction->setChecked(true);
    if (walletFrame) walletFrame->gotoVerifyMessageTab(addr);
}

void AokChainGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    if (walletFrame) walletFrame->usedReceivingAddresses();
}

void AokChainGUI::gotoSendingAddressesPage()
{
    addressBookAction->setChecked(true);
    if (walletFrame) walletFrame->usedSendingAddresses();
}

void AokChainGUI::gotoSettingsPage()
{
    settingsPageAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSettingsPage();
}

/** TOKENS START */
void AokChainGUI::gotoTokensPage()
{
    transferTokenAction->setChecked(true);
    if (walletFrame) walletFrame->gotoTokensPage();
};

void AokChainGUI::gotoCreateTokensPage()
{
    createTokenAction->setChecked(true);
    if (walletFrame) walletFrame->gotoCreateTokensPage();
};

void AokChainGUI::gotoManageTokensPage()
{
    manageTokenAction->setChecked(true);
    if (walletFrame) walletFrame->gotoManageTokensPage();
};

void AokChainGUI::gotoExplorerPage(QString query)
{
    explorerAction->setChecked(true);
    if (walletFrame) walletFrame->gotoExplorerPage(query);
}
/** TOKENS END */
#endif // ENABLE_WALLET

void AokChainGUI::updateNetworkState()
{
    int count = clientModel->getNumConnections();
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }

    QString tooltip;

    if (clientModel->getNetworkActive()) {
        tooltip = tr("%n active connection(s) to AokChain network", "", count) + QString(".<br>") + tr("Click to disable network activity.");
    } else {
        tooltip = tr("Network activity disabled.") + QString("<br>") + tr("Click to enable network activity again.");
        icon = ":/icons/network_disabled";
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");
    connectionsControl->setToolTip(tooltip);
    if (sidebar) {
        QString network = QString::fromStdString(Params().NetworkIDString());
        if (network == "main")
            network = tr("Mainnet");
        else if (network == "test")
            network = tr("Testnet");
        else if (network == "regtest")
            network = tr("Regtest");
        sidebar->setNetworkState(clientModel->getNetworkActive() ? tr("Peers %1 · %2").arg(count).arg(network)
                                                                 : tr("Network off · %1").arg(network), tooltip);
    }

    connectionsControl->setPixmap(platformStyle->SingleColorIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
}

void AokChainGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void AokChainGUI::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void AokChainGUI::updateAlerts(const QString &warnings)
{
    if (!sidebar)
        return;
    // the notice of a pre-release build is not an alarm; anything else is shown in a warning color
    const char *prerelease = "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications";
    const bool notice = warnings == QCoreApplication::translate("aokchain-core", prerelease) || warnings == QString(prerelease);
    sidebar->setNotice(notice ? tr("Pre-release test build. Use at your own risk.") : warnings, warnings, !notice);
}

void AokChainGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft = (GetTime() - headersTipTime) / Params().GetConsensus().nTargetSpacing;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC)
    {
        progressBarLabel->setText(tr("Syncing Headers (%1%)...").arg(QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1)));
        if (sidebar)
            setSideSync(tr("Syncing headers"), QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1) + "%",
                                  1.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, true, progressBarLabel->text());
    }
}

void AokChainGUI::setSideSync(const QString& status, const QString& remaining, double progress, bool showProgress, const QString& toolTip)
{
    if (sidebar && !bootstrapActive)
        sidebar->setSyncState(status, remaining, progress, showProgress, toolTip);
}

static QString megabytes(quint64 bytes)
{
    return bytes >= 1000000000ULL ? QString("%1 GB").arg(bytes / 1e9, 0, 'f', 1) : QString("%1 MB").arg(bytes / 1000000);
}

void AokChainGUI::updateBootstrap()
{
    // A wallet that started from a chain state snapshot and needs the past blocks (it was
    // restored from a file, say): ask once whether to download them.
    if (!pastBlocksAsked && walletFrame && clientModel && WalletRescanPending() && PastBlocks::missing() && isVisible() && !QApplication::activeModalWidget()) {
        pastBlocksAsked = true;
        PastBlocks::offer(this, tr("Showing the earlier transactions of this wallet"));
    }
    {
        const bool fetching = GetPastBlocksInfo().fetching;
        if (pastBlocksWereFetching && !fetching) {
            const PastBlocksInfo past = GetPastBlocksInfo();
            if (!past.missing)
                message(tr("Past blocks"), tr("The past blocks are downloaded."), CClientUIInterface::MSG_INFORMATION | CClientUIInterface::MODAL);
            else if (!past.error.empty() && past.error != "cancelled")
                message(tr("Past blocks"), tr("The download of the past blocks stopped: %1").arg(QString::fromStdString(past.error)), CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
        }
        pastBlocksWereFetching = fetching;
    }
    const BootstrapInfo info = GetBootstrapInfo();
    if (!info.configured && bootstrapLastState.isEmpty())
        return; // nothing is shown when the bootstrap is not set up
    const QString state = QString::fromStdString(info.state);
    const bool active = info.active;
    const QString explanation = tr("The blocks are fetched from a server as archives and then validated one by one, exactly like blocks from other nodes. The server does not have to be trusted.");
    if (active) {
        QString status, right, line;
        double progress = 0;
        const int height = clientModel ? clientModel->getNumBlocks() : info.startHeight;
        if (info.chunks == 0) {
            status = tr("Fast sync: getting ready");
            line = status;
        } else if (info.chunksDownloaded < info.chunks && info.chunksDownloaded - info.chunksImported < 2) {
            // (with two archives waiting for their import the download pauses: the validation is shown then)
            status = tr("Fast sync: archive %1/%2").arg(info.chunksDownloaded + 1).arg(info.chunks);
            right = QString("%1 / %2").arg(megabytes(info.bytesDownloaded), megabytes(info.bytesTotal));
            line = tr("Fast sync: fetching block archive %1/%2 · %3 / %4").arg(info.chunksDownloaded + 1).arg(info.chunks)
                       .arg(megabytes(info.bytesDownloaded), megabytes(info.bytesTotal));
            progress = info.bytesTotal ? double(info.bytesDownloaded) / info.bytesTotal : 0;
            const QString left = bootstrapTimeLeft(false, info.bytesDownloaded, info.bytesTotal);
            if (!left.isEmpty())
                line += " · " + left;
        } else {
            status = tr("Validating blocks");
            right = QString("%1 / %2").arg(GUIUtil::withThousands(QString::number(height)), GUIUtil::withThousands(QString::number(info.targetHeight)));
            line = tr("Validating blocks %1 / %2").arg(GUIUtil::withThousands(QString::number(height)), GUIUtil::withThousands(QString::number(info.targetHeight)));
            progress = info.targetHeight ? double(height) / info.targetHeight : 0;
            const QString left = bootstrapTimeLeft(true, height, info.targetHeight);
            if (!left.isEmpty()) {
                line += " · " + left;
                status += " · " + left;
            }
        }
        const QString detail = info.chunks == 0 ? explanation
            : explanation + "\n" + tr("Fetched %1 of %2 archives (%3 / %4), validated up to block %5 of %6.")
                  .arg(info.chunksDownloaded).arg(info.chunks).arg(megabytes(info.bytesDownloaded), megabytes(info.bytesTotal))
                  .arg(GUIUtil::withThousands(QString::number(height)), GUIUtil::withThousands(QString::number(info.targetHeight)));
        if (sidebar)
            sidebar->setSyncState(status, right, progress, true, line + "\n" + explanation);
        if (modalOverlay)
            modalOverlay->setBootstrapState(true, line, detail);
    } else if (state != bootstrapLastState || bootstrapActive) {
        // over: say how it ended, and let the ordinary sync line come back
        QString status, detail;
        if (state == "failed") {
            status = tr("Fast sync was not possible: the blocks come from other nodes");
            detail = QString::fromStdString(info.message);
        } else if (state == "skipped") {
            status = tr("Fast sync skipped: the blocks come from other nodes");
        } else if (state == "done") {
            status = tr("Fast sync finished: the remaining blocks come from other nodes");
        }
        if (modalOverlay)
            modalOverlay->setBootstrapState(false, status, detail);
        bootstrapActive = false;
        if (clientModel)
            setNumBlocks(clientModel->getNumBlocks(), clientModel->getLastBlockDate(), clientModel->getVerificationProgress(nullptr), false);
    }
    bootstrapActive = active;
    bootstrapLastState = state;
}

QString AokChainGUI::bootstrapTimeLeft(bool blocks, double value, double end)
{
    // the rate of the last minutes: early blocks validate much faster than recent ones
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (blocks != bootstrapSamplesAreBlocks) {
        bootstrapSamples.clear();
        bootstrapSamplesAreBlocks = blocks;
    }
    if (bootstrapSamples.isEmpty() || now - bootstrapSamples.last().first >= 5000)
        bootstrapSamples.append(qMakePair(now, value));
    while (bootstrapSamples.size() > 2 && now - bootstrapSamples.first().first > 180000)
        bootstrapSamples.removeFirst();
    const qint64 span = now - bootstrapSamples.first().first;
    const double advanced = value - bootstrapSamples.first().second;
    if (span < 20000 || advanced <= 0 || value >= end)
        return QString();
    return PastBlocks::timeLeft(qint64(span / 1000.0 * (end - value) / advanced));
}

void AokChainGUI::skipBootstrap()
{
    SkipBootstrap();
    updateBootstrap();
}

void AokChainGUI::chooseBootstrapFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder with block archives (manifest.json and the blocks-... files)"));
    if (folder.isEmpty())
        return;
    std::string error;
    if (!StartBootstrapFromFolder(folder.toStdString(), error)) {
        QMessageBox::warning(this, tr("Block archives"), error == "a bootstrap is running already"
            ? tr("A fast sync is running. Skip it first, then choose the folder.")
            : tr("This folder has no manifest.json. Choose the folder that holds manifest.json and the blocks-... files."));
        return;
    }
    updateBootstrap();
}

void AokChainGUI::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
    if (modalOverlay)
    {
        if (header)
            modalOverlay->setKnownBestHeight(count, blockDate);
        else
            modalOverlay->tipUpdate(count, blockDate, nVerificationProgress);
    }

    if (!clientModel)
        return;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    QString sideStatus = tr("Syncing");
    switch (blockSource) {
        case BLOCK_SOURCE_NETWORK:
            if (header) {
                updateHeadersSyncProgressLabel();
                return;
            }
            progressBarLabel->setText(tr("Synchronizing with network..."));
            updateHeadersSyncProgressLabel();
            break;
        case BLOCK_SOURCE_DISK:
            if (header) {
                progressBarLabel->setText(tr("Indexing blocks on disk..."));
            } else {
                progressBarLabel->setText(tr("Processing blocks on disk..."));
            }
            sideStatus = tr("Importing blocks");
            break;
        case BLOCK_SOURCE_REINDEX:
            progressBarLabel->setText(tr("Reindexing blocks on disk..."));
            sideStatus = tr("Reindexing");
            break;
        case BLOCK_SOURCE_NONE:
            if (header) {
                return;
            }
            progressBarLabel->setText(tr("Connecting to peers..."));
            sideStatus = tr("Connecting");
            break;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr("Processed %n block(s) of transaction history.", "", count);

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(false);
            modalOverlay->showHide(true, true);
        }
#endif // ENABLE_WALLET

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
        fSyncing = false;
        if (sidebar)
            setSideSync(tr("Synced"), tr("Block %1").arg(count), 1.0, false, QString("<nobr>") + tooltip + QString("</nobr>"));
    }
    else
    {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);
        fSyncing = true;

        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nVerificationProgress * 1000000000.0 + 0.5);
        progressBar->setVisible(true);

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
            labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(QString(
                ":/movies/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')))
                .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(true);
            modalOverlay->showHide();
        }
#endif // ENABLE_WALLET

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
        if (sidebar) {
            // the side bar is narrow: years only when the chain is more than a year behind
            const qint64 year = 31556952;
            const QString sideBehind = secs >= year ? QObject::tr("%n year(s)", "", secs / year) : timeBehindText;
            setSideSync(sideStatus, tr("%1 left").arg(sideBehind), nVerificationProgress, true, QString("<nobr>") + tooltip + QString("</nobr>"));
        }
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void AokChainGUI::message(const QString &title, const QString &message, unsigned int style, bool *ret)
{
    QString strTitle = tr("AokChain"); // default title
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    }
    else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            break;
        default:
            break;
        }
    }
    // Append title to "AokChain - "
    if (!msgType.isEmpty())
        strTitle += " - " + msgType;

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        QMessageBox mBox(static_cast<QMessageBox::Icon>(nMBoxIcon), strTitle, message, buttons, this);
        int r = mBox.exec();
        if (ret != nullptr)
            *ret = r == QMessageBox::Ok;
    }
    else
        notificator->notify(static_cast<Notificator::Class>(nNotifyIcon), strTitle, message);
}

void AokChainGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel() && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void AokChainGUI::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel && clientModel->getOptionsModel())
    {
        if(!clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            QApplication::quit();
        }
        else
        {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

void AokChainGUI::showEvent(QShowEvent *event)
{
    // enable the debug window when the main window shows up
    openRPCConsoleAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);
}

#ifdef ENABLE_WALLET
void AokChainGUI::incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label, const QString& tokenName)
{
    // On new transaction, make an info balloon
    QString msg = tr("Date: %1\n").arg(date);
    if (tokenName == "AOK")
        msg += tr("Amount: %1\n").arg(AokChainUnits::formatWithUnit(unit, amount, true));
    else
        msg += tr("Amount: %1\n").arg(AokChainUnits::formatWithCustomName(tokenName, amount, MAX_TOKEN_UNITS, true));

    msg += tr("Type: %1\n").arg(type);
    
    if (!label.isEmpty())
        msg += tr("Label: %1\n").arg(label);
    else if (!address.isEmpty())
        msg += tr("Address: %1\n").arg(address);
    message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
             msg, CClientUIInterface::MSG_INFORMATION);
}

void AokChainGUI::checkTokens()
{
    // Check that status of RIP2 and activate the tokens icon if it is active
    if(AreTokensDeployed()) {
        transferTokenAction->setDisabled(false);
        transferTokenAction->setToolTip(tr("Transfer tokens to AOK addresses"));
        createTokenAction->setDisabled(false);
        createTokenAction->setToolTip(tr("Create new main/sub/unique tokens"));
        manageTokenAction->setDisabled(false);
    } else {
        transferTokenAction->setDisabled(true);
        transferTokenAction->setToolTip(tr("Tokens not yet active"));
        createTokenAction->setDisabled(true);
        createTokenAction->setToolTip(tr("Tokens not yet active"));
        manageTokenAction->setDisabled(true);
    }
}
#endif // ENABLE_WALLET

void AokChainGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void AokChainGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        for (const QUrl &uri : event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool AokChainGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool AokChainGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void AokChainGUI::setHDStatus(int hdEnabled)
{
    labelWalletHDStatusIcon->setPixmap(platformStyle->SingleColorIcon(hdEnabled ? ":/icons/hd_enabled" : ":/icons/hd_disabled").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelWalletHDStatusIcon->setToolTip(hdEnabled ? tr("HD key generation is <b>enabled</b>") : tr("HD key generation is <b>disabled</b>"));

    // eventually disable the QLabel to set its opacity to 50% 
    labelWalletHDStatusIcon->setEnabled(hdEnabled);
}

void AokChainGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        if (sidebar)
            sidebar->setWalletState(QString(), QString());
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        if (sidebar)
            sidebar->setWalletState(tr("Wallet unlocked"), labelWalletEncryptionIcon->toolTip());
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::UnlockedForStaking:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked for staking only</b>"));
        if (sidebar)
            sidebar->setWalletState(tr("Unlocked for staking only"), labelWalletEncryptionIcon->toolTip());
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false);
        break;
    case WalletModel::Locked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        if (sidebar)
            sidebar->setWalletState(tr("Wallet locked"), labelWalletEncryptionIcon->toolTip());
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}
#endif // ENABLE_WALLET

void AokChainGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if(!clientModel)
        return;

    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void AokChainGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void AokChainGUI::updateStakingIcon()
{
    if (walletFrame) {
        nWeight = walletFrame->updateWeight();
    }
    // The wallet was locked from elsewhere (walletlock, or the time of walletpassphrase ran out):
    // nothing is staked any more, and the switch says so.
    if (this->nStaking && walletFrame && !walletFrame->isWalletUnlocked()) {
        this->nStaking = false;
        for (CWallet* pwallet : vpwallets)
            pwallet->StopStake();
    }

    QString detail;
    if (this->nStaking && nWeight) {
        uint64_t nWeight = this->nWeight;
        uint64_t nNetworkWeight = 1.1429 * GetPoSKernelPS();
        unsigned nEstimateTime = 1.0455 * 64 * nNetworkWeight / nWeight;

        QString text;
        if (nEstimateTime < 60) {
            text = tr("%n second(s)", "", nEstimateTime);
        } else if (nEstimateTime < 60 * 60) {
            text = tr("%n minute(s)", "", nEstimateTime / 60);
        } else if (nEstimateTime < 24 * 60 * 60) {
            text = tr("%n hour(s)", "", nEstimateTime / (60 * 60));
        } else {
            text = tr("%n day(s)", "", nEstimateTime / (60 * 60 * 24));
        }

        nWeight /= COIN;
        nNetworkWeight /= COIN;

        labelStakingIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
        if (stakingButton)
            stakingButton->setText(tr("Stop Staking"));
        detail = tr("On · a reward about every %1").arg(text);
    } else {
        labelStakingIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        const QString state = this->nStaking ? tr("On") : tr("Off");
        if (IsInitialBlockDownload()) {
            labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
            detail = this->nStaking ? tr("On · waiting for the synchronization to finish") : tr("Off · staking is possible once synchronized");
        } else if (walletFrame && !walletFrame->isWalletUnlocked()) {
            labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
            detail = tr("%1 · the wallet is locked").arg(state);
        } else if (!nWeight) {
            labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
            detail = tr("%1 · no mature coins").arg(state);
        } else {
            labelStakingIcon->setToolTip(tr("Not staking"));
            detail = state;
        }
        if (stakingButton)
            stakingButton->setText(tr("Start Staking"));
    }
    if (sidebar)
        sidebar->setStaking(this->nStaking, detail, labelStakingIcon->toolTip());
}

void AokChainGUI::detectShutdown()
{
    if (ShutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void AokChainGUI::showProgress(const QString &title, int nProgress)
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

void AokChainGUI::setTrayIconVisible(bool fHideTrayIcon)
{
    if (trayIcon)
    {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}

void AokChainGUI::showModalOverlay()
{
    if (modalOverlay && (fSyncing || progressBar->isVisible() || modalOverlay->isLayerVisible()))
        modalOverlay->toggleVisibility();
}

static bool ThreadSafeMessageBox(AokChainGUI *gui, const std::string& message, const std::string& caption, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button
    QMetaObject::invokeMethod(gui, "message",
                               modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                               Q_ARG(QString, QString::fromStdString(caption)),
                               Q_ARG(QString, QString::fromStdString(message)),
                               Q_ARG(unsigned int, style),
                               Q_ARG(bool*, &ret));
    return ret;
}

void AokChainGUI::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
    uiInterface.ThreadSafeQuestion.connect(boost::bind(ThreadSafeMessageBox, this, boost::placeholders::_1, boost::placeholders::_3, boost::placeholders::_4));
}

void AokChainGUI::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ThreadSafeMessageBox.disconnect(boost::bind(ThreadSafeMessageBox, this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
    uiInterface.ThreadSafeQuestion.disconnect(boost::bind(ThreadSafeMessageBox, this, boost::placeholders::_1, boost::placeholders::_3, boost::placeholders::_4));
}

void AokChainGUI::toggleNetworkActive()
{
    if (clientModel) {
        clientModel->setNetworkActive(!clientModel->getNetworkActive());
    }
}

void AokChainGUI::toggleStakingActive()
{
    if (!walletFrame)
        return;
    this->nStaking = !this->nStaking;

    if (this->nStaking) {
        walletFrame->unlockWalletForStaking();
        if (walletFrame->isWalletUnlocked()) {
            for (CWallet* pwallet : vpwallets) {
                pwallet->StartStake();
            }
        } else {
            // the passphrase dialog was cancelled: staking was not started
            this->nStaking = false;
        }
        updateStakingIcon();
    } else {
        for (CWallet* pwallet : vpwallets) {
            pwallet->StopStake();
        }
        walletFrame->lockWalletAfterStaking();
        updateStakingIcon();
    }
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(const PlatformStyle *platformStyle) :
    optionsModel(0),
    menu(0)
{
    createContextMenu(platformStyle);
    setToolTip(tr("Unit to show amounts in. Click to select another unit."));
    QList<AokChainUnits::Unit> units = AokChainUnits::availableUnits();
    int max_width = 0;
    const QFontMetrics fm(font());
    for (const AokChainUnits::Unit unit : units)
    {
        max_width = qMax(max_width, fm.width(AokChainUnits::name(unit)));
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setStyleSheet(QString("QLabel { color : %1; }").arg(platformStyle->DarkOrangeColor().name()));
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event)
{
    onDisplayUnitsClicked(event->pos());
}

/** Creates context menu, its actions, and wires up all the relevant signals for mouse events. */
void UnitDisplayStatusBarControl::createContextMenu(const PlatformStyle *platformStyle)
{
    menu = new QMenu(this);
    for (AokChainUnits::Unit u : AokChainUnits::availableUnits())
    {
        QAction *menuAction = new QAction(QString(AokChainUnits::name(u)), this);
        menuAction->setData(QVariant(u));
        menu->addAction(menuAction);
    }
    menu->setStyleSheet(QString("QMenu::item{ color: %1; } QMenu::item:selected{ color: %2;}").arg(platformStyle->DarkOrangeColor().name(), platformStyle->TextColor().name()));
    connect(menu,SIGNAL(triggered(QAction*)),this,SLOT(onMenuSelection(QAction*)));
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel(OptionsModel *_optionsModel)
{
    if (_optionsModel)
    {
        this->optionsModel = _optionsModel;

        // be aware of a display unit change reported by the OptionsModel object.
        connect(_optionsModel,SIGNAL(displayUnitChanged(int)),this,SLOT(updateDisplayUnit(int)));

        // initialize the display units label with the current value in the model.
        updateDisplayUnit(_optionsModel->getDisplayUnit());
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits)
{
    setText(AokChainUnits::name(newUnits));
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint& point)
{
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit. */
void UnitDisplayStatusBarControl::onMenuSelection(QAction* action)
{
    if (action)
    {
        optionsModel->setDisplayUnit(action->data());
    }
}
