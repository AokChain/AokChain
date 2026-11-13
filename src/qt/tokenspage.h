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

/**
 * Tokens page: the root tokens of the wallet as cards, in more detail than on
 * the overview.
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

protected:
    void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    void scheduleRebuild();
    void rebuildHeld();

private:
    WalletModel *model;
    QTimer *rebuildTimer;
    QList<QWidget*> cards;
    int columns;

    QLabel *heldCount;
    QGridLayout *grid;
    QLabel *heldEmpty;

    HeldTokenIndex *heldTokens;
    QString cardSignature;
    void showTokensBelow(const QString& root);
    void reflow();
};

#endif // AOKCHAIN_QT_TOKENSPAGE_H
