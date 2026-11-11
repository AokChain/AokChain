// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "firstrun.h"
#include "guiutil.h"
#include "config/aokchain-config.h"
#endif

#include "splashscreen.h"

#include "networkstyle.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "version.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>

SplashScreen::SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0)
{
    const GUIUtil::Theme &t = GUIUtil::theme();
    float devicePixelRatio      = 1.0;
#if QT_VERSION > 0x050100
    devicePixelRatio = static_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio();
#endif

    // define text to place
    QString titleText       = tr(PACKAGE_NAME);
    QString versionText     = QString::fromStdString(FormatFullVersion());
    QString copyrightText   = QString::fromUtf8(CopyrightHolders(strprintf("\xc2\xA9 %u ", COPYRIGHT_YEAR)).c_str());
    QString titleAddText    = networkStyle->getTitleAddText();

    // create a bitmap according to device pixelratio
    const int width = 480, height = 320;
    QSize splashSize(width*devicePixelRatio,height*devicePixelRatio);
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    // The look of the first-run screens: the surface of a card, the logo, the name, the version.
    QPainter pixPaint(&pixmap);
    pixPaint.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform, true);
    pixPaint.fillRect(QRect(0, 0, width, height), t.surface);
    pixPaint.fillRect(QRect(0, 0, width, 6), t.fill);

    const int logoSize = 76;
    pixPaint.drawPixmap(QPointF((width - logoSize) / 2.0, 56), WizardHeader::logo(logoSize, devicePixelRatio));

    pixPaint.setPen(t.text);
    pixPaint.setFont(GUIUtil::appFont(24, QFont::Bold));
    pixPaint.drawText(QRect(0, 146, width, 36), Qt::AlignHCenter | Qt::AlignVCenter, titleText);

    pixPaint.setPen(t.textSecondary);
    pixPaint.setFont(GUIUtil::monoFont(12));
    pixPaint.drawText(QRect(0, 184, width, 20), Qt::AlignHCenter | Qt::AlignVCenter, versionText);

    pixPaint.setPen(t.textDisabled);
    pixPaint.setFont(GUIUtil::appFont(10));
    pixPaint.drawText(QRect(40, 208, width - 80, 30), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, copyrightText);

    // a chip for a network other than the main one
    if(!titleAddText.isEmpty()) {
        pixPaint.setFont(GUIUtil::appFont(11, QFont::DemiBold));
        const int chipWidth = pixPaint.fontMetrics().boundingRect(titleAddText).width() + 20;
        const QRectF chip(width - chipWidth - 14, 18, chipWidth, 22);
        pixPaint.setPen(Qt::NoPen);
        pixPaint.setBrush(t.warnBg);
        pixPaint.drawRoundedRect(chip, 11, 11);
        pixPaint.setPen(t.warnText);
        pixPaint.drawText(chip, Qt::AlignCenter, titleAddText);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if(keyEvent->text()[0] == 'q') {
            StartShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? _("(press q to shutdown and continue later)")
                                : _("press q to shutdown")) +
            strprintf("\n%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2, false));
    connectedWallets.push_back(wallet);
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, boost::placeholders::_1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(&SplashScreen::ConnectWallet, this, boost::placeholders::_1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, boost::placeholders::_1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
#ifdef ENABLE_WALLET
    for (CWallet* const & pwallet : connectedWallets) {
        pwallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2, false));
    }
#endif
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    // what the node is doing, below a thin line
    const GUIUtil::Theme &t = GUIUtil::theme();
    painter.fillRect(QRect(40, height() - 70, width() - 80, 1), t.line);
    painter.setPen(t.textSecondary);
    painter.setFont(GUIUtil::appFont(12));
    painter.drawText(QRect(24, height() - 62, width() - 48, 52), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
