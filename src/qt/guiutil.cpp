// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "guiutil.h"

#include "aokchainaddressvalidator.h"
#include "aokchainunits.h"
#include "qvalidatedlineedit.h"
#include "walletmodel.h"

#include "fs.h"
#include "primitives/transaction.h"
#include "init.h"
#include "policy/policy.h"
#include "protocol.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include "utiltime.h"

#ifdef WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501
#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "shellapi.h"
#include "shlobj.h"
#include "shlwapi.h"
#endif

#include <boost/scoped_array.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QLocale>
#include <QDir>
#include <QCoreApplication>
#include <QComboBox>
#include <QEvent>
#include <QLayout>
#include <QDialog>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QSettings>
#include <QTextDocument> // for Qt::mightBeRichText
#include <QThread>
#include <QMouseEvent>
#include <QPainter>

#if QT_VERSION < 0x050000
#include <QUrl>
#else
#include <QUrlQuery>
#endif

#if QT_VERSION >= 0x50200
#include <QFontDatabase>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QDebug>
#include <QFile>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#endif

#ifdef WIN32
static fs::detail::utf8_codecvt_facet utf8;
#endif

#if defined(Q_OS_MAC)
extern double NSAppKitVersionNumber;
#if !defined(NSAppKitVersionNumber10_8)
#define NSAppKitVersionNumber10_8 1187
#endif
#if !defined(NSAppKitVersionNumber10_9)
#define NSAppKitVersionNumber10_9 1265
#endif
#endif

#include "guiconstants.h"
#include "platformstyle.h"

namespace GUIUtil {

const Theme& theme()
{
    static const Theme light = [] {
        Theme t;
        t.bg = QColor("#F3F3F1");
        t.surface = QColor("#FFFFFF");
        t.surfaceAlt = QColor("#F7F7F5");
        t.border = QColor("#DCDCD8");
        t.borderStrong = QColor("#B9B9B4");
        t.line = QColor("#E8E8E4");
        t.text = QColor("#171717");
        t.textSecondary = QColor("#5F5F5B");
        t.label = QColor("#3B3B38");
        t.textDisabled = QColor("#9B9B96");
        t.fill = QColor("#054848");
        t.fillHover = QColor("#0A5A58");
        t.fillPressed = QColor("#033837");
        t.onFill = QColor("#FFFFFF");
        t.accent = QColor("#066A63");
        t.accentHover = QColor("#0A5A58");
        t.accentPressed = QColor("#033837");
        t.accentSoft = QColor("#E4EEEC");
        t.onAccent = QColor("#FFFFFF");
        t.positive = QColor("#06705F");
        t.positiveSoft = QColor("#E4EEEC");
        t.negative = QColor("#A84A0B");
        t.negativeSoft = QColor("#FBEBDD");
        t.ok = QColor("#0B7A4B");
        t.okSoft = QColor("#DDF3E7");
        t.error = QColor("#B3261E");
        t.errorSoft = QColor("#FDECEC");
        t.warnBg = QColor("#FFF4DD");
        t.warnText = QColor("#7A4A00");
        t.warnBorder = QColor("#F0D58A");
        t.chip = QColor("#EBEBE7");
        t.chipText = QColor("#4B4B47");
        t.sideBg = QColor("#054848");
        t.sideText = QColor("#C2DAD6");
        t.sideStrong = QColor("#FFFFFF");
        t.sideOnBg = QColor("#033A3A");
        t.sideCard = QColor("#1A5C5A");
        t.sideTrack = QColor("#0B6460");
        t.sideBar = QColor("#7EE0CF");
        t.sideOff = QColor("#2F7C77");
        return t;
    }();
    return light;
}

void loadAppFonts()
{
    for (const char* name : {"NotoSans-Regular", "NotoSans-Medium", "NotoSans-SemiBold", "NotoSans-Bold",
                             "NotoSansKR-Regular", "NotoSansKR-Bold",
                             "NotoSansMono-Regular", "NotoSansMono-Medium", "NotoSansMono-SemiBold", "NotoSansMono-Bold"}) {
        if (QFontDatabase::addApplicationFont(QString(":/fonts/%1").arg(name)) < 0)
            qWarning() << "Could not load the font" << name;
    }
}

QFont appFont(int pixelSize, int weight)
{
    QFont font;
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    // Latin from Noto Sans, Hangul and CJK from Noto Sans KR
    font.setFamilies({"Noto Sans", "Noto Sans KR"});
#else
    font.setFamily("Noto Sans");
#endif
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont monoFont(int pixelSize, int weight)
{
    QFont font;
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    font.setFamilies({"Noto Sans Mono", "Noto Sans KR"});
#else
    font.setFamily("Noto Sans Mono");
#endif
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

void fitDialogHeight(QWidget* dialog);

/**
 * Gives the stock dialogs (pass phrase, edit address, transaction details, coin control, open URI
 * and the like) the margins of the theme when they are first shown. Colors, buttons and inputs
 * come from the style sheet.
 */
class DialogPolisher : public QObject
{
public:
    explicit DialogPolisher(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Show && object->isWidgetType()) {
            QDialog* dialog = qobject_cast<QDialog*>(object);
            if (dialog && dialog->isWindow() && !dialog->property("themePolished").toBool()
                && !dialog->inherits("QMessageBox") && !dialog->inherits("QFileDialog") && !dialog->inherits("QProgressDialog")) {
                dialog->setProperty("themePolished", true);
                if (QLayout* layout = dialog->layout()) {
                    const QMargins m = layout->contentsMargins();
                    layout->setContentsMargins(qMax(m.left(), 22), qMax(m.top(), 20), qMax(m.right(), 22), qMax(m.bottom(), 18));
                    if (layout->spacing() >= 0 && layout->spacing() < 10)
                        layout->setSpacing(10);
                    // Texts that wrap need more height than the layout asks for by itself:
                    // without this the rows of a dialog with long notes are squeezed together.
                    fitDialogHeight(dialog);
                }
            }
        }
        return QObject::eventFilter(object, event);
    }
};

/** The icon of a message box: a soft disc with the sign in the color of its meaning. */
static QPixmap messageBadge(QStyle::StandardPixmap which, int size)
{
    const Theme& t = theme();
    QColor fill, ink;
    QString sign;
    switch (which) {
    case QStyle::SP_MessageBoxInformation: fill = t.accentSoft; ink = t.accent; sign = "i"; break;
    case QStyle::SP_MessageBoxQuestion: fill = t.accentSoft; ink = t.accent; sign = "?"; break;
    case QStyle::SP_MessageBoxWarning: fill = t.warnBg; ink = t.warnText; sign = "!"; break;
    case QStyle::SP_MessageBoxCritical: fill = t.errorSoft; ink = t.error; sign = "!"; break;
    default: return QPixmap();
    }
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(QRectF(0, 0, size, size));
    p.setPen(ink);
    p.setFont(appFont(size * 55 / 100, QFont::Bold));
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, sign);
    return pixmap;
}

/** Fusion, with the icons of the message boxes drawn in the theme. */
class ThemeStyle : public QProxyStyle
{
public:
    explicit ThemeStyle(QStyle* base) : QProxyStyle(base) {}

    QIcon standardIcon(StandardPixmap which, const QStyleOption* option, const QWidget* widget) const override
    {
        const QPixmap badge = messageBadge(which, 40);
        return badge.isNull() ? QProxyStyle::standardIcon(which, option, widget) : QIcon(badge);
    }

