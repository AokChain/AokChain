// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_WALLETDIALOGS_H
#define AOKCHAIN_QT_WALLETDIALOGS_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

/** Create wallet: a name and whether to encrypt it (the passphrase dialog follows). */
class CreateWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateWalletDialog(QWidget *parent);

    QString walletName() const;
    bool encrypt() const;

private Q_SLOTS:
    void check();

private:
    QLineEdit *nameEdit;
    QLabel *problem;
    QCheckBox *encryptBox;
    QPushButton *createButton;
};

/** Open wallet: the wallets of the wallet folder that are not open, and restoring a backup file. */
class OpenWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenWalletDialog(const QStringList &closedWallets, QWidget *parent);

    /** The wallet chosen to open; empty when a backup is to be restored instead */
    QString chosenWallet() const { return chosen; }
    /** restoreRequested(): the dialog ended with "Restore from a backup file" */
    bool restoreRequested() const { return restore; }

private:
    QListWidget *list;
    QPushButton *openButton;
    QString chosen;
    bool restore = false;
};

/** Restore a wallet from a backup file: the file and the name of the new wallet. */
class RestoreWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RestoreWalletDialog(QWidget *parent);

    QString walletName() const;
    QString backupFile() const;

private Q_SLOTS:
    void chooseFile();
    void check();

private:
    QLineEdit *fileEdit;
    QLineEdit *nameEdit;
    QLabel *problem;
    QPushButton *restoreButton;
};

#endif // AOKCHAIN_QT_WALLETDIALOGS_H
