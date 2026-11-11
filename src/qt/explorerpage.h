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

Q_SIGNALS:
    void lookupDone(const QString& query, const QString& json);
    void refreshDone(const QString& json);
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
    QLabel *statHeight;
    QLabel *statDifficulty;
    QLabel *statWeight;
    QLabel *statPeers;
    QLabel *resultTitle;
    QLabel *resultBadge;
    QVBoxLayout *resultLayout;
    QWidget *resultButtons;
    QPushButton *hexButton;
    QLabel *poolCount;
    QLabel *poolSize;
    QLabel *poolMinFee;
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
    QLabel *linkLabel(const QString& text, const QString& target, const QString& toolTip = QString(), int px = 12);
    QWidget *transferRow(const UniValue& entry);
};

#endif // AOKCHAIN_QT_EXPLORERPAGE_H