    QPixmap standardPixmap(StandardPixmap which, const QStyleOption* option, const QWidget* widget) const override
    {
        const QPixmap badge = messageBadge(which, 40);
        return badge.isNull() ? QProxyStyle::standardPixmap(which, option, widget) : badge;
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        if (metric == PM_MessageBoxIconSize)
            return 40;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

void fitDialogHeight(QWidget* dialog)
{
    QLayout* layout = dialog ? dialog->layout() : nullptr;
    if (!layout)
        return;
    const int needed = layout->totalHeightForWidth(dialog->width());
    if (needed > dialog->height()) {
        if (dialog->minimumHeight() < needed && dialog->maximumHeight() >= needed)
            dialog->setMinimumHeight(needed);
        dialog->resize(dialog->width(), needed);
    }
}

void applyTheme(QApplication& app)
{
    const Theme& t = theme();
    app.installEventFilter(new DialogPolisher(&app));

    // The same widget style on every platform, colored by the palette
    app.setStyle(new ThemeStyle(QStyleFactory::create("Fusion")));
    QFont::insertSubstitution("Noto Sans", "Noto Sans KR");
    QFont::insertSubstitution("Noto Sans Mono", "Noto Sans KR");
    app.setFont(appFont());
    // Windows takes the fonts of these from the system settings unless told otherwise
    for (const char* cls : {"QMenuBar", "QMenu", "QToolTip", "QTipLabel", "QMessageBox", "QStatusBar",
                            "QHeaderView", "QAbstractItemView", "QToolButton", "QDockWidgetTitle"})
        app.setFont(appFont(), cls);

    QPalette p;
    p.setColor(QPalette::Window, t.bg);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.surface);
    p.setColor(QPalette::AlternateBase, t.surfaceAlt);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.surface);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, t.error);
    p.setColor(QPalette::ToolTipBase, t.text);
    p.setColor(QPalette::ToolTipText, t.surface);
    p.setColor(QPalette::Highlight, t.fill);
    p.setColor(QPalette::HighlightedText, t.onFill);
    p.setColor(QPalette::Link, t.accent);
    p.setColor(QPalette::LinkVisited, t.accentPressed);
    p.setColor(QPalette::Light, t.surface);
    p.setColor(QPalette::Midlight, t.surfaceAlt);
    p.setColor(QPalette::Mid, t.border);
    p.setColor(QPalette::Dark, t.borderStrong);
    p.setColor(QPalette::Shadow, t.borderStrong);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    p.setColor(QPalette::PlaceholderText, t.textDisabled);
#endif
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.textDisabled);
    app.setPalette(p);

    QFile file(":/css/theme");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not load the theme style sheet";
        return;
    }
    QString qss = QString::fromUtf8(file.readAll());
    // longer names first, so that @accent does not eat @accentHover
    const std::pair<const char*, QColor> tokens[] = {
        {"@accentPressed", t.accentPressed}, {"@accentHover", t.accentHover}, {"@accentSoft", t.accentSoft},
        {"@borderStrong", t.borderStrong}, {"@textSecondary", t.textSecondary}, {"@textDisabled", t.textDisabled},
        {"@surfaceAlt", t.surfaceAlt}, {"@warnBorder", t.warnBorder}, {"@warnText", t.warnText}, {"@warnBg", t.warnBg},
        {"@fillPressed", t.fillPressed}, {"@fillHover", t.fillHover}, {"@onFill", t.onFill}, {"@fill", t.fill},
        {"@positiveSoft", t.positiveSoft}, {"@negativeSoft", t.negativeSoft}, {"@errorSoft", t.errorSoft}, {"@error", t.error},
        {"@okSoft", t.okSoft}, {"@ok", t.ok}, {"@chipText", t.chipText}, {"@chip", t.chip},
        {"@sideStrong", t.sideStrong}, {"@sideTrack", t.sideTrack}, {"@sideText", t.sideText}, {"@sideOnBg", t.sideOnBg},
        {"@sideCard", t.sideCard}, {"@sideBar", t.sideBar}, {"@sideOff", t.sideOff}, {"@sideBg", t.sideBg},
        {"@onAccent", t.onAccent}, {"@positive", t.positive}, {"@negative", t.negative}, {"@surface", t.surface},
        {"@accent", t.accent}, {"@border", t.border}, {"@label", t.label}, {"@line", t.line}, {"@text", t.text}, {"@bg", t.bg},
    };
    for (const std::pair<const char*, QColor>& token : tokens)
        qss.replace(token.first, token.second.name());
    app.setStyleSheet(qss);
}

QString shortAddress(const QString& address)
{
    if (address.size() <= 9 || !IsValidDestinationString(address.toStdString()))
        return address;
    return address.left(4) + QChar(0x2026) + address.right(4);
}

QString shortHash(const QString& hash, int head, int tail)
{
    if (hash.size() <= head + tail + 1)
        return hash;
    return hash.left(head) + QChar(0x2026) + hash.right(tail);
}

QString withThousands(const QString& number)
{
    QString s = number;
    // the unit formatter separates thousands with thin spaces
    s.remove(QChar(0x2009));
    int start = 0;
    while (start < s.size() && !s[start].isDigit())
        ++start;
    int end = start;
    while (end < s.size() && s[end].isDigit())
        ++end;
    for (int i = end - 3; i > start; i -= 3)
        s.insert(i, ',');
    return s;
}

