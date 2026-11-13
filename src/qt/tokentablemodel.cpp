// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokentablemodel.h"
#include "tokenrecord.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "wallet/wallet.h"

#include "core_io.h"

#include "amount.h"
#include "tokens/tokens.h"
#include "validation.h"
#include "platformstyle.h"

#include <QPointer>
#include <QThread>

#include <QDebug>
#include <QStringList>



class TokenTablePriv {
public:
    TokenTablePriv(TokenTableModel *_parent) :
            parent(_parent)
    {
    }

    TokenTableModel *parent;

    QList<TokenRecord> cachedBalances;

    /**
     * Read the token balances of the wallet. This walks every token output of the
     * wallet (GetAllMyTokenBalances) and looks up the meta data of every token, so
     * it is done on a worker thread, see TokenTableModel::checkBalanceChanged().
     * Returns false when the balances could not be read.
     */
    static bool compute(QList<TokenRecord>& out) {
        qDebug() << "TokenTablePriv::refreshWallet";
        GUIUtil::LogTimer timer("token table refresh (wallet scan and meta data)");
        out.clear();
        auto currentActiveTokenCache = GetCurrentTokenCache();
        if (currentActiveTokenCache) {
            {
                LOCK(cs_main);
                std::map<std::string, CAmount> balances;
                std::map<std::string, CAmount> locked_balances;
                std::map<std::string, std::vector<COutput> > outputs;
                std::map<std::string, std::vector<COutput> > outputs_locked;

                if (!GetAllMyTokenBalances(outputs, balances)) {
                    qWarning("TokenTablePriv::refreshWallet: Error retrieving token balances");
                    return false;
                }

                if (!GetAllMyLockedTokenBalances(outputs_locked, locked_balances)) {
                    qWarning("TokenTablePriv::refreshWallet: Error retrieving locked token balances");
                    return false;
                }
                timer.setDetail(QString("%1 tokens").arg(balances.size()));
                LogPrint(BCLog::QT, "GUI timing: GetAllMyTokenBalances + GetAllMyLockedTokenBalances done\n");

                std::set<std::string> setTokensToSkip;
                auto bal = balances.begin();
                for (; bal != balances.end(); bal++) {
                    // retrieve units for token
                    uint8_t units = OWNER_UNITS;
                    bool fIsAdministrator = true;

                    if (setTokensToSkip.count(bal->first))
                        continue;

                    if (!IsTokenNameAnOwner(bal->first)) {
                        // Token is not an administrator token
                        CNewToken tokenData;
                        if (!currentActiveTokenCache->GetTokenMetaDataIfExists(bal->first, tokenData)) {
                            qWarning("TokenTablePriv::refreshWallet: Error retrieving token data");
                            return false;
                        }
                        units = tokenData.units;
                        // If we have the administrator token, add it to the skip listå
                        if (balances.count(bal->first + OWNER_TAG)) {
                            setTokensToSkip.insert(bal->first + OWNER_TAG);
                        } else {
                            fIsAdministrator = false;
                        }
                    } else {
                        // Token is an administrator token, if we own tokens that is administrators, skip this balance
                        std::string name = bal->first;
                        name.pop_back();
                        if (balances.count(name)) {
                            setTokensToSkip.insert(bal->first);
                            continue;
                        }
                    }
                    out.append(TokenRecord(bal->first, bal->second, units, fIsAdministrator, false));
                }

                auto lbal = locked_balances.begin();
                for (; lbal != locked_balances.end(); lbal++) {
                    CNewToken tokenData;
                    if (!currentActiveTokenCache->GetTokenMetaDataIfExists(lbal->first, tokenData)) {
                        qWarning("TokenTablePriv::refreshWallet: Error retrieving locked token data");
                        return false;
                    }
                    out.append(TokenRecord(lbal->first + " (LOCKED)", lbal->second, tokenData.units, false, true));
                }
            }
        }
        return true;
    }

    // loads all current balances into cache
    void refreshWallet() {
        QList<TokenRecord> fresh;
        compute(fresh);
        cachedBalances = fresh;
    }

    int size() {
        return cachedBalances.size();
    }

    TokenRecord *index(int idx) {
        if (idx >= 0 && idx < cachedBalances.size()) {
            return &cachedBalances[idx];
        }
        return 0;
    }

};

TokenTableModel::TokenTableModel(WalletModel *parent) :
        QAbstractTableModel(parent),
        walletModel(parent),
        priv(new TokenTablePriv(this)),
        refreshRunning(false),
        refreshPending(false)
{
    columns << tr("Name") << tr("Quantity");

    priv->refreshWallet();
};

