// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletcontroller.h"

#include "walletmodel.h"

#include "fs.h"
#include "ui_interface.h"
#include "util.h"
#include "wallet/wallet.h"
#include "wallet/walletutil.h"

#include <QApplication>
#include <QProgressDialog>
#include <QRegExp>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

struct WalletController::Operation
{
    QString what;
};

namespace {
WalletController *g_controller = nullptr;
bool g_explicitWallets = false;

/** QSettings keeps the remembered wallets per wallet directory: two data directories of one
 *  network (the settings are per network) do not open each other's wallets. */
QString walletDirKey()
{
    return QString::fromStdString(fs::absolute(GetWalletDir()).string());
}

QStringList rememberedWallets()
{
    return QSettings().value("openWallets").toMap().value(walletDirKey()).toStringList();
}

QString rememberedCurrent()
{
    return QSettings().value("currentWallet").toMap().value(walletDirKey()).toString();
}

void storeInMap(const char *key, const QVariant &value)
{
    QSettings settings;
    QVariantMap map = settings.value(key).toMap();
    map[walletDirKey()] = value;
    settings.setValue(key, map);
}
} // namespace

WalletController::WalletController(const PlatformStyle *_platformStyle, OptionsModel *_optionsModel, QObject *parent) :
    QObject(parent),
    platformStyle(_platformStyle),
    optionsModel(_optionsModel),
    worker(new QObject()),
    parentWindow(nullptr),
    operation(nullptr),
    openingRemembered(false),
    syncTimer(nullptr)
{
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    thread.setObjectName("aok-walletcontrol");
    thread.start();
    g_controller = this;
}

WalletController::~WalletController()
{
    // an operation that still runs (a rescan stops at the shutdown) is waited for
    thread.quit();
    thread.wait();
    if (g_controller == this)
        g_controller = nullptr;
    delete operation;
    operation = nullptr;
    // the models are children of the controller; the window has removed their views
}

WalletController *WalletController::instance()
{
    return g_controller;
}

void WalletController::setExplicitWallets(bool explicitWallets)
{
    g_explicitWallets = explicitWallets;
}

bool WalletController::remembersWallets()
{
    return !g_explicitWallets;
}

QString WalletController::displayName(const QString &walletName)
{
    return walletName.isEmpty() ? tr("Main wallet") : walletName;
}

QString WalletController::displayName(const WalletModel *model) const
{
    return model && model->getWallet() ? displayName(QString::fromStdString(model->getWallet()->GetName())) : QString();
}

QString WalletController::walletPath(const QString &walletName)
{
    const fs::path path = fs::absolute(walletName.toStdString(), GetWalletDir());
    return QString::fromStdString((fs::is_directory(path) ? path / "wallet.dat" : path).string());
}

WalletModel *WalletController::current() const
{
    return currentModel.data();
}

WalletModel *WalletController::modelFor(const CWallet *wallet) const
{
    for (WalletModel *model : walletModels)
        if (model->getWallet() == wallet)
            return model;
    return nullptr;
}

WalletModel *WalletController::modelFor(const QString &walletName) const
{
    for (WalletModel *model : walletModels)
        if (QString::fromStdString(model->getWallet()->GetName()) == walletName)
            return model;
    return nullptr;
}

bool WalletController::isFirstWallet(const WalletModel *model)
{
    return model && IsDefaultWallet(model->getWallet());
}

WalletModel *WalletController::addModel(CWallet *wallet)
{
    WalletModel *model = new WalletModel(platformStyle, wallet, optionsModel, this);
    walletModels.append(model);
    Q_EMIT walletAdded(model);
    Q_EMIT walletsChanged();
    return model;
}

void WalletController::removeModel(WalletModel *model)
{
    if (!model || !walletModels.contains(model))
        return;
    if (currentModel == model) {
        WalletModel *other = nullptr;
        for (WalletModel *m : walletModels)
            if (m != model) { other = m; break; }
        setCurrent(other);
    }
    Q_EMIT walletRemoved(model);
    walletModels.removeAll(model);
    model->deleteLater();
    Q_EMIT walletsChanged();
}