QPixmap lineIcon(const QString& name, int size, const QColor& color, qreal strokeWidth)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    // the icons are defined on a 24 unit grid
    p.scale(size / 24.0, size / 24.0);
    QPen pen(color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
    auto poly = [&p](std::initializer_list<QPointF> points) { p.drawPolyline(QPolygonF(QVector<QPointF>(points))); };
    if (name == "overview") {
        p.drawRect(QRectF(3, 3, 7, 9)); p.drawRect(QRectF(14, 3, 7, 5));
        p.drawRect(QRectF(14, 12, 7, 9)); p.drawRect(QRectF(3, 16, 7, 5));
    } else if (name == "send") {
        line(7, 17, 17, 7); poly({{8, 7}, {17, 7}, {17, 16}});
    } else if (name == "receive") {
        line(17, 7, 7, 17); poly({{16, 17}, {7, 17}, {7, 8}});
    } else if (name == "history") {
        line(4, 6, 20, 6); line(4, 12, 20, 12); line(4, 18, 14, 18);
    } else if (name == "tokens") {
        p.drawEllipse(QPointF(9, 12), 6, 6);
        p.drawArc(QRectF(7.13, 6, 12, 12), 72 * 16, -144 * 16);
    } else if (name == "explorer" || name == "search") {
        p.drawEllipse(QPointF(11, 11), 6.5, 6.5); line(16, 16, 20, 20);
    } else if (name == "create") {
        p.drawEllipse(QPointF(12, 12), 9, 9); line(12, 8, 12, 16); line(8, 12, 16, 12);
    } else if (name == "manage") {
        line(4, 7, 20, 7); line(4, 17, 20, 17);
        p.setBrush(color);
        p.drawEllipse(QPointF(9, 7), 2.2, 2.2); p.drawEllipse(QPointF(15, 17), 2.2, 2.2);
    } else if (name == "addressbook") {
        p.drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
        p.drawEllipse(QPointF(12, 10), 2.5, 2.5);
        p.drawArc(QRectF(8, 14.44, 8, 8), 29 * 16, 122 * 16);
    } else if (name == "paste") {
        p.drawRoundedRect(QRectF(6, 5, 12, 16), 2, 2); poly({{9, 5}, {9, 3}, {15, 3}, {15, 5}});
    } else if (name == "copy") {
        p.drawRoundedRect(QRectF(9, 9, 11, 11), 2, 2); poly({{5, 15}, {4, 15}, {4, 4}, {15, 4}, {15, 5}});
    } else if (name == "download") {
        line(12, 4, 12, 15); poly({{7, 11}, {12, 16}, {17, 11}}); line(5, 20, 19, 20);
    } else if (name == "chevron") {
        poly({{6, 9}, {12, 15}, {18, 9}});
    } else if (name == "chevron-right") {
        poly({{9, 6}, {15, 12}, {9, 18}});
    } else if (name == "tag") {
        poly({{3, 4}, {12, 4}, {21, 13}, {13, 21}, {3, 11}, {3, 4}}); p.drawEllipse(QPointF(8, 9), 1.2, 1.2);
    } else if (name == "clock") {
        p.drawEllipse(QPointF(12, 12), 9, 9); poly({{12, 7}, {12, 12}, {15.5, 14}});
    } else if (name == "qr") {
        p.drawRect(QRectF(4, 4, 6, 6)); p.drawRect(QRectF(14, 4, 6, 6)); p.drawRect(QRectF(4, 14, 6, 6));
        line(14, 14, 14, 20); line(17, 17, 20, 17); line(20, 14, 20, 14.1); line(17, 20, 20, 20);
    } else if (name == "plus") {
        line(12, 5, 12, 19); line(5, 12, 19, 12);
    } else if (name == "remove") {
        line(6, 6, 18, 18); line(18, 6, 6, 18);
    } else if (name == "lock") {
        p.drawRoundedRect(QRectF(5, 11, 14, 10), 2, 2); p.drawArc(QRectF(8, 3, 8, 12), 0, 180 * 16); line(8, 9, 8, 11); line(16, 9, 16, 11);
    } else if (name == "stake") {
        poly({{12, 3}, {20, 12}, {12, 21}, {4, 12}, {12, 3}}); line(12, 9, 12, 15); line(9, 12, 15, 12);
    } else if (name == "sign") {
        // a pen on a line
        poly({{5, 17}, {5, 13.5}, {14.5, 4}, {18, 7.5}, {8.5, 17}, {5, 17}}); line(12.5, 6, 16, 9.5); line(4, 21, 20, 21);
    } else if (name == "globe") {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawEllipse(QPointF(12, 12), 4, 9);
        line(3, 12, 21, 12);
    } else if (name == "shield") {
        poly({{12, 3}, {19, 6}, {19, 11}, {17.5, 15.5}, {12, 21}, {6.5, 15.5}, {5, 11}, {5, 6}, {12, 3}});
        poly({{9, 12}, {11.2, 14.2}, {15.5, 10}});
    } else if (name == "people") {
        p.drawEllipse(QPointF(8, 9), 3, 3); p.drawEllipse(QPointF(17, 9), 3, 3);
        p.drawArc(QRectF(2.5, 13.5, 11, 11), 0, 180 * 16);
        p.drawArc(QRectF(12.5, 14, 9, 9), 10 * 16, 120 * 16);
    } else if (name == "settings") {
        // sliders
        line(4, 6, 20, 6); line(4, 12, 20, 12); line(4, 18, 20, 18);
        p.setBrush(color);
        p.drawEllipse(QPointF(15, 6), 2, 2); p.drawEllipse(QPointF(8, 12), 2, 2); p.drawEllipse(QPointF(13, 18), 2, 2);
    } else if (name == "dots") {
        p.setBrush(color);
        p.drawEllipse(QPointF(5, 12), 1.2, 1.2); p.drawEllipse(QPointF(12, 12), 1.2, 1.2); p.drawEllipse(QPointF(19, 12), 1.2, 1.2);
    }
    return pixmap;
}

void fillLanguageCombo(QComboBox* combo)
{
    combo->clear();
    // readable whatever the current language is
    const QString translated = QCoreApplication::translate("GUIUtil", "System default");
    combo->addItem(translated == "System default" ? translated : QString("System default / %1").arg(translated), QVariant(""));
    QDir translations(":translations");
    for (const QString& name : translations.entryList()) {
        const QLocale locale(name);
        QString label = locale.nativeLanguageName();
        if (label.isEmpty())
            label = QLocale::languageToString(locale.language());
        if (!label.isEmpty())
            label[0] = label[0].toUpper();
        if (name.contains("_") && !locale.nativeCountryName().isEmpty())
            label += QString(" - ") + locale.nativeCountryName();
        combo->addItem(label + QString(" (") + name + QString(")"), QVariant(name));
    }
}

static bool fRestartRequested = false;
static QStringList restartExtraArguments;

void requestRestart(const QStringList& extraArguments)
{
    fRestartRequested = true;
    restartExtraArguments = extraArguments;
}

QStringList restartArguments()
{
    return restartExtraArguments;
}

bool restartRequested()
{
    return fRestartRequested;
}

QPixmap tokenLogo(const QString& tokenName, int size)
{
    QString root = tokenName;
    for (const QChar sep : {QChar('/'), QChar('#'), QChar('!'), QChar(' ')}) {
        const int pos = root.indexOf(sep);
        if (pos >= 0)
            root = root.left(pos);
    }
    static QMap<QString, QPixmap> cache;
    const QString key = root + QChar(':') + QString::number(size);
    QMap<QString, QPixmap>::const_iterator it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    const Theme& t = theme();
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform, true);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(circle);
    QPixmap logo;
    if (root == "AOK")
        logo.load(":/icons/aok_logo");
    else if (!root.isEmpty())
        logo.load(":/tokens/" + root.toLower());
    if (!logo.isNull()) {
        p.fillRect(QRectF(0, 0, size, size), Qt::white);
        p.drawPixmap(QRectF(0, 0, size, size), logo, QRectF(logo.rect()));
    } else {
        p.fillRect(QRectF(0, 0, size, size), t.accentSoft);
        p.setPen(t.accent);
        p.setFont(appFont(qMax(10, size * 45 / 100), QFont::DemiBold));
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, root.left(1));
    }
    p.end();
    cache.insert(key, pixmap);
    return pixmap;
}

void paintDirectionTile(QPainter* painter, const QRect& tile, bool outgoing, bool reward)
{
    const Theme& t = theme();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(outgoing ? t.negativeSoft : t.positiveSoft);
    painter->drawRoundedRect(tile, 3, 3);
    const QPixmap icon = lineIcon(reward ? "stake" : (outgoing ? "send" : "receive"), 16, outgoing ? t.negative : t.positive, 2.4);
    painter->drawPixmap(tile.left() + (tile.width() - 16) / 2, tile.top() + (tile.height() - 16) / 2, icon);
    painter->restore();
}

LogTimer::LogTimer(const QString& what) : m_what(what), m_start(GetTimeMicros())
{
}

LogTimer::~LogTimer()
{
    LogPrint(BCLog::QT, "GUI timing: %s%s %.1f ms\n", m_what.toStdString(), m_detail.isEmpty() ? "" : " (" + m_detail.toStdString() + ")",
             (GetTimeMicros() - m_start) / 1000.0);
}

