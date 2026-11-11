// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_SIDEBAR_H
#define AOKCHAIN_QT_SIDEBAR_H

#include <QAbstractButton>
#include <QWidget>

class QAction;
class QLabel;
class QVBoxLayout;

/** An on/off switch. */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(44, 26); }

protected:
    void paintEvent(QPaintEvent* event) override;
};

/** A navigation item of the side bar, bound to a (checkable) action. */
class SideNavButton : public QAbstractButton
{
    Q_OBJECT

public:
    SideNavButton(QAction* action, const QString& iconName, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(200, 44); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

private Q_SLOTS:
    void actionChanged();

private:
    QAction* action;
    QString iconName;
    bool hovered;
};

/** The thin progress bar of the side bar. */
class SideProgress : public QWidget
{
    Q_OBJECT

public:
    explicit SideProgress(QWidget* parent = nullptr);
    void setValue(double fraction);
    QSize sizeHint() const override { return QSize(100, 4); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double value;
};

/**
 * Left side bar of the main window: logo, page navigation, the staking switch
 * and the synchronization status that used to be in the status bar.
 */
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    /** Add a navigation item for a page action. */
    void addPage(QAction* action, const QString& iconName);

    void setStaking(bool on, const QString& detail, const QString& toolTip);
    void setSyncState(const QString& status, const QString& remaining, double progress, bool showProgress, const QString& toolTip);
    void setNetworkState(const QString& text, const QString& toolTip);
    void setWalletState(const QString& text, const QString& toolTip);
    void setStakingVisible(bool visible);
    /** A warning of the node, shown small below the peers line */
    void setNotice(const QString& text, const QString& toolTip, bool warning);

Q_SIGNALS:
    void stakingToggled();
    void syncClicked();
    void networkClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QVBoxLayout* navLayout;
    QWidget* stakingCard;
    ToggleSwitch* stakingSwitch;
    QLabel* stakingDetail;
    QWidget* syncBox;
    QLabel* syncStatus;
    QLabel* syncRemaining;
    SideProgress* syncProgress;
    QLabel* networkLabel;
    QLabel* walletLabel;
    QLabel* noticeLabel;
};

#endif // AOKCHAIN_QT_SIDEBAR_H
