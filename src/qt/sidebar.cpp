// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sidebar.h"

#include "guiutil.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QRegExp>
#include <QVBoxLayout>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(44, 26);
    setFocusPolicy(Qt::TabFocus);
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? t.sideBar : t.sideOff);
    p.drawRoundedRect(rect(), 13, 13);
    p.setBrush(isChecked() ? t.sideBg : t.sideStrong);
    p.drawEllipse(QRectF(isChecked() ? width() - 23 : 3, 3, 20, 20));
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.sideStrong, 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
    }
}

SideNavButton::SideNavButton(QAction* _action, const QString& _iconName, QWidget* parent) :
    QAbstractButton(parent), action(_action), iconName(_iconName), hovered(false)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(44);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::TabFocus);
    connect(this, SIGNAL(clicked()), action, SLOT(trigger()));
    connect(action, SIGNAL(changed()), this, SLOT(actionChanged()));
    connect(action, SIGNAL(toggled(bool)), this, SLOT(actionChanged()));
    actionChanged();
}

/** Text of an action without its mnemonic, also in the "Text(O)" form that CJK translations use. */
static QString plainActionText(const QAction* action)
{
    QString text = action->iconText();
    text.remove(QRegExp("\\s*\\(\\w\\)$"));
    return text;
}

void SideNavButton::actionChanged()
{
    setChecked(action->isChecked());
    setEnabled(action->isEnabled());
    setToolTip(action->toolTip());
    setText(plainActionText(action));
    update();
}

void SideNavButton::enterEvent(QEvent* event)
{
    hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void SideNavButton::leaveEvent(QEvent* event)
{
    hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void SideNavButton::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    // the button state follows the action; a click must not uncheck the active page
    const bool on = action->isChecked();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.sideOnBg);
        p.drawRoundedRect(rect(), 10, 10);
    }
    QColor color = (on || hovered || hasFocus()) ? t.sideStrong : t.sideText;
    if (!isEnabled())
        color = t.sideOff;
    p.drawPixmap(12, (height() - 18) / 2, GUIUtil::lineIcon(iconName, 18, color));
    p.setPen(color);
    p.setFont(GUIUtil::appFont(14, on ? QFont::DemiBold : QFont::Normal));
    p.drawText(QRect(42, 0, width() - 50, height()), Qt::AlignLeft | Qt::AlignVCenter, plainActionText(action));
}

SideProgress::SideProgress(QWidget* parent) : QWidget(parent), value(0)
{
    setFixedHeight(4);
}

void SideProgress::setValue(double fraction)
{
    value = qBound(0.0, fraction, 1.0);
    update();
}

void SideProgress::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(t.sideTrack);
    p.drawRoundedRect(rect(), 2, 2);
    if (value > 0) {
        p.setBrush(t.sideBar);
        p.drawRoundedRect(QRectF(0, 0, qMax(4.0, width() * value), height()), 2, 2);
    }
}

static QLabel* sideLabel(const QString& text, const QColor& color, int px, int weight)
{
    QLabel* label = new QLabel(text);
    label->setFont(GUIUtil::appFont(px, weight));
    label->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(color.name()));
    return label;
}