TokenTableModel::~TokenTableModel()
{
    // a refresh may still be reading the wallet
    if (refreshThread)
        refreshThread->wait();
    delete priv;
};

static bool sameRecords(const QList<TokenRecord>& a, const QList<TokenRecord>& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].quantity != b[i].quantity || a[i].units != b[i].units ||
            a[i].fIsAdministrator != b[i].fIsAdministrator || a[i].fIsLocked != b[i].fIsLocked)
            return false;
    }
    return true;
}

/**
 * Called for every new block and every wallet transaction. Reading the balances walks all
 * token outputs of the wallet, which takes long with tens of thousands of tokens: it runs on
 * a worker thread, requests that arrive meanwhile are folded into one more run, and the views
 * are only told when something changed.
 */
void TokenTableModel::checkBalanceChanged() {
    qDebug() << "TokenTableModel::CheckBalanceChanged";
    if (refreshRunning) {
        refreshPending = true;
        return;
    }
    refreshRunning = true;
    refreshThread = QThread::create([this] {
        QList<TokenRecord> fresh;
        const bool ok = TokenTablePriv::compute(fresh);
        // back on the thread of the model; dropped when the model is gone
        QMetaObject::invokeMethod(this, [this, fresh, ok] { applyBalances(fresh, ok); }, Qt::QueuedConnection);
    });
    connect(refreshThread, &QThread::finished, refreshThread, &QObject::deleteLater);
    refreshThread->start();
}

void TokenTableModel::applyBalances(const QList<TokenRecord>& fresh, bool ok)
{
    refreshRunning = false;
    if (ok && !sameRecords(fresh, priv->cachedBalances)) {
        Q_EMIT layoutAboutToBeChanged();
        priv->cachedBalances = fresh;
        Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(priv->size(), columns.length()-1, QModelIndex()));
        Q_EMIT layoutChanged();
    }
    if (refreshPending) {
        refreshPending = false;
        checkBalanceChanged();
    }
}

const QList<TokenRecord>& TokenTableModel::records() const
{
    return priv->cachedBalances;
}

int TokenTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int TokenTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant TokenTableModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(role);
    if(!index.isValid())
        return QVariant();
    TokenRecord *rec = static_cast<TokenRecord*>(index.internalPointer());

    switch (role)
    {
        case AmountRole:
            return (unsigned long long) rec->quantity;
        case TokenNameRole:
            return QString::fromStdString(rec->name);
        case FormattedAmountRole:
            return QString::fromStdString(rec->formattedQuantity());
        case AdministratorRole:
        {
            return rec->fIsAdministrator;
        }
        case IsLockedRole:
        {
            return rec->fIsLocked;
        }
        case Qt::DecorationRole:
        {
            QPixmap pixmap;

            if (!rec->fIsAdministrator) {
                QVariant();
                if (rec->fIsLocked) {
                    pixmap = QPixmap::fromImage(QImage(":/icons/token_locked"));
                }
            } else {
                pixmap = QPixmap::fromImage(QImage(":/icons/token_administrator"));
            }
            
            return pixmap;
        }
        case Qt::ToolTipRole:
            return formatTooltip(rec);
        default:
            return QVariant();
    }
}

QVariant TokenTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole)
    {
            if (section < columns.size())
                return columns.at(section);
    } else if (role == Qt::SizeHintRole) {
        if (section == 0)
            return QSize(300, 50);
        else if (section == 1)
            return QSize(200, 50);
    } else if (role == Qt::TextAlignmentRole) {
        return Qt::AlignHCenter + Qt::AlignVCenter;
    }

    return QVariant();
}

QModelIndex TokenTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    TokenRecord *data = priv->index(row);
    if(data)
    {
        QModelIndex idx = createIndex(row, column, priv->index(row));
        return idx;
    }

    return QModelIndex();
}

QString TokenTableModel::formatTooltip(const TokenRecord *rec) const
{
    QString tooltip = formatTokenName(rec) + QString("\n") + formatTokenQuantity(rec);
    return tooltip;
}

QString TokenTableModel::formatTokenName(const TokenRecord *wtx) const
{
    return tr("Token Name: ") + QString::fromStdString(wtx->name);
}

QString TokenTableModel::formatTokenQuantity(const TokenRecord *wtx) const
{
    return tr("Token Quantity: ") + QString::fromStdString(wtx->formattedQuantity());
}