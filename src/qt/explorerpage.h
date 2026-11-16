// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_EXPLORERPAGE_H
#define AOKCHAIN_QT_EXPLORERPAGE_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QLabel>
#include <QWidget>

#include <string>
#include <vector>

class AmountLabel;
class StatsStrip;
class ClientModel;
class PageActivity;
class PlatformStyle;
class UniValue;
class WalletModel;

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;
QT_END_NAMESPACE

namespace NodeRPC
{
    /** Call an RPC handler of this node, as the console does. Returns false when it reports an error.
     *  The wallet calls go to the first wallet; see callWallet(). */
    bool call(const std::string& method, const std::vector<std::string>& args, UniValue& result, std::string *error = nullptr);
    /** The same, for the wallet `walletName` (its /wallet/<name> endpoint) */
    bool callWallet(const std::string& walletName, const std::string& method, const std::vector<std::string>& args, UniValue& result, std::string *error = nullptr);
}

/**
 * Runs the look-ups of the explorer on its own thread. It only reads: it calls
 * the RPC handlers of this node (getrawtransaction, getblock, getrawmempool, ...)
 * and hands the result back as JSON text.
 */
class ExplorerWorker : public QObject
{
    Q_OBJECT

public:
    /** The wallet whose transactions the look-ups mark as "mine" (the wallet of the page).
     *  Only touched on the thread of the worker. */
    std::string walletName;
    bool walletKnown = false;

public Q_SLOTS:
    /** Look up a transaction id, an address, a block height or a block hash. */
    void lookup(const QString& query);
    /** Read the chain status and the memory pool. */
    void refresh();
    /** Read the chain status only: height, difficulty, network stake weight, peers, size of the memory pool. */
    void chainStatus();

Q_SIGNALS:
    void lookupDone(const QString& query, const QString& json);
    void refreshDone(const QString& json);
    void statusDone(const QString& json);
};

/** Explorer page: transactions, addresses, blocks and the memory pool as this node sees them. */
class ExplorerPage : public QWidget
{
    Q_OBJECT

public:
    explicit ExplorerPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~ExplorerPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    /** Fill the cells of a chain status strip (height, difficulty, stake weight, peers and, when it has
        a fifth cell, the transactions waiting in the memory pool) from what the worker read. */
    static void fillChainStats(StatsStrip *strip, const QString& json);
    /** Note shown next to the page title */
    QWidget *headerWidget() const;
    /** Where the page reports what it is reading */
    void setActivity(PageActivity *activity);

    /** What the page shows, for going back and forward: the query, empty before the first look-up */
    QString navState() const { return pendingQuery; }
    /** Show a query again (going back or forward), where the result was scrolled to; empty: the first view */
    void restoreNavState(const QString& state);
    /** A query in a few words ("block 3,035,316", "Kj6i…qVgR") */
    static QString describeQuery(const QString& query);

public Q_SLOTS:
    /** Put the query into the search field and look it up. */
    void lookup(const QString& query);
    void focusSearch();
    /** Read the result and the memory pool again */
    void refreshPage();

Q_SIGNALS:
    void requestLookup(const QString& query);
    void requestRefresh();
    /** The user wants to send to an address shown in the explorer */
    void sendToAddress(const QString& address);
    /** Another query is shown (or the first view again) */
    void navStateChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    void searchClicked();
    void linkActivated(const QString& link);
    void lookupDone(const QString& query, const QString& json);
    void refreshDone(const QString& json);
    void refreshNow();
    void copyShownId();
    void showRawHex();

private:
    ClientModel *clientModel;
    WalletModel *walletModel;
    QPointer<PageActivity> activity;
    QThread thread;
    ExplorerWorker *worker;
    QTimer *timer;
    bool refreshPending;
    QString pendingQuery;
    /** The query whose result is on the page, and where the results of earlier queries were scrolled to */
    QString shownQuery;
    QHash<QString, int> scrollPositions;
    /** Where to scroll the next result to; -1: the top */
    int restoreScroll;
    /** Where the shown result goes once its rows are laid out (the scroll range grows after the result is built); -1: nowhere */
    int pendingScroll;
    QString shownId;
    QString shownHex;

    QPointer<QLabel> headerNote;
    QLineEdit *searchEdit;
    QPushButton *searchButton;
    StatsStrip *chainStats;
    QLabel *resultTitle;
    QLabel *resultBadge;
    QScrollArea *resultScroll;
    QVBoxLayout *resultLayout;
    QWidget *resultButtons;
    QPushButton *hexButton;
    AmountLabel *poolCount;
    AmountLabel *poolSize;
    AmountLabel *poolMinFee;
    QVBoxLayout *poolLayout;
    QLabel *poolFooter;

    void clearLayout(QVBoxLayout *layout);
    /** Look a query up; scroll: where to put the result (-1: the top) */
    void startLookup(const QString& query, int scroll);
    /** The first view: what can be looked up */
    void showIntro();
    /** Keep where the shown result is scrolled to, before another one replaces it */
    void rememberScroll();
    void setBadge(const QString& text, bool ok);
    void showMessage(const QString& title, const QString& text, bool notice);
    void showTransaction(const UniValue& result);
    void showBlock(const UniValue& result);
    void showAddress(const UniValue& result);
    QWidget *keyValueRow(const QString& key, QWidget *value);
    QLabel *monoLabel(const QString& text, int px = 13, bool bold = false);
    /** An amount in the typography of all pages (smaller fraction), optionally with a unit */
    QWidget *amountValue(const QString& amount, const QString& unit = QString(), int px = 15);
    QLabel *linkLabel(const QString& text, const QString& target, const QString& toolTip = QString(), int px = 12);
    QWidget *transferRow(const UniValue& entry);
};

#endif // AOKCHAIN_QT_EXPLORERPAGE_H