QString monoHtml(const QString& escapedText)
{
    return "<span style=\"font-family:'Noto Sans Mono';\">" + escapedText + "</span>";
}

void MonoColumnDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (monoColumns.contains(index.column()))
        option->font = monoFont(option->font.pixelSize() > 0 ? option->font.pixelSize() : 13, option->font.weight());
}

void MonoColumnDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!amountColumns.contains(index.column())) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString text = opt.text;
    // background, selection and decoration as usual, the text by paintAmount()
    opt.text.clear();
    const QWidget *widget = opt.widget;
    (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    const QVariant foreground = index.data(Qt::ForegroundRole);
    const QColor color = foreground.canConvert<QColor>() && foreground.value<QColor>().isValid() ? foreground.value<QColor>() : theme().text;
    const Qt::Alignment align = (opt.displayAlignment & Qt::AlignRight) ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter;
    paintAmount(painter, opt.rect.adjusted(8, 0, -8, 0), withThousands(text), monoFont(14, QFont::Medium), color, align);
}

QString monoToolTip(const QString& value, const QString& caption)
{
    QString html = "<nobr>";
    if (!caption.isEmpty())
        html += HtmlEscape(caption) + "<br>";
    html += "<span style=\"font-family:'Noto Sans Mono'; font-size:12px;\">" + HtmlEscape(value) + "</span></nobr>";
    return html;
}

QString isoDateTime(const QDateTime& dateTime, bool seconds)
{
    return dateTime.toString(seconds ? "yyyy-MM-dd HH:mm:ss" : "yyyy-MM-dd HH:mm");
}

static QString tokenRootName(const QString& tokenName)
{
    QString root = tokenName;
    for (const QChar sep : {QChar('/'), QChar('#'), QChar('!'), QChar(' ')}) {
        const int pos = root.indexOf(sep);
        if (pos >= 0)
            root = root.left(pos);
    }
    return root;
}

bool hasTokenLogo(const QString& tokenName)
{
    const QString root = tokenRootName(tokenName);
    return root == "AOK" || (!root.isEmpty() && QFile::exists(":/tokens/" + root.toLower()));
}

static QFont fractionFont(const QFont& font)
{
    QFont small = font;
    small.setPixelSize(qMax(9, qRound(font.pixelSize() * 0.6)));
    small.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    return small;
}

/** Where the fraction starts; -1 when there is none or when it is not plain digits (1.5e-10). */
static int fractionStart(const QString& amount)
{
    const int point = amount.indexOf('.');
    for (int i = point + 1; point >= 0 && i < amount.size(); ++i)
        if (!amount[i].isDigit())
            return -1;
    return point;
}

int amountWidth(const QString& amount, const QFont& font)
{
    const int point = fractionStart(amount);
    if (point < 0)
        return QFontMetrics(font).horizontalAdvance(amount);
    return QFontMetrics(font).horizontalAdvance(amount.left(point)) + QFontMetrics(fractionFont(font)).horizontalAdvance(amount.mid(point));
}

int paintAmount(QPainter* painter, const QRect& rect, const QString& amount, const QFont& font, const QColor& color, Qt::Alignment align)
{
    const int point = fractionStart(amount);
    const QString whole = point < 0 ? amount : amount.left(point);
    const QString fraction = point < 0 ? QString() : amount.mid(point);
    const QFontMetrics fm(font);
    const QFont small = fractionFont(font);
    const int wholeWidth = fm.horizontalAdvance(whole);
    const int width = wholeWidth + (fraction.isEmpty() ? 0 : QFontMetrics(small).horizontalAdvance(fraction));
    int x = rect.left();
    if (align & Qt::AlignRight)
        x = rect.right() + 1 - width;
    else if (align & Qt::AlignHCenter)
        x = rect.left() + (rect.width() - width) / 2;
    const int baseline = rect.top() + (rect.height() - fm.height()) / 2 + fm.ascent();
    painter->save();
    painter->setPen(color);
    painter->setFont(font);
    painter->drawText(x, baseline, whole);
    if (!fraction.isEmpty()) {
        painter->setFont(small);
        painter->drawText(x + wholeWidth, baseline, fraction);
    }
    painter->restore();
    return width;
}

int paintChip(QPainter* painter, int x, int centerY, const QString& text)
{
    const Theme& t = theme();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(appFont(11, QFont::Medium));
    const int w = painter->fontMetrics().horizontalAdvance(text) + 16;
    const QRect r(x, centerY - 10, w, 20);
    painter->setPen(Qt::NoPen);
    painter->setBrush(t.chip);
    painter->drawRoundedRect(r, 3, 3);
    painter->setPen(t.chipText);
    painter->drawText(r, Qt::AlignCenter, text);
    painter->restore();
    return w;
}

void paintBadge(QPainter* painter, const QRect& rect, const QString& text, const QColor& fg, const QColor& bg, Qt::Alignment align)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(appFont(12, QFont::Medium));
    const int w = qMin(rect.width(), painter->fontMetrics().horizontalAdvance(text) + 20);
    const int h = 24;
    const int x = (align & Qt::AlignRight) ? rect.right() - w + 1 : rect.left();
    const QRect r(x, rect.center().y() - h / 2 + 1, w, h);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(r, 3, 3);
    painter->setPen(fg);
    painter->drawText(r, Qt::AlignCenter, painter->fontMetrics().elidedText(text, Qt::ElideRight, w - 12));
    painter->restore();
}

QString labelColorStyle()
{
    return QString("color: %1").arg(theme().text.name());
}

QString invalidFieldStyle()
{
    const Theme& t = theme();
    return QString("background-color: %1; border: 1px solid %2;")
        .arg(t.errorSoft.name(), t.error.name());
}

QString validFieldStyle()
{
    return QString();
}

QString cardFrameStyle(const QString& selector)
{
    const Theme& t = theme();
    return QString("%1 { background-color: %2; border: 1px solid %3; border-radius: 4px; padding: 10px 8px; }")
        .arg(selector, t.surface.name(), t.border.name());
}

QFont getSubLabelFont()
{
    return appFont(13, QFont::Normal);
}

QFont getSubLabelFontBolded()
{
    return appFont(13, QFont::DemiBold);
}

QFont getTopLabelFontBolded()
{
    return appFont(16, QFont::DemiBold);
}

QFont getTopLabelFont(int weight, int pxsize)
{
    return appFont(pxsize, weight);
}

QFont getTopLabelFont()
{
    return appFont(16, QFont::DemiBold);
}

QString dateTimeStr(const QDateTime &date)
{
    return date.date().toString(Qt::SystemLocaleShortDate) + QString(" ") + date.toString("hh:mm");
}

QString dateTimeStr(qint64 nTime)
{
    return dateTimeStr(QDateTime::fromTime_t((qint32)nTime));
}

QFont fixedPitchFont()
{
    return monoFont();
}

// Just some dummy data to generate an convincing random-looking (but consistent) address
static const uint8_t dummydata[] = {0xeb,0x15,0x23,0x1d,0xfc,0xeb,0x60,0x92,0x58,0x86,0xb6,0x7d,0x06,0x52,0x99,0x92,0x59,0x15,0xae,0xb1,0x72,0xc0,0x66,0x47};

