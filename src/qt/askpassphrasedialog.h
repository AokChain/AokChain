// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_ASKPASSPHRASEDIALOG_H
#define AOKCHAIN_QT_ASKPASSPHRASEDIALOG_H

#include <QDialog>

class WalletModel;
class QCheckBox;
class QPushButton;
class QLabel;

namespace Ui {
    class AskPassphraseDialog;
}

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class AskPassphraseDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        Encrypt,    /**< Ask passphrase twice and encrypt */
        Unlock,     /**< Ask passphrase and unlock */
        UnlockStaking, /**< Ask passphrase and unlock, by default for staking only */
        ChangePass, /**< Ask old passphrase + new passphrase twice */
        Decrypt     /**< Ask passphrase and decrypt wallet */
    };

    explicit AskPassphraseDialog(Mode mode, QWidget *parent);
    ~AskPassphraseDialog();

    void accept() override;
    void reject() override;

    void setModel(WalletModel *model);
    /** Encrypt mode as the second step of the first start: wizard header, "Quit" instead of "Cancel" */
    void setOnboarding();

private:
    Ui::AskPassphraseDialog *ui;
    Mode mode;
    WalletModel *model;
    bool fCapsLock;
    bool onboarding;
    QLabel *strengthLabel;
    QLabel *noteLabel;
    QCheckBox *understood;
    QCheckBox *stakingOnly;

private Q_SLOTS:
    void textChanged();
    void secureClearPassFields();

protected:
    bool event(QEvent *event);
    bool eventFilter(QObject *object, QEvent *event);
};

/** Third step: the wallet was encrypted, now keep a copy of the wallet file. */
class BackupPromptDialog : public QDialog
{
    Q_OBJECT

public:
    BackupPromptDialog(WalletModel *model, QWidget *parent);

private Q_SLOTS:
    void save();

private:
    WalletModel *model;
    QLabel *result;
    QPushButton *saveButton;
    QPushButton *laterButton;
};

#endif // AOKCHAIN_QT_ASKPASSPHRASEDIALOG_H
