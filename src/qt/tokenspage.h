// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_TOKENSPAGE_H
#define AOKCHAIN_QT_TOKENSPAGE_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>
#include <QWidget>

class HeldTokenIndex;
class PlatformStyle;
class WalletModel;

QT_BEGIN_NAMESPACE
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;
QT_END_NAMESPACE

/** Reads the tokens that exist on the chain (RPC listtokens) off the GUI thread, a page at a time. */
class TokensWorker : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void list(const QString& filter, int start, int count, int generation);

Q_SIGNALS:
    void listed(const QString& json, int start, int generation);
};

/**
 * Tokens page: the tokens of the wallet as cards, in more detail than on the
 * overview, and below them the other tokens that exist on the chain.
 */
class TokensPage : public QWidget
{
    Q_OBJECT

public:
    explicit TokensPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~TokensPage();

    void setModel(WalletModel *model);

Q_SIGNALS:
    /** Go to the token tab of the send page with this token chosen */
    void sendToken(const QString& name);
    void receiveRequested();
    void requestList(const QString& filter, int start, int count, int generation);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:
    void scheduleRebuild();
    void rebuildHeld();
    void searchChanged();
    void loadMore();
    void listed(const QString& json, int start, int generation);

private:
    WalletModel *model;
    QThread thread;
    QTimer *rebuildTimer;
    QTimer *searchTimer;
    QList<QWidget*> cards;
    QSet<QString> heldNames;
    int columns;
    int generation;
    int nextStart;
    bool loading;
    bool loadedOnce;

    QLabel *heldCount;
    QGridLayout *grid;
    QLabel *heldEmpty;
    QLineEdit *search;
    QVBoxLayout *chainLayout;
    QLabel *chainStatus;
    QPushButton *moreButton;

    HeldTokenIndex *heldTokens;
    QString cardSignature;
    void showTokensBelow(const QString& root);
    void reflow();
    void startList(bool reset);
};

#endif // AOKCHAIN_QT_TOKENSPAGE_H
