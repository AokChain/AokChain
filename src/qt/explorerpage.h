// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_EXPLORERPAGE_H
#define AOKCHAIN_QT_EXPLORERPAGE_H

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
class PlatformStyle;
class UniValue;
class WalletModel;

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;
QT_END_NAMESPACE

namespace NodeRPC
{
    /** Call an RPC handler of this node, as the console does. Returns false when it reports an error. */
    bool call(const std::string& method, const std::vector<std::string>& args, UniValue& result, std::string *error = nullptr);
}

/**
 * Runs the look-ups of the explorer on its own thread. It only reads: it calls
 * the RPC handlers of this node (getrawtransaction, getblock, getrawmempool, ...)
 * and hands the result back as JSON text.
 */
class ExplorerWorker : public QObject
{
    Q_OBJECT

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

public Q_SLOTS:
    /** Put the query into the search field and look it up. */
    void lookup(const QString& query);
    void focusSearch();

Q_SIGNALS:
    void requestLookup(const QString& query);
    void requestRefresh();
    /** The user wants to send to an address shown in the explorer */
    void sendToAddress(const QString& address);

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
    QThread thread;
    QTimer *timer;
    bool refreshPending;
    QString pendingQuery;
    QString shownId;
    QString shownHex;

    QPointer<QLabel> headerNote;
    QLineEdit *searchEdit;
    QPushButton *searchButton;
    StatsStrip *chainStats;
    QLabel *resultTitle;
    QLabel *resultBadge;
    QVBoxLayout *resultLayout;
    QWidget *resultButtons;
    QPushButton *hexButton;
    AmountLabel *poolCount;
    AmountLabel *poolSize;
    AmountLabel *poolMinFee;
    QVBoxLayout *poolLayout;
    QLabel *poolFooter;

    void clearLayout(QVBoxLayout *layout);
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
