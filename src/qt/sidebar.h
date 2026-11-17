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
    /** A click asks for the change; the owner sets the state when it has happened (a passphrase
     *  dialog may come first, and may be cancelled). */
    void nextCheckState() override {}
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

/** The wallet that is shown, at the top of the side bar: a click opens the wallet menu. */
class WalletSwitcher : public QAbstractButton
{
    Q_OBJECT

public:
    explicit WalletSwitcher(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(208, 56); }
    void setWallet(const QString& name, const QString& state);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    QString name;
    QString state;
    bool hovered;
    /** The focus came by the keyboard: only then is it drawn (not at the start of the program) */
    bool keyFocus;
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
    /** A line between two groups of navigation items */
    void addSeparator();

    void setStaking(bool on, const QString& detail, const QString& toolTip);
    void setSyncState(const QString& status, const QString& remaining, double progress, bool showProgress, const QString& toolTip);
    void setNetworkState(const QString& text, const QString& toolTip);
    void setWalletState(const QString& text, const QString& toolTip);
    /** The name of the wallet that is shown; count: how many wallets are open */
    void setWalletName(const QString& name, int count);
    /** Where the wallet menu opens */
    QPoint walletMenuPosition() const;
    /** The menu is as wide as the box that it opens from */
    int walletMenuWidth() const;
    void setStakingVisible(bool visible);
    /** A warning of the node, shown small below the peers line */
    void setNotice(const QString& text, const QString& toolTip, bool warning);

Q_SIGNALS:
    /** The wallet at the top was clicked: the wallet menu */
    void walletClicked();
    void stakingToggled();
    void syncClicked();
    void networkClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QVBoxLayout* navLayout;
    WalletSwitcher* walletSwitcher;
    QString walletName;
    QString walletStateText;
    int walletCount;
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