void WalletController::addStartupWallets()
{
    for (CWallet *wallet : GetWallets())
        if (!modelFor(wallet))
            addModel(wallet);
    // the first wallet for now; the one shown last is chosen again once the remembered wallets are
    // open (openRememberedWallets), so it is not overwritten here
    if (!walletModels.isEmpty())
        setCurrent(walletModels.first(), false);
    if (!syncTimer) {
        syncTimer = new QTimer(this);
        syncTimer->setInterval(2000);
        connect(syncTimer, &QTimer::timeout, this, &WalletController::syncWithNode);
        syncTimer->start();
    }
}

void WalletController::syncWithNode()
{
    // The window opens and closes its wallets itself (openWallet() and the others). A command can
    // do the same behind its back: loadwallet, createwallet, restorewallet and unloadwallet, in the
    // console or from another program. The window follows, so that every loaded wallet is shown
    // and none is shown that is gone.
    // Not while an operation of the controller runs (its result adds or removes the model), and
    // not under a dialog: a page must not be taken away below a dialog of its own.
    if (operation || openingRemembered || QApplication::activeModalWidget())
        return;
    const std::vector<CWallet*> loaded = GetWallets();
    bool changed = false;
    for (WalletModel *model : QList<WalletModel*>(walletModels)) {
        if (std::find(loaded.begin(), loaded.end(), model->getWallet()) == loaded.end()) {
            removeModel(model);
            changed = true;
        }
    }
    for (CWallet *wallet : loaded) {
        if (!modelFor(wallet)) {
            addModel(wallet);
            changed = true;
        }
    }
    if (changed)
        rememberOpenWallets();
}

void WalletController::setCurrent(WalletModel *model)
{
    setCurrent(model, true);
}

void WalletController::setCurrent(WalletModel *model, bool remember)
{
    if (model == currentModel)
        return;
    currentModel = model;
    Q_EMIT currentChanged(model);
    Q_EMIT walletsChanged();
    if (remember)
        rememberCurrent();
}

QStringList WalletController::closedWallets() const
{
    QStringList names;
    try {
        for (const fs::path &path : ListWalletDir()) {
            const std::string name = path.string();
            if (!GetWallet(name))
                names << QString::fromStdString(name);
        }
    } catch (const fs::filesystem_error &e) {
        LogPrintf("GUI: cannot list the wallet directory: %s\n", e.what());
    }
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return names;
}

bool WalletController::checkNewName(const QString &name, QString *reason)
{
    const QString trimmed = name.trimmed();
    QString why;
    if (trimmed.isEmpty())
        why = tr("Give the wallet a name.");
    else if (trimmed.size() > 40)
        why = tr("The name is too long: at most 40 characters.");
    else if (!QRegExp("[A-Za-z0-9][A-Za-z0-9 _.-]*").exactMatch(trimmed) || trimmed.endsWith('.'))
        why = tr("Use letters, digits, spaces, \"-\", \"_\" and \".\", starting with a letter or a digit.");
    else if (trimmed.compare("wallet.dat", Qt::CaseInsensitive) == 0 || trimmed.compare("database", Qt::CaseInsensitive) == 0)
        why = tr("This name is used by the wallet files themselves.");
    else {
        const fs::path path = fs::absolute(trimmed.toStdString(), GetWalletDir());
        if (fs::exists(path) || GetWallet(trimmed.toStdString()))
            why = tr("There is a wallet or a file of this name in the wallet folder already.");
        else {
            // names that differ in case are one folder on Windows and macOS
            try {
                for (const fs::path &existing : ListWalletDir())
                    if (QString::fromStdString(existing.string()).compare(trimmed, Qt::CaseInsensitive) == 0)
                        why = tr("There is a wallet or a file of this name in the wallet folder already.");
            } catch (const fs::filesystem_error &) {
            }
        }
    }
    if (reason)
        *reason = why;
    return why.isEmpty();
}

void WalletController::start(const QString &what, bool quiet, std::function<void()> work)
{
    operation = new Operation{what};
    if (!quiet) {
        progress = new QProgressDialog(what, QString(), 0, 0, parentWindow);
        progress->setWindowTitle(tr("Wallets"));
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setCancelButton(nullptr);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
    }
    Q_EMIT walletsChanged();
    QMetaObject::invokeMethod(worker, std::move(work), Qt::QueuedConnection);
}

void WalletController::done(bool ok)
{
    delete operation;
    operation = nullptr;
    if (progress) {
        progress->close();
        progress->deleteLater();
    }
    Q_EMIT operationFinished(ok);
    Q_EMIT walletsChanged();
}