// Generate a dummy address with invalid CRC, starting with the network prefix.
static std::string DummyAddress(const CChainParams &params)
{
    std::vector<unsigned char> sourcedata = params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
    sourcedata.insert(sourcedata.end(), dummydata, dummydata + sizeof(dummydata));
    for(int i=0; i<256; ++i) { // Try every trailing byte
        std::string s = EncodeBase58(sourcedata.data(), sourcedata.data() + sourcedata.size());
        if (!IsValidDestinationString(s)) {
            return s;
        }
        sourcedata[sourcedata.size()-1] += 1;
    }
    return "";
}

void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent)
{
    parent->setFocusProxy(widget);

    widget->setFont(monoFont(13));
#if QT_VERSION >= 0x040700
    // We don't want translators to use own addresses in translations
    // and this is the only place, where this address is supplied.
    widget->setPlaceholderText(QObject::tr("Enter an AokChain address (e.g. %1)").arg(
        QString::fromStdString(DummyAddress(Params()))));
#endif
    widget->setValidator(new AokChainAddressEntryValidator(parent));
    widget->setCheckValidator(new AokChainAddressCheckValidator(parent));
}

void setupAmountWidget(QLineEdit *widget, QWidget *parent)
{
    QDoubleValidator *amountValidator = new QDoubleValidator(parent);
    amountValidator->setDecimals(8);
    amountValidator->setBottom(0.0);
    widget->setValidator(amountValidator);
    widget->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
}

bool parseAokChainURI(const QUrl &uri, SendCoinsRecipient *out)
{
    // return if URI is not valid or is no aokchain: URI
    if(!uri.isValid() || uri.scheme() != QString("aokchain"))
        return false;

    SendCoinsRecipient rv;
    rv.address = uri.path();
    // Trim any following forward slash which may have been added by the OS
    if (rv.address.endsWith("/")) {
        rv.address.truncate(rv.address.length() - 1);
    }
    rv.amount = 0;

#if QT_VERSION < 0x050000
    QList<QPair<QString, QString> > items = uri.queryItems();
#else
    QUrlQuery uriQuery(uri);
    QList<QPair<QString, QString> > items = uriQuery.queryItems();
#endif
    for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
    {
        bool fShouldReturnFalse = false;
        if (i->first.startsWith("req-"))
        {
            i->first.remove(0, 4);
            fShouldReturnFalse = true;
        }

        if (i->first == "label")
        {
            rv.label = i->second;
            fShouldReturnFalse = false;
        }
        if (i->first == "message")
        {
            rv.message = i->second;
            fShouldReturnFalse = false;
        }
        else if (i->first == "amount")
        {
            if(!i->second.isEmpty())
            {
                if(!AokChainUnits::parse(AokChainUnits::AOK, i->second, &rv.amount))
                {
                    return false;
                }
            }
            fShouldReturnFalse = false;
        }

        if (fShouldReturnFalse)
            return false;
    }
    if(out)
    {
        *out = rv;
    }
    return true;
}

bool parseAokChainURI(QString uri, SendCoinsRecipient *out)
{
    // Convert aokchain:// to aokchain:
    //
    //    Cannot handle this later, because aokchain:// will cause Qt to see the part after // as host,
    //    which will lower-case it (and thus invalidate the address).
    if(uri.startsWith("aokchain://", Qt::CaseInsensitive))
    {
        uri.replace(0, 10, "aokchain:");
    }
    QUrl uriInstance(uri);
    return parseAokChainURI(uriInstance, out);
}

QString formatAokChainURI(const SendCoinsRecipient &info)
{
    QString ret = QString("aokchain:%1").arg(info.address);
    int paramCount = 0;

    if (info.amount)
    {
        ret += QString("?amount=%1").arg(AokChainUnits::format(AokChainUnits::AOK, info.amount, false, AokChainUnits::separatorNever));
        paramCount++;
    }

    if (!info.label.isEmpty())
    {
        QString lbl(QUrl::toPercentEncoding(info.label));
        ret += QString("%1label=%2").arg(paramCount == 0 ? "?" : "&").arg(lbl);
        paramCount++;
    }

    if (!info.message.isEmpty())
    {
        QString msg(QUrl::toPercentEncoding(info.message));
        ret += QString("%1message=%2").arg(paramCount == 0 ? "?" : "&").arg(msg);
        paramCount++;
    }

    return ret;
}

bool isDust(const QString& address, const CAmount& amount)
{
    CTxDestination dest = DecodeDestination(address.toStdString());
    CScript script = GetScriptForDestination(dest);
    CTxOut txOut(amount, script);
    return IsDust(txOut, ::dustRelayFee);
}

QString HtmlEscape(const QString& str, bool fMultiLine)
{
#if QT_VERSION < 0x050000
    QString escaped = Qt::escape(str);
#else
    QString escaped = str.toHtmlEscaped();
#endif
    if(fMultiLine)
    {
        escaped = escaped.replace("\n", "<br>\n");
    }
    return escaped;
}

QString HtmlEscape(const std::string& str, bool fMultiLine)
{
    return HtmlEscape(QString::fromStdString(str), fMultiLine);
}

void copyEntryData(QAbstractItemView *view, int column, int role)
{
    if(!view || !view->selectionModel())
        return;
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if(!selection.isEmpty())
    {
        // Copy first item
        setClipboard(selection.at(0).data(role).toString());
    }
}

