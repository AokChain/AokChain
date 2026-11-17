// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_WALLETCONTROLLER_H
#define AOKCHAIN_QT_WALLETCONTROLLER_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>

#include <functional>

class CWallet;
class OptionsModel;
class PlatformStyle;
class WalletModel;

QT_BEGIN_NAMESPACE
class QProgressDialog;
class QTimer;
class QWidget;
QT_END_NAMESPACE

/**
 * The wallets of the window. Owns a WalletModel for every wallet the node has loaded, knows which
 * one is shown, and opens, creates, restores and closes wallets. The node work (loading a wallet
 * file, its rescan, closing it) runs on a thread of its own, one operation at a time, while a
 * small window says what is going on.
 *
 * The first wallet (wallet.dat, or the first -wallet option) is never closed.
 *
 * The wallets that were open are opened again at the next start, unless -wallet is given in the
 * configuration or on the command line: then exactly those wallets are loaded and nothing is
 * remembered.
 */
class WalletController : public QObject
{
    Q_OBJECT

public:
    WalletController(const PlatformStyle *platformStyle, OptionsModel *optionsModel, QObject *parent = nullptr);
    ~WalletController();

    /** The controller of the application (nullptr before it is made and after it is gone) */
    static WalletController *instance();

    /** Whether -wallet was given in the configuration or on the command line. Set before the node starts. */
    static void setExplicitWallets(bool explicitWallets);
    /** Whether the open wallets are remembered for the next start (no -wallet given) */
    static bool remembersWallets();

    /** The name the window shows for a wallet name of the node: "" is the main wallet */
    static QString displayName(const QString &walletName);
    QString displayName(const WalletModel *model) const;
    /** The file or folder of a wallet, for the settings page */
    static QString walletPath(const QString &walletName);

    /** The models, the first wallet first */
    QList<WalletModel*> models() const { return walletModels; }
    WalletModel *current() const;
    WalletModel *modelFor(const CWallet *wallet) const;
    WalletModel *modelFor(const QString &walletName) const;
    /** Whether the wallet of this model is the first wallet, which stays open */
    static bool isFirstWallet(const WalletModel *model);
    bool isBusy() const { return operation != nullptr; }

    /** Make a model for every wallet that the node loaded at the start */
    void addStartupWallets();
    /** Open the wallets that were open when the program ended last (see remembersWallets()) */
    void openRememberedWallets();
    /** The wallets in the wallet directory that are not open */
    QStringList closedWallets() const;
    /** Whether a new wallet could have this name: plain, not in use. Returns the reason if not. */
    static bool checkNewName(const QString &name, QString *reason);

    /** The wallet that the window shows */
    void setCurrent(WalletModel *model);

    /** Open the wallet `name` of the wallet directory, then show it */
    void openWallet(const QString &name);
    /** Create the wallet `name`, then show it. encrypt: then ask for its passphrase
     *  (encryptionWanted), with the dialog that encrypts every wallet */
    void createWallet(const QString &name, bool encrypt);
    /** Copy a backup file into the wallet directory as the wallet `name` and open it */
    void restoreWallet(const QString &name, const QString &backupFile);
    /** Close a wallet (not the first one) */
    void closeWallet(WalletModel *model);
    /** A wallet that is not the first one was encrypted: close it and open it again, so that its
     *  database starts without the logs of before (what a restart does for the first wallet) */
    void reopenWallet(WalletModel *model);

    /** The window that the progress window and the messages belong to */
    void setParentWindow(QWidget *window) { parentWindow = window; }

Q_SIGNALS:
    /** A model for a wallet that was loaded; the window gives it a view */
    void walletAdded(WalletModel *model);
    /** The model is about to go: the window removes its view. The model is deleted afterwards. */
    void walletRemoved(WalletModel *model);
    /** The wallet that the window shows */
    void currentChanged(WalletModel *model);
    /** A wallet was opened, created, restored or closed, or the name of the current one changed */
    void walletsChanged();
    /** An operation is over. ok: it did what was asked. */
    void operationFinished(bool ok);
    /** A wallet that was just created and is to be encrypted */
    void encryptionWanted(WalletModel *model);
    /** A wallet that was encrypted and opened again: time for a backup */
    void backupWanted(WalletModel *model);
    /** A message for the user (style as in CClientUIInterface) */
    void message(const QString &title, const QString &text, unsigned int style);

private:
    struct Operation;

    const PlatformStyle *platformStyle;
    OptionsModel *optionsModel;
    QList<WalletModel*> walletModels;
    QPointer<WalletModel> currentModel;
    QThread thread;
    QObject *worker;
    QWidget *parentWindow;
    QPointer<QProgressDialog> progress;
    Operation *operation;
    QStringList pendingRemembered;
    bool openingRemembered;

    WalletModel *addModel(CWallet *wallet);
    void setCurrent(WalletModel *model, bool remember);
    void removeModel(WalletModel *model);
    /** Run `work` on the wallet thread, with the progress window showing `what` */
    void start(const QString &what, bool quiet, std::function<void()> work);
    void done(bool ok);
    void openNextRemembered();
    void rememberOpenWallets();
    void rememberCurrent();
    /** Follow the wallets of the node: a command may have loaded or unloaded one */
    void syncWithNode();

    QTimer *syncTimer;
};

#endif // AOKCHAIN_QT_WALLETCONTROLLER_H
