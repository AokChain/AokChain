// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_SIGNHUBPAGE_H
#define AOKCHAIN_QT_SIGNHUBPAGE_H

#include "multisigmodel.h"

#include <QWidget>

class AokChainAmountField;
class AmountLabel;
class PlatformStyle;
class WalletModel;

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
QT_END_NAMESPACE

/**
 * The "Sign" page of the wallet window: what a signature is good for, as tasks.
 *
 *  - prove an address (sign a text), check a signature that was received
 *  - multisig: make a multisig wallet with others, add one that others made,
 *    send from it with a signing request that goes round the participants
 *
 * The page is a stack of sub pages with a way back to the first one. The
 * work is done by MultisigModel.
 */
class SignHubPage : public QWidget
{
    Q_OBJECT

public:
    explicit SignHubPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);

    void setModel(WalletModel *walletModel);

public Q_SLOTS:
    /** The first page: the tasks and the multisig wallets */
    void showHub();
    void showProve(const QString& address = QString());
    void showVerify(const QString& address = QString());
    void showCreate();
    void showJoin();
    void showWallet(const QString& address);
    void showRequest(const QString& id);
    /** What multisig is, in plain words */
    void showHelp();

Q_SIGNALS:
    /** Look a transaction up in the explorer */
    void explorerRequested(const QString& query);
    void message(const QString& title, const QString& message, unsigned int style);

private Q_SLOTS:
    void modelChanged();
    void walletAdded(const QString& address, const QString& error);
    // prove and verify
    void chooseProveAddress();
    void signText();
    void verifyPasted();
    void verifyFields();
    // hub
    void copyMyKey();
    void openReceivedRequest();
    // create
    void ruleChanged();
    void createStep(int step);
    void participantsChanged();
    void finishCreate();
    // join
    void joinTextChanged();
    void finishJoin();
    // send
    void updateSendPreview();
    void makeRequest();
    // request
    void exportRequestFile();
    void copyRequestCode();
    void showRequestQr();
    void mergeSignatures();
    void signCurrentRequest();
    void broadcastCurrentRequest();
    void cancelCurrentRequest();

private:
    const PlatformStyle *platformStyle;
    WalletModel *walletModel;
    MultisigModel *model;
    QStackedWidget *stack;

    // hub
    QVBoxLayout *walletList;
    void fillHub();

    // prove
    QLineEdit *proveAddress;
    QPlainTextEdit *proveText;
    QPlainTextEdit *proveResult;
    QLabel *proveStatus;
    QPushButton *proveCopy;

    // verify
    QPlainTextEdit *verifyPaste;
    QLineEdit *verifyAddress;
    QPlainTextEdit *verifyText;
    QLineEdit *verifySignature;
    QLabel *verifyResult;
    QLabel *verifyReason;
    QWidget *verifyBanner;
    void showVerifyResult(bool checked, bool valid, const QString& reason);

    // create
    QStackedWidget *createStack;
    QList<QLabel*> stepLabels;
    QLineEdit *createName;
    QButtonGroup *presets;
    QSpinBox *createN;
    QSpinBox *createM;
    QLabel *ruleEffects;
    QLabel *participantsTitle;
    QLabel *participantsRule;
    QVBoxLayout *participantRows;
    QLineEdit *myNameEdit;
    QLabel *myKeyLabel;
    QString myPubkey;
    QList<QLineEdit*> otherNames;
    QList<QLineEdit*> otherKeys;
    QList<QLabel*> otherStatus;
    QPushButton *participantsNext;
    QLabel *stepHelpNeeded;
    QVBoxLayout *confirmBody;
    MultisigWallet draft;
    bool draftCreated;
    void buildParticipantRows();
    bool collectDraft(QString& error);
    void fillConfirm();

    // join
    QPlainTextEdit *joinText;
    QLabel *joinPreview;
    QLineEdit *joinName;
    QCheckBox *joinRescan;
    QPushButton *joinButton;
    MultisigWallet joinWallet;
    QString pendingRequestCode;     //!< a received request that waits for its wallet to be added

    // send
    QString currentWallet;
    QLabel *sendTitle;
    QLabel *sendRule;
    AmountLabel *sendBalance;
    QLineEdit *sendTo;
    AokChainAmountField *sendAmount;
    QPushButton *sendAllButton;
    QLineEdit *sendMemo;
    QButtonGroup *sendFee;
    QLabel *sendFeeLine;
    QLabel *sendError;
    QPushButton *sendButton;
    QVBoxLayout *sendSide;
    QTimer *previewTimer;
    void fillWalletPage();

    // request
    QString currentRequest;
    QVBoxLayout *requestBody;
    void fillRequestPage();

    QWidget *buildHub();
    QWidget *buildProve();
    QWidget *buildVerify();
    QWidget *buildCreate();
    QWidget *buildJoin();
    QWidget *buildWallet();
    QWidget *buildRequest();
    /** Title row of a sub page with the way back to the first page */
    QLayout *subHeader(const QString& title, QLabel **titleLabel = nullptr, QWidget *right = nullptr);
    bool askCode(const QString& title, const QString& hint, QString& text);
    void saveCode(const QString& title, const QString& fileName, const QString& code);
    void shareWallet(const MultisigWallet& wallet, bool toFile);
    void addWalletFromRequest(const MultisigRequest& request);
};

#endif // AOKCHAIN_QT_SIGNHUBPAGE_H