Sidebar::Sidebar(QWidget* parent) : QWidget(parent)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    setObjectName("sidebar");
    setFixedWidth(232);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 24, 16, 20);
    layout->setSpacing(0);

    // logo and word mark
    QHBoxLayout* brand = new QHBoxLayout();
    brand->setContentsMargins(8, 0, 8, 0);
    brand->setSpacing(10);
    QLabel* logo = new QLabel();
    logo->setFixedSize(36, 36);
    {
        const qreal dpr = devicePixelRatioF();
        QPixmap source(":/icons/aok_logo");
        QPixmap rounded(QSize(36, 36) * dpr);
        rounded.setDevicePixelRatio(dpr);
        rounded.fill(Qt::transparent);
        QPainter p(&rounded);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform, true);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, 36, 36), 9, 9);
        p.setClipPath(clip);
        p.drawPixmap(QRectF(0, 0, 36, 36), source, QRectF(source.rect()));
        p.end();
        logo->setPixmap(rounded);
    }
    brand->addWidget(logo);
    brand->addWidget(sideLabel("AokChain", t.sideStrong, 18, QFont::DemiBold));
    brand->addStretch();
    layout->addLayout(brand);
    layout->addSpacing(28);

    navLayout = new QVBoxLayout();
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(4);
    layout->addLayout(navLayout);
    layout->addStretch(1);

    // staking
    stakingCard = new QWidget();
    stakingCard->setObjectName("sideCard");
    stakingCard->setAttribute(Qt::WA_StyledBackground, true);
    stakingCard->setStyleSheet(QString("QWidget#sideCard { background-color: %1; border-radius: 12px; }").arg(t.sideCard.name()));
    QVBoxLayout* stakingLayout = new QVBoxLayout(stakingCard);
    stakingLayout->setContentsMargins(14, 14, 14, 14);
    stakingLayout->setSpacing(8);
    QHBoxLayout* stakingRow = new QHBoxLayout();
    stakingRow->addWidget(sideLabel(tr("Staking"), t.sideStrong, 14, QFont::Medium));
    stakingRow->addStretch();
    stakingSwitch = new ToggleSwitch();
    stakingSwitch->setAccessibleName(tr("Staking"));
    stakingRow->addWidget(stakingSwitch);
    stakingLayout->addLayout(stakingRow);
    stakingDetail = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    stakingDetail->setWordWrap(true);
    stakingLayout->addWidget(stakingDetail);
    layout->addWidget(stakingCard);
    layout->addSpacing(12);
    connect(stakingSwitch, SIGNAL(clicked()), this, SIGNAL(stakingToggled()));

    // synchronization
    syncBox = new QWidget();
    syncBox->setCursor(Qt::PointingHandCursor);
    syncBox->installEventFilter(this);
    QVBoxLayout* syncLayout = new QVBoxLayout(syncBox);
    syncLayout->setContentsMargins(8, 0, 8, 0);
    syncLayout->setSpacing(8);
    QHBoxLayout* syncRow = new QHBoxLayout();
    syncStatus = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    syncRemaining = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    syncRow->addWidget(syncStatus);
    syncRow->addStretch();
    syncRow->addWidget(syncRemaining);
    syncLayout->addLayout(syncRow);
    syncProgress = new SideProgress();
    syncLayout->addWidget(syncProgress);
    layout->addWidget(syncBox);
    layout->addSpacing(8);

    networkLabel = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    networkLabel->setContentsMargins(8, 0, 8, 0);
    networkLabel->setCursor(Qt::PointingHandCursor);
    networkLabel->installEventFilter(this);
    layout->addWidget(networkLabel);

    walletLabel = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    walletLabel->setContentsMargins(8, 4, 8, 0);
    walletLabel->hide();
    layout->addWidget(walletLabel);

    // warnings of the node, such as the notice of a pre-release build: small, at the very bottom
    noticeLabel = sideLabel(QString(), t.sideText, 11, QFont::Normal);
    noticeLabel->setContentsMargins(8, 6, 8, 0);
    noticeLabel->setWordWrap(true);
    noticeLabel->hide();
    layout->addWidget(noticeLabel);
}

void Sidebar::addPage(QAction* action, const QString& iconName)
{
    navLayout->addWidget(new SideNavButton(action, iconName, this));
}

void Sidebar::setStaking(bool on, const QString& detail, const QString& toolTip)
{
    stakingSwitch->setChecked(on);
    stakingDetail->setText(detail);
    stakingCard->setToolTip(toolTip);
}

void Sidebar::setStakingVisible(bool visible)
{
    stakingCard->setVisible(visible);
}

void Sidebar::setSyncState(const QString& status, const QString& remaining, double progress, bool showProgress, const QString& toolTip)
{
    syncStatus->setText(status);
    syncRemaining->setText(remaining);
    syncProgress->setValue(showProgress ? progress : 1.0);
    syncBox->setToolTip(toolTip);
}

void Sidebar::setNetworkState(const QString& text, const QString& toolTip)
{
    networkLabel->setText(text);
    networkLabel->setToolTip(toolTip);
}

void Sidebar::setWalletState(const QString& text, const QString& toolTip)
{
    walletLabel->setText(text);
    walletLabel->setToolTip(toolTip);
    walletLabel->setVisible(!text.isEmpty());
}

void Sidebar::setNotice(const QString& text, const QString& toolTip, bool warning)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    noticeLabel->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg((warning ? QColor("#FFD58A") : t.sideText).name()));
    // at most two short lines; the tool tip has the whole text
    const QFontMetrics fm(noticeLabel->font());
    const int width = 232 - 32 - 16;
    QString first = text, second;
    if (fm.horizontalAdvance(text) > width) {
        int cut = text.size();
        while (cut > 0 && fm.horizontalAdvance(text.left(cut)) > width)
            --cut;
        const int space = text.lastIndexOf(' ', cut);
        if (space > cut / 2)
            cut = space;
        first = text.left(cut).trimmed();
        second = fm.elidedText(text.mid(cut).trimmed(), Qt::ElideRight, width);
    }
    noticeLabel->setText(second.isEmpty() ? first : first + "\n" + second);
    noticeLabel->setToolTip(toolTip);
    noticeLabel->setVisible(!text.isEmpty());
}

void Sidebar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), GUIUtil::theme().sideBg);
}

bool Sidebar::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (object == syncBox)
            Q_EMIT syncClicked();
        else if (object == networkLabel)
            Q_EMIT networkClicked();
    }
    return QWidget::eventFilter(object, event);
}