QList<QModelIndex> getEntryData(QAbstractItemView *view, int column)
{
    if(!view || !view->selectionModel())
        return QList<QModelIndex>();
    return view->selectionModel()->selectedRows(column);
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if(dir.isEmpty()) // Default to user documents location
    {
#if QT_VERSION < 0x050000
        myDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result = QDir::toNativeSeparators(QFileDialog::getSaveFileName(parent, caption, myDir, filter, &selectedFilter));

    /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
    QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
    QString selectedSuffix;
    if(filter_re.exactMatch(selectedFilter))
    {
        selectedSuffix = filter_re.cap(1);
    }

    /* Add suffix if needed */
    QFileInfo info(result);
    if(!result.isEmpty())
    {
        if(info.suffix().isEmpty() && !selectedSuffix.isEmpty())
        {
            /* No suffix specified, add selected suffix */
            if(!result.endsWith("."))
                result.append(".");
            result.append(selectedSuffix);
        }
    }

    /* Return selected suffix if asked to */
    if(selectedSuffixOut)
    {
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if(dir.isEmpty()) // Default to user documents location
    {
#if QT_VERSION < 0x050000
        myDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result = QDir::toNativeSeparators(QFileDialog::getOpenFileName(parent, caption, myDir, filter, &selectedFilter));

    if(selectedSuffixOut)
    {
        /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
        QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
        QString selectedSuffix;
        if(filter_re.exactMatch(selectedFilter))
        {
            selectedSuffix = filter_re.cap(1);
        }
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

Qt::ConnectionType blockingGUIThreadConnection()
{
    if(QThread::currentThread() != qApp->thread())
    {
        return Qt::BlockingQueuedConnection;
    }
    else
    {
        return Qt::DirectConnection;
    }
}

bool checkPoint(const QPoint &p, const QWidget *w)
{
    QWidget *atW = QApplication::widgetAt(w->mapToGlobal(p));
    if (!atW) return false;
    return atW->topLevelWidget() == w;
}

bool isObscured(QWidget *w)
{
    return !(checkPoint(QPoint(0, 0), w)
        && checkPoint(QPoint(w->width() - 1, 0), w)
        && checkPoint(QPoint(0, w->height() - 1), w)
        && checkPoint(QPoint(w->width() - 1, w->height() - 1), w)
        && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

void openDebugLogfile()
{
    fs::path pathDebug = GetDataDir() / "debug.log";

    /* Open debug.log with the associated application */
    if (fs::exists(pathDebug))
        QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathDebug)));
}

bool openAokChainConf()
{
    boost::filesystem::path pathConfig = GetConfigFile(AOKCHAIN_CONF_FILENAME);

    /* Create the file */
    boost::filesystem::ofstream configFile(pathConfig, std::ios_base::app);
    
    if (!configFile.good())
        return false;
    
    configFile.close();
    
    /* Open aokchain.conf with the associated application */
    return QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathConfig)));
}

void SubstituteFonts(const QString& language)
{
#if defined(Q_OS_MAC)
// Background:
// OSX's default font changed in 10.9 and Qt is unable to find it with its
// usual fallback methods when building against the 10.7 sdk or lower.
// The 10.8 SDK added a function to let it find the correct fallback font.
// If this fallback is not properly loaded, some characters may fail to
// render correctly.
//
// The same thing happened with 10.10. .Helvetica Neue DeskInterface is now default.
//
// Solution: If building with the 10.7 SDK or lower and the user's platform
// is 10.9 or higher at runtime, substitute the correct font. This needs to
// happen before the QApplication is created.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_8
    if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_8)
    {
        if (floor(NSAppKitVersionNumber) <= NSAppKitVersionNumber10_9)
            /* On a 10.9 - 10.9.x system */
            QFont::insertSubstitution(".Lucida Grande UI", "Lucida Grande");
        else
        {
            /* 10.10 or later system */
            if (language == "zh_CN" || language == "zh_TW" || language == "zh_HK") // traditional or simplified Chinese
              QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Heiti SC");
            else if (language == "ja") // Japanese
              QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Songti SC");
            else
              QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Lucida Grande");
        }
    }
#endif
#endif
}

ToolTipToRichTextFilter::ToolTipToRichTextFilter(int _size_threshold, QObject *parent) :
    QObject(parent),
    size_threshold(_size_threshold)
{

}

bool ToolTipToRichTextFilter::eventFilter(QObject *obj, QEvent *evt)
{
    if(evt->type() == QEvent::ToolTipChange)
    {
        QWidget *widget = static_cast<QWidget*>(obj);
        QString tooltip = widget->toolTip();
        if(tooltip.size() > size_threshold && !tooltip.startsWith("<qt") && !Qt::mightBeRichText(tooltip))
        {
            // Envelop with <qt></qt> to make sure Qt detects this as rich text
            // Escape the current message as HTML and replace \n by <br>
            tooltip = "<qt>" + HtmlEscape(tooltip, true) + "</qt>";
            widget->setToolTip(tooltip);
            return true;
        }
    }
    return QObject::eventFilter(obj, evt);
}

void TableViewLastColumnResizingFixer::connectViewHeadersSignals()
{
    connect(tableView->horizontalHeader(), SIGNAL(sectionResized(int,int,int)), this, SLOT(on_sectionResized(int,int,int)));
    connect(tableView->horizontalHeader(), SIGNAL(geometriesChanged()), this, SLOT(on_geometriesChanged()));
}

// We need to disconnect these while handling the resize events, otherwise we can enter infinite loops.
void TableViewLastColumnResizingFixer::disconnectViewHeadersSignals()
{
    disconnect(tableView->horizontalHeader(), SIGNAL(sectionResized(int,int,int)), this, SLOT(on_sectionResized(int,int,int)));
    disconnect(tableView->horizontalHeader(), SIGNAL(geometriesChanged()), this, SLOT(on_geometriesChanged()));
}

// Setup the resize mode, handles compatibility for Qt5 and below as the method signatures changed.
// Refactored here for readability.
void TableViewLastColumnResizingFixer::setViewHeaderResizeMode(int logicalIndex, QHeaderView::ResizeMode resizeMode)
{
#if QT_VERSION < 0x050000
    tableView->horizontalHeader()->setResizeMode(logicalIndex, resizeMode);
#else
    tableView->horizontalHeader()->setSectionResizeMode(logicalIndex, resizeMode);
#endif
}

void TableViewLastColumnResizingFixer::resizeColumn(int nColumnIndex, int width)
{
    tableView->setColumnWidth(nColumnIndex, width);
    tableView->horizontalHeader()->resizeSection(nColumnIndex, width);
}

int TableViewLastColumnResizingFixer::getColumnsWidth()
{
    int nColumnsWidthSum = 0;
    for (int i = 0; i < columnCount; i++)
    {
        nColumnsWidthSum += tableView->horizontalHeader()->sectionSize(i);
    }
    return nColumnsWidthSum;
}

int TableViewLastColumnResizingFixer::getAvailableWidthForColumn(int column)
{
    int nResult = lastColumnMinimumWidth;
    int nTableWidth = tableView->horizontalHeader()->width();

    if (nTableWidth > 0)
    {
        int nOtherColsWidth = getColumnsWidth() - tableView->horizontalHeader()->sectionSize(column);
        nResult = std::max(nResult, nTableWidth - nOtherColsWidth);
    }

    return nResult;
}

// Make sure we don't make the columns wider than the table's viewport width.
void TableViewLastColumnResizingFixer::adjustTableColumnsWidth()
{
    disconnectViewHeadersSignals();
    resizeColumn(lastColumnIndex, getAvailableWidthForColumn(lastColumnIndex));
    connectViewHeadersSignals();

    int nTableWidth = tableView->horizontalHeader()->width();
    int nColsWidth = getColumnsWidth();
    if (nColsWidth > nTableWidth)
    {
        resizeColumn(secondToLastColumnIndex,getAvailableWidthForColumn(secondToLastColumnIndex));
    }
}

// Make column use all the space available, useful during window resizing.
void TableViewLastColumnResizingFixer::stretchColumnWidth(int column)
{
    disconnectViewHeadersSignals();
    resizeColumn(column, getAvailableWidthForColumn(column));
    connectViewHeadersSignals();
}

// When a section is resized this is a slot-proxy for ajustAmountColumnWidth().
void TableViewLastColumnResizingFixer::on_sectionResized(int logicalIndex, int oldSize, int newSize)
{
    adjustTableColumnsWidth();
    int remainingWidth = getAvailableWidthForColumn(logicalIndex);
    if (newSize > remainingWidth)
    {
       resizeColumn(logicalIndex, remainingWidth);
    }
}

// When the table's geometry is ready, we manually perform the stretch of the "Message" column,
// as the "Stretch" resize mode does not allow for interactive resizing.
void TableViewLastColumnResizingFixer::on_geometriesChanged()
{
    if ((getColumnsWidth() - this->tableView->horizontalHeader()->width()) != 0)
    {
        disconnectViewHeadersSignals();
        resizeColumn(secondToLastColumnIndex, getAvailableWidthForColumn(secondToLastColumnIndex));
        connectViewHeadersSignals();
    }
}

/**
 * Initializes all internal variables and prepares the
 * the resize modes of the last 2 columns of the table and
 */