void WalletController::openWallet(const QString &name)
{
    if (operation || modelFor(name))
        return;
    const std::string walletName = name.toStdString();
    start(tr("Opening %1...").arg(displayName(name)), false, [this, walletName] {
        WalletLoadResult result;
        std::string error, warning;
        CWallet *wallet = LoadWalletByName(walletName, result, error, warning);
        QMetaObject::invokeMethod(this, [this, wallet, walletName, error, warning] {
            const QString shown = displayName(QString::fromStdString(walletName));
            if (!wallet) {
                Q_EMIT message(tr("Open wallet"), tr("%1 was not opened: %2").arg(shown, QString::fromStdString(error)), CClientUIInterface::MSG_ERROR | CClientUIInterface::MODAL);
                done(false);
                return;
            }
            WalletModel *model = addModel(wallet);
            if (!warning.empty())
                Q_EMIT message(tr("Open wallet"), QString::fromStdString(warning), CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
            rememberOpenWallets();
            setCurrent(model);
            done(true);
        }, Qt::QueuedConnection);
    });
}

void WalletController::createWallet(const QString &name, bool encrypt)
{
    if (operation)
        return;
    const std::string walletName = name.trimmed().toStdString();
    start(tr("Creating %1...").arg(QString::fromStdString(walletName)), false, [this, walletName, encrypt] {
        WalletLoadResult result;
        std::string error, warning;
        CWallet *wallet = CreateWalletByName(walletName, result, error, warning);
        QMetaObject::invokeMethod(this, [this, wallet, walletName, error, warning, encrypt] {
            const QString shown = QString::fromStdString(walletName);
            if (!wallet) {
                Q_EMIT message(tr("Create wallet"), tr("%1 was not created: %2").arg(shown, QString::fromStdString(error)), CClientUIInterface::MSG_ERROR | CClientUIInterface::MODAL);
                done(false);
                return;
            }
            WalletModel *model = addModel(wallet);
            rememberOpenWallets();
            setCurrent(model);
            if (!warning.empty())
                Q_EMIT message(tr("Create wallet"), QString::fromStdString(warning), CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
            done(true);
            if (encrypt)
                Q_EMIT encryptionWanted(model);
        }, Qt::QueuedConnection);
    });
}

void WalletController::restoreWallet(const QString &name, const QString &backupFile)
{
    if (operation)
        return;
    const std::string walletName = name.trimmed().toStdString();
    const std::string file = backupFile.toStdString();
    start(tr("Restoring %1...").arg(QString::fromStdString(walletName)), false, [this, walletName, file] {
        WalletLoadResult result;
        std::string error, warning;
        CWallet *wallet = RestoreWalletFromBackup(walletName, file, result, error, warning);
        QMetaObject::invokeMethod(this, [this, wallet, walletName, error, warning] {
            const QString shown = QString::fromStdString(walletName);
            if (!wallet) {
                Q_EMIT message(tr("Restore wallet"), tr("%1 was not restored: %2").arg(shown, QString::fromStdString(error)), CClientUIInterface::MSG_ERROR | CClientUIInterface::MODAL);
                done(false);
                return;
            }
            WalletModel *model = addModel(wallet);
            if (!warning.empty())
                Q_EMIT message(tr("Restore wallet"), QString::fromStdString(warning), CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
            rememberOpenWallets();
            setCurrent(model);
            done(true);
        }, Qt::QueuedConnection);
    });
}

void WalletController::closeWallet(WalletModel *model)
{
    if (operation || !model || isFirstWallet(model) || !walletModels.contains(model))
        return;
    CWallet *wallet = model->getWallet();
    QPointer<WalletModel> guard(model);
    start(tr("Closing %1...").arg(displayName(model)), false, [this, wallet, guard] {
        bool closed = false;
        const WalletUnloadResult result = UnloadWallet(wallet, closed);
        QMetaObject::invokeMethod(this, [this, guard, result, closed] {
            if (result != WalletUnloadResult::OK) {
                const QString why = result == WalletUnloadResult::SCANNING
                    ? tr("The wallet is being rescanned. Try again when the rescan is over.")
                    : tr("This wallet stays open.");
                Q_EMIT message(tr("Close wallet"), why, CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
                done(false);
                return;
            }
            if (guard)
                removeModel(guard);
            rememberOpenWallets();
            if (!closed)
                Q_EMIT message(tr("Close wallet"), tr("The wallet is closed, but its file is still in use by a running command. It is released when the program ends; do not copy or move it before."), CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
            done(true);
        }, Qt::QueuedConnection);
    });
}

void WalletController::reopenWallet(WalletModel *model)
{
    if (operation || !model || isFirstWallet(model) || !walletModels.contains(model))
        return;
    CWallet *wallet = model->getWallet();
    const std::string walletName = wallet->GetName();
    QPointer<WalletModel> guard(model);
    start(tr("Finishing the encryption of %1...").arg(displayName(model)), false, [this, wallet, walletName, guard] {
        bool closed = false;
        const WalletUnloadResult unloaded = UnloadWallet(wallet, closed);
        CWallet *reopened = nullptr;
        WalletLoadResult result;
        std::string error, warning;
        if (unloaded == WalletUnloadResult::OK)
            reopened = LoadWalletByName(walletName, result, error, warning);
        QMetaObject::invokeMethod(this, [this, guard, unloaded, reopened, walletName, error] {
            const QString shown = displayName(QString::fromStdString(walletName));
            if (unloaded != WalletUnloadResult::OK) {
                // it stays open as it is: encrypted, with the old logs until the program ends
                Q_EMIT message(tr("Encrypt wallet"), tr("%1 is encrypted. It could not be closed and opened again now; that happens when the program ends.").arg(shown), CClientUIInterface::MSG_WARNING | CClientUIInterface::MODAL);
                done(false);
                return;
            }
            if (guard)
                removeModel(guard);
            if (!reopened) {
                Q_EMIT message(tr("Encrypt wallet"), tr("%1 is encrypted, but it was not opened again: %2").arg(shown, QString::fromStdString(error)), CClientUIInterface::MSG_ERROR | CClientUIInterface::MODAL);
                rememberOpenWallets();
                done(false);
                return;
            }
            WalletModel *model = addModel(reopened);
            rememberOpenWallets();
            setCurrent(model);
            done(true);
            Q_EMIT backupWanted(model);
        }, Qt::QueuedConnection);
    });
}

void WalletController::openRememberedWallets()
{
    if (!remembersWallets())
        return;
    pendingRemembered.clear();
    for (const QString &name : rememberedWallets())
        if (!name.isEmpty() && !modelFor(name))
            pendingRemembered << name;
    openingRemembered = true;
    openNextRemembered();
}

void WalletController::openNextRemembered()
{
    while (!pendingRemembered.isEmpty()) {
        const QString name = pendingRemembered.takeFirst();
        // a wallet whose folder is gone is simply forgotten
        if (!fs::exists(fs::absolute(name.toStdString(), GetWalletDir())))
            continue;
        const std::string walletName = name.toStdString();
        start(tr("Opening %1...").arg(displayName(name)), true, [this, walletName] {
            WalletLoadResult result;
            std::string error, warning;
            CWallet *wallet = LoadWalletByName(walletName, result, error, warning);
            QMetaObject::invokeMethod(this, [this, wallet, walletName, error] {
                if (wallet)
                    addModel(wallet);
                else
                    Q_EMIT message(tr("Open wallet"), tr("%1 was open when the program ended, but it was not opened now: %2. It is no longer opened at the start; open it again with Open wallet.").arg(displayName(QString::fromStdString(walletName)), QString::fromStdString(error)), CClientUIInterface::MSG_WARNING);
                done(wallet != nullptr);
                openNextRemembered();
            }, Qt::QueuedConnection);
        });
        return;
    }
    openingRemembered = false;
    // the wallet that was shown last, if it is open again
    if (WalletModel *model = modelFor(rememberedCurrent()))
        setCurrent(model, false);
    rememberOpenWallets();
}

void WalletController::rememberOpenWallets()
{
    if (!remembersWallets() || openingRemembered)
        return;
    QStringList names;
    for (WalletModel *model : walletModels)
        if (!isFirstWallet(model))
            names << QString::fromStdString(model->getWallet()->GetName());
    storeInMap("openWallets", names);
}

void WalletController::rememberCurrent()
{
    if (!remembersWallets() || openingRemembered || !currentModel)
        return;
    storeInMap("currentWallet", QString::fromStdString(currentModel->getWallet()->GetName()));
}
