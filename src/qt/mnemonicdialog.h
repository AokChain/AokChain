// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_MNEMONICDIALOG_H
#define AOKCHAIN_QT_MNEMONICDIALOG_H

#include "key.h"
#include "pubkey.h"
#include "wallet/mnemonicrestore.h"

#include <QDialog>
#include <QThread>

#include <atomic>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class WalletModel;

/** What takes long runs here, off the GUI thread: the search for the addresses and the rescan. */
class MnemonicWorker : public QObject
{
    Q_OBJECT

public:
    CExtKey master;
    bool haveKnown = false;
    CKeyID known;
    mnemonicrestore::Plan plan;
    std::atomic<bool> cancel{false};
    CWallet *wallet = nullptr;

public Q_SLOTS:
    void search();
    void rescan();

Q_SIGNALS:
    void searchDone(bool ok, const QString &error);
    void rescanProgress(int percent);
    void rescanDone(bool ok, const QString &error);
};

/**
 * Restore the keys of another wallet from its recovery words (BIP39): enter
 * the words, see the addresses that hold coins, import the keys, rescan.
 * The words are not stored or logged; the fields are wiped as soon as the
 * master key is made.
 */
class MnemonicDialog : public QDialog
{
    Q_OBJECT

public:
    MnemonicDialog(WalletModel *model, QWidget *parent);
    ~MnemonicDialog();

public Q_SLOTS:
    void reject() override;

Q_SIGNALS:
    void backupRequested();
    void startSearch();
    void startRescan();

private Q_SLOTS:
    void validateWords();
    void findAddresses();
    void searchDone(bool ok, const QString &error);
    void importKeys();
    void rescanProgress(int percent);
    void rescanDone(bool ok, const QString &error);

private:
    enum Page { Enter, Working, Result, Rescan, Done, Unavailable };
    QWidget *enterPage();
    QWidget *workingPage();
    QWidget *resultPage();
    QWidget *rescanPage();
    QWidget *donePage();
    void wipeInputs();
    QString errorText(const QString &error) const;

    WalletModel *model;
    QThread thread;
    MnemonicWorker *worker;
    bool busy;
    bool closeWhenIdle;
    int imported;

    QStackedWidget *stack;
    QPlainTextEdit *words;
    QLabel *wordsStatus;
    QLineEdit *passphrase;
    QLineEdit *address;
    QLabel *enterError;
    QPushButton *findButton;
    QTimer *validateTimer;
    QLabel *workingText;
    QLabel *resultSummary;
    QTableWidget *resultTable;
    QLabel *resultNote;
    QProgressBar *rescanBar;
    QLabel *rescanText;
    QPushButton *rescanStop;
    QLabel *doneText;
    QLabel *doneNote;
    QPushButton *backupButton;
};

#endif // AOKCHAIN_QT_MNEMONICDIALOG_H