TableViewLastColumnResizingFixer::TableViewLastColumnResizingFixer(QTableView* table, int lastColMinimumWidth, int allColsMinimumWidth, QObject *parent) :
    QObject(parent),
    tableView(table),
    lastColumnMinimumWidth(lastColMinimumWidth),
    allColumnsMinimumWidth(allColsMinimumWidth)
{
    columnCount = tableView->horizontalHeader()->count();
    lastColumnIndex = columnCount - 1;
    secondToLastColumnIndex = columnCount - 2;
    tableView->horizontalHeader()->setMinimumSectionSize(allColumnsMinimumWidth);
    setViewHeaderResizeMode(columnCount - 3, QHeaderView::ResizeToContents);
    setViewHeaderResizeMode(secondToLastColumnIndex, QHeaderView::ResizeToContents);
    setViewHeaderResizeMode(lastColumnIndex, QHeaderView::Stretch);
}

#ifdef WIN32
fs::path static StartupShortcutPath()
{
    std::string chain = ChainNameFromCommandLine();
    if (chain == CBaseChainParams::MAIN)
        return GetSpecialFolderPath(CSIDL_STARTUP) / "AokChain.lnk";
    if (chain == CBaseChainParams::TESTNET) // Remove this special case when CBaseChainParams::TESTNET = "testnet4"
        return GetSpecialFolderPath(CSIDL_STARTUP) / "AokChain (testnet).lnk";
    return GetSpecialFolderPath(CSIDL_STARTUP) / strprintf("AokChain (%s).lnk", chain);
}

bool GetStartOnSystemStartup()
{
    // check for AokChain*.lnk
    return fs::exists(StartupShortcutPath());
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    // If the shortcut exists already, remove it for updating
    fs::remove(StartupShortcutPath());

    if (fAutoStart)
    {
        CoInitialize(nullptr);

        // Get a pointer to the IShellLink interface.
        IShellLink* psl = nullptr;
        HRESULT hres = CoCreateInstance(CLSID_ShellLink, nullptr,
            CLSCTX_INPROC_SERVER, IID_IShellLink,
            reinterpret_cast<void**>(&psl));

        if (SUCCEEDED(hres))
        {
            // Get the current executable path
            TCHAR pszExePath[MAX_PATH];
            GetModuleFileName(nullptr, pszExePath, sizeof(pszExePath));

            // Start client minimized
            QString strArgs = "-min";
            // Set -testnet /-regtest options
            strArgs += QString::fromStdString(strprintf(" -testnet=%d -regtest=%d", gArgs.GetBoolArg("-testnet", false), gArgs.GetBoolArg("-regtest", false)));

#ifdef UNICODE
            boost::scoped_array<TCHAR> args(new TCHAR[strArgs.length() + 1]);
            // Convert the QString to TCHAR*
            strArgs.toWCharArray(args.get());
            // Add missing '\0'-termination to string
            args[strArgs.length()] = '\0';
#endif

            // Set the path to the shortcut target
            psl->SetPath(pszExePath);
            PathRemoveFileSpec(pszExePath);
            psl->SetWorkingDirectory(pszExePath);
            psl->SetShowCmd(SW_SHOWMINNOACTIVE);
#ifndef UNICODE
            psl->SetArguments(strArgs.toStdString().c_str());
#else
            psl->SetArguments(args.get());
#endif

            // Query IShellLink for the IPersistFile interface for
            // saving the shortcut in persistent storage.
            IPersistFile* ppf = nullptr;
            hres = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
            if (SUCCEEDED(hres))
            {
                WCHAR pwsz[MAX_PATH];
                // Ensure that the string is ANSI.
                MultiByteToWideChar(CP_ACP, 0, StartupShortcutPath().string().c_str(), -1, pwsz, MAX_PATH);
                // Save the link by calling IPersistFile::Save.
                hres = ppf->Save(pwsz, TRUE);
                ppf->Release();
                psl->Release();
                CoUninitialize();
                return true;
            }
            psl->Release();
        }
        CoUninitialize();
        return false;
    }
    return true;
}
#elif defined(Q_OS_LINUX)

// Follow the Desktop Application Autostart Spec:
// http://standards.freedesktop.org/autostart-spec/autostart-spec-latest.html

fs::path static GetAutostartDir()
{
    char* pszConfigHome = getenv("XDG_CONFIG_HOME");
    if (pszConfigHome) return fs::path(pszConfigHome) / "autostart";
    char* pszHome = getenv("HOME");
    if (pszHome) return fs::path(pszHome) / ".config" / "autostart";
    return fs::path();
}

fs::path static GetAutostartFilePath()
{
    std::string chain = ChainNameFromCommandLine();
    if (chain == CBaseChainParams::MAIN)
        return GetAutostartDir() / "aokchain.desktop";
    return GetAutostartDir() / strprintf("aokchain-%s.lnk", chain);
}

bool GetStartOnSystemStartup()
{
    fs::ifstream optionFile(GetAutostartFilePath());
    if (!optionFile.good())
        return false;
    // Scan through file for "Hidden=true":
    std::string line;
    while (!optionFile.eof())
    {
        getline(optionFile, line);
        if (line.find("Hidden") != std::string::npos &&
            line.find("true") != std::string::npos)
            return false;
    }
    optionFile.close();

    return true;
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    if (!fAutoStart)
        fs::remove(GetAutostartFilePath());
    else
    {
        char pszExePath[MAX_PATH+1];
        ssize_t r = readlink("/proc/self/exe", pszExePath, sizeof(pszExePath) - 1);
        if (r == -1)
            return false;
        pszExePath[r] = '\0';

        fs::create_directories(GetAutostartDir());

        fs::ofstream optionFile(GetAutostartFilePath(), std::ios_base::out|std::ios_base::trunc);
        if (!optionFile.good())
            return false;
        std::string chain = ChainNameFromCommandLine();
        // Write a aokchain.desktop file to the autostart directory:
        optionFile << "[Desktop Entry]\n";
        optionFile << "Type=Application\n";
        if (chain == CBaseChainParams::MAIN)
            optionFile << "Name=AokChain\n";
        else
            optionFile << strprintf("Name=AokChain (%s)\n", chain);
        optionFile << "Exec=" << pszExePath << strprintf(" -min -testnet=%d -regtest=%d\n", gArgs.GetBoolArg("-testnet", false), gArgs.GetBoolArg("-regtest", false));
        optionFile << "Terminal=false\n";
        optionFile << "Hidden=false\n";
        optionFile.close();
    }
    return true;
}


#elif defined(Q_OS_MAC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// based on: https://github.com/Mozketo/LaunchAtLoginController/blob/master/LaunchAtLoginController.m

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

LSSharedFileListItemRef findStartupItemInList(LSSharedFileListRef list, CFURLRef findUrl);
LSSharedFileListItemRef findStartupItemInList(LSSharedFileListRef list, CFURLRef findUrl)
{
    CFArrayRef listSnapshot = LSSharedFileListCopySnapshot(list, nullptr);
    if (listSnapshot == nullptr) {
        return nullptr;
    }
    
    // loop through the list of startup items and try to find the aokchain app
    for(int i = 0; i < CFArrayGetCount(listSnapshot); i++) {
        LSSharedFileListItemRef item = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(listSnapshot, i);
        UInt32 resolutionFlags = kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes;
        CFURLRef currentItemURL = nullptr;

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 10100
        if(&LSSharedFileListItemCopyResolvedURL)
            currentItemURL = LSSharedFileListItemCopyResolvedURL(item, resolutionFlags, nullptr);
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 10100
        else
            LSSharedFileListItemResolve(item, resolutionFlags, &currentItemURL, nullptr);
#endif
#else
        LSSharedFileListItemResolve(item, resolutionFlags, &currentItemURL, nullptr);
#endif

        if(currentItemURL) {
            if (CFEqual(currentItemURL, findUrl)) {
                // found
                CFRelease(listSnapshot);
                CFRelease(currentItemURL);
                return item;
            }
            CFRelease(currentItemURL);
        }
    }
    
    CFRelease(listSnapshot);
    return nullptr;
}

bool GetStartOnSystemStartup()
{
    CFURLRef aokchainAppUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    if (aokchainAppUrl == nullptr) {
        return false;
    }
    
    LSSharedFileListRef loginItems = LSSharedFileListCreate(nullptr, kLSSharedFileListSessionLoginItems, nullptr);
    LSSharedFileListItemRef foundItem = findStartupItemInList(loginItems, aokchainAppUrl);

    CFRelease(aokchainAppUrl);
    return !!foundItem; // return boolified object
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    CFURLRef aokchainAppUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    if (aokchainAppUrl == nullptr) {
        return false;
    }
    
    LSSharedFileListRef loginItems = LSSharedFileListCreate(nullptr, kLSSharedFileListSessionLoginItems, nullptr);
    LSSharedFileListItemRef foundItem = findStartupItemInList(loginItems, aokchainAppUrl);

    if(fAutoStart && !foundItem) {
        // add aokchain app to startup item list
        LSSharedFileListInsertItemURL(loginItems, kLSSharedFileListItemBeforeFirst, nullptr, nullptr, aokchainAppUrl, nullptr, nullptr);
    }
    else if(!fAutoStart && foundItem) {
        // remove item
        LSSharedFileListItemRemove(loginItems, foundItem);
    }
    
    CFRelease(aokchainAppUrl);
    return true;
}
#pragma GCC diagnostic pop
#else

bool GetStartOnSystemStartup() { return false; }
bool SetStartOnSystemStartup(bool fAutoStart) { return false; }

#endif

void setClipboard(const QString& str)
{
    QApplication::clipboard()->setText(str, QClipboard::Clipboard);
    QApplication::clipboard()->setText(str, QClipboard::Selection);
}

fs::path qstringToBoostPath(const QString &path)
{
#ifdef WIN32
    return fs::path(path.toStdString(), utf8);
#else
    return fs::path(path.toStdString());
#endif
}

QString boostPathToQString(const fs::path &path)
{
#ifdef WIN32
    return QString::fromStdString(path.string(utf8));
#else
    return QString::fromStdString(path.string());
#endif
}

QString formatDurationStr(int secs)
{
    QStringList strList;
    int days = secs / 86400;
    int hours = (secs % 86400) / 3600;
    int mins = (secs % 3600) / 60;
    int seconds = secs % 60;

    if (days)
        strList.append(QString(QObject::tr("%1 d")).arg(days));
    if (hours)
        strList.append(QString(QObject::tr("%1 h")).arg(hours));
    if (mins)
        strList.append(QString(QObject::tr("%1 m")).arg(mins));
    if (seconds || (!days && !hours && !mins))
        strList.append(QString(QObject::tr("%1 s")).arg(seconds));

    return strList.join(" ");
}

QString formatServicesStr(quint64 mask)
{
    QStringList strList;

    // Just scan the last 8 bits for now.
    for (int i = 0; i < 8; i++) {
        uint64_t check = 1 << i;
        if (mask & check)
        {
            switch (check)
            {
            case NODE_NETWORK:
                strList.append("NETWORK");
                break;
            case NODE_GETUTXO:
                strList.append("GETUTXO");
                break;
            case NODE_BLOOM:
                strList.append("BLOOM");
                break;
            case NODE_WITNESS:
                strList.append("WITNESS");
                break;
            case NODE_XTHIN:
                strList.append("XTHIN");
                break;
            default:
                strList.append(QString("%1[%2]").arg("UNKNOWN").arg(check));
            }
        }
    }

    if (strList.size())
        return strList.join(" & ");
    else
        return QObject::tr("None");
}

QString formatPingTime(double dPingTime)
{
    return (dPingTime == std::numeric_limits<int64_t>::max()/1e6 || dPingTime == 0) ? QObject::tr("N/A") : QString(QObject::tr("%1 ms")).arg(QString::number((int)(dPingTime * 1000), 10));
}

QString formatTimeOffset(int64_t nTimeOffset)
{
  return QString(QObject::tr("%1 s")).arg(QString::number((int)nTimeOffset, 10));
}

QString formatNiceTimeOffset(qint64 secs)
{
    // Represent time from last generated block in human readable text
    QString timeBehindText;
    const int HOUR_IN_SECONDS = 60*60;
    const int DAY_IN_SECONDS = 24*60*60;
    const int WEEK_IN_SECONDS = 7*24*60*60;
    const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
    if(secs < 60)
    {
        timeBehindText = QObject::tr("%n second(s)","",secs);
    }
    else if(secs < 2*HOUR_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n minute(s)","",secs/60);
    }
    else if(secs < 2*DAY_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
    }
    else if(secs < 2*WEEK_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n day(s)","",secs/DAY_IN_SECONDS);
    }
    else if(secs < YEAR_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n week(s)","",secs/WEEK_IN_SECONDS);
    }
    else
    {
        qint64 years = secs / YEAR_IN_SECONDS;
        qint64 remainder = secs % YEAR_IN_SECONDS;
        timeBehindText = QObject::tr("%1 and %2").arg(QObject::tr("%n year(s)", "", years)).arg(QObject::tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
    }
    return timeBehindText;
}

QString formatBytes(uint64_t bytes)
{
    if(bytes < 1024)
        return QString(QObject::tr("%1 B")).arg(bytes);
    if(bytes < 1024 * 1024)
        return QString(QObject::tr("%1 KB")).arg(bytes / 1024);
    if(bytes < 1024 * 1024 * 1024)
        return QString(QObject::tr("%1 MB")).arg(bytes / 1024 / 1024);

    return QString(QObject::tr("%1 GB")).arg(bytes / 1024 / 1024 / 1024);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    Q_EMIT clicked(event->pos());
}
    
void ClickableProgressBar::mouseReleaseEvent(QMouseEvent *event)
{
    Q_EMIT clicked(event->pos());
}

void concatenate(QPainter* painter, QString& catString, int static_width, int left_side, int right_size)
{
    // Starting length of the name
    int start_name_length = catString.size();

    // Get the length of the dots
    int dots_width = painter->fontMetrics().width("...");

    // Add the dots width to the amount width
    static_width += dots_width;

    // Start concatenation loop, end loop if name is at three characters
    while (catString.size() > 3) {
        // Get the text width of the current name
        int text_width = painter->fontMetrics().width(catString);

        // Check to see if the text width is going to overlap the amount width if it doesn't break the loop
        if (left_side + text_width < right_size - static_width)
            break;

        // substring the name minus the last character of it and continue the loop
        catString = catString.left(catString.size() - 1);
    }

    // Add the ... if the name was concatenated
    if (catString.size() != start_name_length)
        catString.append("...");
}

} // namespace GUIUtil
