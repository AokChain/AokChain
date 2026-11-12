// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"

#include "aokchainunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "explorerpage.h"
#include "pagewidgets.h"
#include "tokenlists.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "tokenfilterproxy.h"
#include "tokentablemodel.h"
#include "walletmodel.h"
#include "tokenrecord.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <validation.h>
#include <utiltime.h>

#define ROW_HEIGHT 52
#define NUM_ITEMS 8

/** One recent transaction: direction tile, title, address and date, amount and asset. */
class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(AokChainUnits::AOK),
        platformStyle(_platformStyle)
    {
    }

    /** Second line: abbreviated address (or label) and the date */
    static QString subLine(const QModelIndex &index, bool *mono)
    {
        const QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        const QString address = index.data(TransactionTableModel::AddressRole).toString();
        const QString label = index.data(TransactionTableModel::LabelRole).toString();
        const int type = index.data(TransactionTableModel::TypeRole).toInt();
        *mono = false;
        if (type == TransactionRecord::SendToSelf || address.isEmpty())
            return GUIUtil::isoDateTime(date);
        if (!label.isEmpty())
            return label + QString::fromUtf8(" · ") + date.toString("yyyy-MM-dd");
        *mono = true;
        return GUIUtil::shortAddress(address) + QString::fromUtf8(" · ") + date.toString("yyyy-MM-dd");
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const GUIUtil::Theme& theme = GUIUtil::theme();
        const QRect r = option.rect;

        // separator above each row
        painter->setPen(theme.line);
        painter->drawLine(r.left(), r.top(), r.right(), r.top());

        const qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        const int type = index.data(TransactionTableModel::TypeRole).toInt();
        const bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        const bool outgoing = amount < 0 || type == TransactionRecord::SendToSelf || type == TransactionRecord::TransferTo;
        GUIUtil::paintDirectionTile(painter, QRect(r.left(), r.top() + (r.height() - 36) / 2, 36, 36), outgoing, type == TransactionRecord::Generated);

        const int left = r.left() + 48;
        const QRect topRect(left, r.top() + 7, r.width() - 48, 20);
        const QRect bottomRect(left, r.top() + 27, r.width() - 48, 18);

        // amount and asset, right-aligned
        const QString amountText = index.data(TransactionTableModel::DisplayAmountRole).toString();
        QColor amountColor = outgoing ? theme.negative : theme.positive;
        if (!confirmed)
            amountColor = theme.textDisabled;
        const int amountWidth = GUIUtil::paintAmount(painter, topRect, amountText, GUIUtil::monoFont(15, QFont::Medium), amountColor);
        painter->setFont(GUIUtil::appFont(12));
        painter->setPen(theme.textSecondary);
        const QString asset = index.data(TransactionTableModel::TokenNameRole).toString();
        painter->drawText(bottomRect, Qt::AlignRight | Qt::AlignVCenter, asset);
        const int assetWidth = painter->fontMetrics().horizontalAdvance(asset);

        // title and chip
        painter->setFont(GUIUtil::appFont(14, QFont::Medium));
        painter->setPen(theme.text);
        const bool owner = index.data(TransactionTableModel::OwnerTokenRole).toBool();
        const QString chip = owner ? tr("Owner") : QString();
        int titleSpace = topRect.width() - amountWidth - 12;
        if (owner)
            titleSpace -= 70;
        const QString title = painter->fontMetrics().elidedText(index.data(TransactionTableModel::TitleRole).toString(), Qt::ElideRight, qMax(40, titleSpace));
        painter->drawText(topRect, Qt::AlignLeft | Qt::AlignVCenter, title);
        if (owner)
            GUIUtil::paintChip(painter, left + painter->fontMetrics().horizontalAdvance(title) + 8, topRect.center().y() + 1, chip);

        // address and date
        bool mono = false;
        QString sub = subLine(index, &mono);
        painter->setFont(mono ? GUIUtil::monoFont(12) : GUIUtil::appFont(12));
        painter->setPen(theme.textSecondary);
        sub = painter->fontMetrics().elidedText(sub, Qt::ElideRight, qMax(40, bottomRect.width() - assetWidth - 12));
        painter->drawText(bottomRect, Qt::AlignLeft | Qt::AlignVCenter, sub);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            iconWatchonly.paint(painter, QRect(left + painter->fontMetrics().horizontalAdvance(sub) + 6, bottomRect.top() + 1, 16, 16));
        }

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(ROW_HEIGHT, ROW_HEIGHT);
    }

    /** The full address of an abbreviated one */
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index)
    {
        if (event->type() == QEvent::ToolTip && index.isValid()) {
            const QString address = index.data(TransactionTableModel::AddressRole).toString();
            if (!address.isEmpty()) {
                const QString label = index.data(TransactionTableModel::LabelRole).toString();
                QToolTip::showText(event->globalPos(), GUIUtil::monoToolTip(address, label), view);
                return true;
            }
        }
        return QAbstractItemDelegate::helpEvent(event, view, option, index);
    }

    int unit;
    const PlatformStyle *platformStyle;

};

namespace OverviewRootRoles {
    enum { HeldRole = Qt::UserRole + 200, ChildrenRole, RootNameRole };
}

/**
 * One token: logo, name, chips, amount. The owner token ("NAME!") is not a row
 * of its own; it shows as a chip on the row of the token. Sub tokens and unique
 * tokens are indented under their parent.
 */
class TokenViewDelegate : public QAbstractItemDelegate
{
Q_OBJECT
public:
    explicit TokenViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
            QAbstractItemDelegate(parent), unit(AokChainUnits::AOK),
            platformStyle(_platformStyle)
    {
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const GUIUtil::Theme& theme = GUIUtil::theme();
        // room for the scroll bar on the right
        const QRect r = option.rect.adjusted(0, 0, -12, 0);

        painter->setPen(theme.line);
        painter->drawLine(r.left(), r.top(), r.right(), r.top());

        QString name = index.data(TokenTableModel::TokenNameRole).toString();
        const bool admin = index.data(TokenTableModel::AdministratorRole).toBool();
        const bool locked = index.data(TokenTableModel::IsLockedRole).toBool();
        // only the owner token, or only tokens below the root, are held
        const bool ownerOnly = !index.data(OverviewRootRoles::HeldRole).toBool();
        if (name.endsWith('!'))
            name.chop(1);
        const int children = index.data(OverviewRootRoles::ChildrenRole).toInt();
        const bool sub = false, unique = false;

        const int indent = 0;
        const int logoSize = 32;
        painter->drawPixmap(r.left() + indent, r.top() + (r.height() - logoSize) / 2, GUIUtil::tokenLogo(name, logoSize));

        // amount
        QString amountText = GUIUtil::withThousands(index.data(TokenTableModel::FormattedAmountRole).toString());
        int amountWidth;
        if (ownerOnly) {
            amountText = tr("None held");
            painter->setFont(GUIUtil::appFont(13));
            painter->setPen(theme.textSecondary);
            painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, amountText);
            amountWidth = painter->fontMetrics().horizontalAdvance(amountText);
        } else {
            amountWidth = GUIUtil::paintAmount(painter, r, amountText, GUIUtil::monoFont(15, QFont::Medium), theme.text);
        }

        // name and chips
        QStringList chips;
        if (unique)
            chips << tr("Unique token");
        else if (sub)
            chips << tr("Sub token");
        if (admin)
            chips << tr("Owner");
        if (locked)
            chips << tr("Locked");
        // sub and unique tokens are not rows: the root says how many are held below it
        if (children > 0)
            chips << tr("%1 below").arg(children);
        painter->setFont(GUIUtil::appFont(11, QFont::Medium));
        int chipsWidth = 0;
        for (const QString& chip : chips)
            chipsWidth += painter->fontMetrics().horizontalAdvance(chip) + 16 + 8;

        const int left = r.left() + indent + logoSize + 12;
        painter->setFont(GUIUtil::appFont(14, QFont::Medium));
        painter->setPen(theme.text);
        const QString shown = painter->fontMetrics().elidedText(name, Qt::ElideRight, qMax(40, r.right() - left - amountWidth - chipsWidth - 12));
        painter->drawText(QRect(left, r.top(), r.width(), r.height()), Qt::AlignLeft | Qt::AlignVCenter, shown);
        int x = left + painter->fontMetrics().horizontalAdvance(shown) + 8;
        childChips.remove(index.row());
        for (const QString& chip : chips) {
            const int w = GUIUtil::paintChip(painter, x, r.center().y() + 1, chip);
            // the last chip of a root with tokens below it opens their list
            if (children > 0 && &chip == &chips.last())
                childChips.insert(index.row(), QRect(x, r.center().y() - 12, w, 24));
            x += w + 6;
        }

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(ROW_HEIGHT, ROW_HEIGHT);
    }

    int unit;
    const PlatformStyle *platformStyle;
    /** Where the "N below" chips were painted, by row */
    mutable QHash<int, QRect> childChips;
};

/**
 * The root tokens of the wallet for the list of the overview. A wallet can hold
 * tens of thousands of sub and unique tokens; they are not rows here. The rows
 * answer to the roles of the token table model, so that what is connected to a
 * click on a token works as before.
 */
class OverviewRootModel : public QAbstractListModel
{
public:
    explicit OverviewRootModel(HeldTokenIndex *_index, QObject *parent = nullptr) : QAbstractListModel(parent), index(_index)
    {
        connect(index, &HeldTokenIndex::changed, this, [this] { refill(); });
        refill();
    }

    void setFilter(const QString& text)
    {
        filter = text;
        refill();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : rows.size(); }

    QVariant data(const QModelIndex& idx, int role) const override
    {
        if (!idx.isValid() || idx.row() >= rows.size())
            return QVariant();
        const RootToken& root = rows[idx.row()];
        switch (role) {
        case TokenTableModel::TokenNameRole:
            // as the table model names it: "NAME!" when only the owner token is held
            return !root.held && root.admin ? root.name + "!" : root.name;
        case TokenTableModel::FormattedAmountRole: return root.amount;
        case TokenTableModel::AdministratorRole: return root.admin;
        case TokenTableModel::IsLockedRole: return false;
        case OverviewRootRoles::HeldRole: return root.held;
        case OverviewRootRoles::ChildrenRole: return root.children;
        case OverviewRootRoles::RootNameRole: return root.name;
        }
        return QVariant();
    }

private:
    void refill()
    {
        beginResetModel();
        rows.clear();
        for (const RootToken& root : index->roots()) {
            if (filter.isEmpty() || root.name.contains(filter, Qt::CaseInsensitive))
                rows.push_back(root);
        }
        endResetModel();
    }

    HeldTokenIndex *index;
    QString filter;
    QVector<RootToken> rows;
};

#include "overviewpage.moc"

static QLabel *captionLabel(const QString& text)
{
    QLabel *label = new QLabel(text);
    label->setProperty("role", "caption");
    label->setFont(GUIUtil::appFont(12));
    return label;
}

static AmountLabel *amountLabel()
{
    return new AmountLabel(17, QFont::Medium);
}

static QWidget *statBlock(const QString& caption, QWidget *value)
{
    QWidget *block = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(captionLabel(caption));
    layout->addWidget(value);
    return block;
}

static QFrame *hline()
{
    QFrame *line = new QFrame();
    line->setProperty("role", "hline");
    line->setFixedHeight(1);
    return line;
}

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentStake(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    currentWatchOnlyStake(-1),
    currentLockedBalance(-1),
    outOfSync(true),
    statsPending(false),
    heldTokens(nullptr),
    rootModel(nullptr),
    transactionLimit(NUM_ITEMS),
    txdelegate(new TxViewDelegate(platformStyle, this)),
    tokendelegate(new TokenViewDelegate(platformStyle, this))
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    QVBoxLayout *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(14);

    // Page header: a small hint while synchronizing and the chain status, which leads to the explorer
    QWidget *header = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    GUIUtil::ClickableLabel *hint = new GUIUtil::ClickableLabel();
    hint->setText(tr("Balances may differ from the real ones while synchronizing"));
    hint->setFont(GUIUtil::appFont(12));
    hint->setStyleSheet(QString("QLabel { color: %1; }").arg(theme.warnText.name()));
    hint->setCursor(Qt::PointingHandCursor);
    hint->setToolTip(tr("The displayed information may be out of date. Your wallet automatically synchronizes with the AokChain network after a connection is established, but this process has not completed yet."));
    connect(hint, SIGNAL(clicked(QPoint)), this, SLOT(handleOutOfSyncWarningClicks()));
    syncHint = hint;
    headerLayout->addWidget(syncHint);
    syncPill = header;

    // Search that continues in the explorer, and the state of the chain as at the top of the explorer
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->setSpacing(10);
    searchEdit = new QLineEdit();
    searchEdit->setProperty("look", "search");
    searchEdit->setFont(GUIUtil::monoFont(13));
    searchEdit->setPlaceholderText(tr("Search a transaction ID, an address or a block height"));
    searchEdit->setFixedHeight(40);
    searchEdit->addAction(QIcon(GUIUtil::lineIcon("search", 16, theme.textSecondary, 2.2)), QLineEdit::LeadingPosition);
    searchEdit->setClearButtonEnabled(true);
    QPushButton *searchButton = new QPushButton(tr("Look up"));
    searchButton->setProperty("primary", true);
    searchButton->setProperty("look", "large");
    searchButton->setFixedHeight(40);
    searchButton->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(searchEdit, 1);
    searchRow->addWidget(searchButton);
    pageLayout->addLayout(searchRow);
    connect(searchButton, SIGNAL(clicked()), this, SLOT(searchSubmitted()));
    connect(searchEdit, SIGNAL(returnPressed()), this, SLOT(searchSubmitted()));

    chainStats = new StatsStrip();
    chainStats->addCell(tr("Block height"));
    chainStats->addCell(tr("Difficulty"));
    chainStats->addCell(tr("Network stake weight"));
    chainStats->addCell(tr("Connected peers"));
    chainStats->addCell(tr("Waiting in the memory pool"), true);
    chainStats->setCompact(true);
    chainStats->setToolTip(tr("State of the chain as this node sees it. The memory pool opens the explorer."));
    pageLayout->addWidget(chainStats);
    connect(chainStats, SIGNAL(cellClicked(int)), this, SIGNAL(explorerClicked()));

    // the figures are read off the GUI thread, as in the explorer
    ExplorerWorker *worker = new ExplorerWorker();
    worker->moveToThread(&statsThread);
    connect(&statsThread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(this, SIGNAL(requestChainStatus()), worker, SLOT(chainStatus()));
    connect(worker, SIGNAL(statusDone(QString)), this, SLOT(chainStatusDone(QString)));
    statsThread.start();
    statsTimer = new QTimer(this);
    statsTimer->setInterval(10000);
    connect(statsTimer, SIGNAL(timeout()), this, SLOT(updateChainStrip()));

    // balance card
    QFrame *balanceCard = new QFrame();
    balanceCard->setProperty("role", "card");
    QHBoxLayout *balanceLayout = new QHBoxLayout(balanceCard);
    balanceLayout->setContentsMargins(28, 16, 28, 18);
    balanceLayout->setSpacing(24);

    QVBoxLayout *balanceLeft = new QVBoxLayout();
    balanceLeft->setSpacing(4);
    QLabel *totalCaption = new QLabel(tr("Total balance"));
    totalCaption->setFont(GUIUtil::appFont(13, QFont::Medium));
    totalCaption->setProperty("role", "secondary");
    balanceLeft->addWidget(totalCaption);
    labelTotal = new AmountLabel(32, QFont::Medium);
    labelTotal->setUnitColor(theme.accent);
    balanceLeft->addWidget(labelTotal);
    balanceLeft->addSpacing(2);

    QHBoxLayout *stats = new QHBoxLayout();
    stats->setSpacing(32);
    labelBalance = amountLabel();
    labelBalance->setToolTip(tr("Your current spendable balance"));
    labelUnconfirmed = amountLabel();
    labelUnconfirmed->setToolTip(tr("Total of transactions that have yet to be confirmed, and do not yet count toward the spendable balance"));
    labelImmature = amountLabel();
    labelImmature->setToolTip(tr("Staking rewards that have not yet matured"));
    labelStake = amountLabel();
    labelStake->setToolTip(tr("Coins that are staked at the moment"));
    stats->addWidget(statBlock(tr("Available"), labelBalance));
    stats->addWidget(statBlock(tr("Pending"), labelUnconfirmed));
    stats->addWidget(statBlock(tr("Immature (staking rewards)"), labelImmature));
    stakeBlock = statBlock(tr("Staked"), labelStake);
    stats->addWidget(stakeBlock);
    stats->addStretch();
    balanceLeft->addLayout(stats);

    // balances of watch-only addresses, shown only when the wallet has such addresses
    labelWatchOnly = new QWidget();
    labelWatchOnly->setToolTip(tr("Balances in watch-only addresses"));
    QHBoxLayout *watchLayout = new QHBoxLayout(labelWatchOnly);
    watchLayout->setContentsMargins(0, 0, 0, 0);
    watchLayout->setSpacing(6);
    const QString watchCaptions[4] = {tr("Watch-only: available"), tr("pending"), tr("immature"), tr("total")};
    for (int i = 0; i < 4; ++i) {
        watchLayout->addWidget(captionLabel(watchCaptions[i]));
        watchAmounts[i] = new AmountLabel(14, QFont::Medium);
        watchAmounts[i]->setColor(theme.textSecondary);
        watchLayout->addWidget(watchAmounts[i]);
        watchLayout->addSpacing(8);
    }
    watchLayout->addStretch();
    labelWatchOnly->setVisible(false);
    balanceLeft->addSpacing(6);
    balanceLeft->addWidget(labelWatchOnly);
    balanceLayout->addLayout(balanceLeft, 1);

    QPushButton *sendButton = new QPushButton(tr("Send"));
    sendButton->setProperty("primary", true);
    sendButton->setProperty("look", "large");
    sendButton->setIcon(QIcon(GUIUtil::lineIcon("send", 16, theme.onFill, 2.4)));
    sendButton->setCursor(Qt::PointingHandCursor);
    QPushButton *receiveButton = new QPushButton(tr("Receive"));
    receiveButton->setProperty("look", "large");
    receiveButton->setIcon(QIcon(GUIUtil::lineIcon("receive", 16, theme.text, 2.4)));
    receiveButton->setCursor(Qt::PointingHandCursor);
    for (QPushButton *button : {sendButton, receiveButton}) {
        button->setFont(GUIUtil::appFont(14, QFont::Medium));
        button->setIconSize(QSize(16, 16));
        button->setFixedHeight(40);
        button->setMinimumWidth(116);
    }
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->setSpacing(12);
    buttons->addWidget(sendButton);
    buttons->addWidget(receiveButton);
    QVBoxLayout *balanceRight = new QVBoxLayout();
    balanceRight->addStretch();
    balanceRight->addLayout(buttons);
    balanceLayout->addLayout(balanceRight);
    pageLayout->addWidget(balanceCard);
    connect(sendButton, SIGNAL(clicked()), this, SIGNAL(sendClicked()));
    connect(receiveButton, SIGNAL(clicked()), this, SIGNAL(receiveClicked()));

    // tokens and recent transactions
    QHBoxLayout *cards = new QHBoxLayout();
    cards->setSpacing(14);

    tokenFrame = new QFrame();
    tokenFrame->setProperty("role", "card");
    QVBoxLayout *tokenLayout = new QVBoxLayout(tokenFrame);
    tokenLayout->setContentsMargins(22, 14, 22, 10);
    tokenLayout->setSpacing(0);
    QHBoxLayout *tokenHeader = new QHBoxLayout();
    tokenHeader->setSpacing(8);
    QLabel *tokenTitle = new QLabel(tr("Tokens"));
    tokenTitle->setProperty("role", "title");
    tokenTitle->setFont(GUIUtil::appFont(15, QFont::Medium));
    tokenCount = new QLabel();
    tokenCount->setProperty("role", "secondary");
    tokenCount->setFont(GUIUtil::appFont(13, QFont::Medium));
    tokenSearch = new QLineEdit();
    tokenSearch->setPlaceholderText(tr("Search tokens"));
    tokenSearch->setFixedSize(200, 40);
    tokenSearch->addAction(QIcon(GUIUtil::lineIcon("search", 14, theme.textSecondary, 2.4)), QLineEdit::LeadingPosition);
    tokenSearch->setClearButtonEnabled(true);
    tokenHeader->addWidget(tokenTitle);
    tokenHeader->addWidget(tokenCount);
    tokenHeader->addStretch();
    tokenHeader->addWidget(tokenSearch);
    tokenLayout->addLayout(tokenHeader);
    tokenLayout->addSpacing(8);
    listTokens = new QListView();
    tokenLayout->addWidget(listTokens, 1);
    tokenLayout->addWidget(hline());
    QHBoxLayout *tokenFooter = new QHBoxLayout();
    tokenFooter->setContentsMargins(0, 8, 0, 0);
    QLabel *tokenHint = captionLabel(tr("Click a token to send it or to copy its name"));
    QPushButton *allTokens = new QPushButton(tr("Show all"));
    allTokens->setProperty("look", "link");
    allTokens->setCursor(Qt::PointingHandCursor);
    tokenFooter->addWidget(tokenHint);
    tokenFooter->addStretch();
    tokenFooter->addWidget(allTokens);
    tokenLayout->addLayout(tokenFooter);
    connect(allTokens, SIGNAL(clicked()), this, SIGNAL(allTokensClicked()));
    cards->addWidget(tokenFrame, 1);

    QFrame *txFrame = new QFrame();
    txFrame->setProperty("role", "card");
    QVBoxLayout *txLayout = new QVBoxLayout(txFrame);
    txLayout->setContentsMargins(22, 14, 22, 10);
    txLayout->setSpacing(0);
    QHBoxLayout *txHeader = new QHBoxLayout();
    QLabel *txTitle = new QLabel(tr("Recent transactions"));
    txTitle->setProperty("role", "title");
    txTitle->setFont(GUIUtil::appFont(15, QFont::Medium));
    QPushButton *allTransactions = new QPushButton(tr("Show all"));
    allTransactions->setProperty("look", "link");
    allTransactions->setCursor(Qt::PointingHandCursor);
    txHeader->addWidget(txTitle);
    txHeader->addStretch();
    txHeader->addWidget(allTransactions);
    QWidget *txHeaderBox = new QWidget();
    txHeaderBox->setLayout(txHeader);
    txHeader->setContentsMargins(0, 0, 0, 0);
    txHeaderBox->setFixedHeight(40);
    txLayout->addWidget(txHeaderBox);
    txLayout->addSpacing(8);
    listTransactions = new QListView();
    txLayout->addWidget(listTransactions, 1);
    connect(allTransactions, SIGNAL(clicked()), this, SIGNAL(allTransactionsClicked()));
    cards->addWidget(txFrame, 1);
    pageLayout->addLayout(cards, 1);

    for (QListView *list : {listTransactions, listTokens}) {
        list->setFrameShape(QFrame::NoFrame);
        list->setStyleSheet("QListView { background: transparent; border: none; }");
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setFocusPolicy(Qt::NoFocus);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setMouseTracking(true);
        list->setCursor(Qt::PointingHandCursor);
        list->setMinimumHeight(3 * ROW_HEIGHT);
    }
    listTransactions->setItemDelegate(txdelegate);
    listTransactions->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    listTokens->setItemDelegate(tokendelegate);

    // Delay before filtering tokens in ms
    static const int input_filter_delay = 200;
    QTimer *token_typing_delay = new QTimer(this);
    token_typing_delay->setSingleShot(true);
    token_typing_delay->setInterval(input_filter_delay);
    connect(tokenSearch, SIGNAL(textChanged(QString)), token_typing_delay, SLOT(start()));
    connect(token_typing_delay, SIGNAL(timeout()), this, SLOT(tokenSearchChanged()));

    connect(listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    connect(listTokens, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTokenClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

    // Trigger the call to show the tokens table if tokens are active
    showTokens();

    // context menu actions
    sendAction = new QAction(tr("Send Token"), this);
    QAction *copyAmountAction = new QAction(tr("Copy Amount"), this);
    QAction *copyNameAction = new QAction(tr("Copy Name"), this);
    issueSub = new QAction(tr("Issue Sub Token"), this);
    issueUnique = new QAction(tr("Issue Unique Token"), this);
    reissue = new QAction(tr("Reissue Token"), this);

    sendAction->setObjectName("Send");
    issueSub->setObjectName("Sub");
    issueUnique->setObjectName("Unique");
    reissue->setObjectName("Reissue");
    copyNameAction->setObjectName("Copy Name");
    copyAmountAction->setObjectName("Copy Amount");

    // context menu
    contextMenu = new QMenu(this);
    belowAction = new QAction(this);
    contextMenu->addAction(sendAction);
    contextMenu->addAction(belowAction);
    contextMenu->addAction(issueSub);
    contextMenu->addAction(issueUnique);
    contextMenu->addAction(reissue);
    contextMenu->addSeparator();
    contextMenu->addAction(copyNameAction);
    contextMenu->addAction(copyAmountAction);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleTokenClicked(const QModelIndex &index)
{
    if(rootModel) {
        const int children = index.data(OverviewRootRoles::ChildrenRole).toInt();
        const QString rootName = index.data(OverviewRootRoles::RootNameRole).toString();
        // a click on the "N below" chip opens the list of the sub and unique tokens
        if (children > 0 && tokendelegate->childChips.value(index.row()).contains(listTokens->viewport()->mapFromGlobal(QCursor::pos()))) {
            showTokensBelow(rootName);
            return;
        }
        belowAction->setVisible(children > 0);
        belowAction->setText(tr("Show the %1 sub tokens").arg(children));

        QString name = index.data(TokenTableModel::TokenNameRole).toString();
        bool fOwner = false;
        if (IsTokenNameAnOwner(name.toStdString())) {
            fOwner = true;
            name = name.left(name.size() - 1);
            sendAction->setDisabled(true);
        } else {
            // a root of which only sub tokens are held cannot be sent itself
            sendAction->setDisabled(!index.data(OverviewRootRoles::HeldRole).toBool());
        }
        Q_UNUSED(fOwner);

        if (!index.data(TokenTableModel::AdministratorRole).toBool()) {
            issueSub->setDisabled(true);
            issueUnique->setDisabled(true);
            reissue->setDisabled(true);
        } else {
            issueSub->setDisabled(false);
            issueUnique->setDisabled(false);
            reissue->setDisabled(true);
            CNewToken token;
            auto currentActiveTokenCache = GetCurrentTokenCache();
            if (currentActiveTokenCache && currentActiveTokenCache->GetTokenMetaDataIfExists(name.toStdString(), token))
                if (token.nReissuable)
                    reissue->setDisabled(false);

        }

        QAction* action = contextMenu->exec(QCursor::pos());

        if (action) {
            if (action->objectName() == "Send")
                    Q_EMIT tokenSendClicked(index);
            else if (action->objectName() == "Sub")
                    Q_EMIT tokenIssueSubClicked(index);
            else if (action->objectName() == "Unique")
                    Q_EMIT tokenIssueUniqueClicked(index);
            else if (action->objectName() == "Reissue")
                    Q_EMIT tokenReissueClicked(index);
            else if (action == belowAction)
                showTokensBelow(rootName);
            else if (action->objectName() == "Copy Name")
                GUIUtil::setClipboard(index.data(TokenTableModel::TokenNameRole).toString());
            else if (action->objectName() == "Copy Amount")
                GUIUtil::setClipboard(index.data(TokenTableModel::FormattedAmountRole).toString());
        }
    }
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    // the header widget may have been given to the page shell
    statsThread.quit();
    statsThread.wait();
    if (syncPill && !syncPill->parent())
        delete syncPill;
}

static QString displayAmount(int unit, const CAmount& amount)
{
    return GUIUtil::withThousands(AokChainUnits::format(unit, amount, false, AokChainUnits::separatorNever));
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& stake, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchOnlyStake, const CAmount& lockedBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentStake = stake;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    currentWatchOnlyStake = watchOnlyStake;
    currentLockedBalance = lockedBalance;

    labelTotal->setAmount(displayAmount(unit, balance + unconfirmedBalance + immatureBalance + lockedBalance), AokChainUnits::name(unit));
    labelBalance->setAmount(displayAmount(unit, balance));
    labelUnconfirmed->setAmount(displayAmount(unit, unconfirmedBalance + lockedBalance));
    labelImmature->setAmount(displayAmount(unit, immatureBalance));
    labelStake->setAmount(displayAmount(unit, stake));
    // the staked amount is only shown when there is one
    stakeBlock->setVisible(stake != 0 || watchOnlyStake != 0);

    watchAmounts[0]->setAmount(displayAmount(unit, watchOnlyBalance));
    watchAmounts[1]->setAmount(displayAmount(unit, watchUnconfBalance));
    watchAmounts[2]->setAmount(displayAmount(unit, watchImmatureBalance));
    watchAmounts[3]->setAmount(displayAmount(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + watchOnlyStake));
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    labelWatchOnly->setVisible(showWatchOnly);
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // The warnings of the node are shown at the bottom of the side bar.
        // Chain status for the page header
        connect(model, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateChainStrip()));
        connect(model, SIGNAL(numConnectionsChanged(int)), this, SLOT(updateChainStrip()));
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        listTransactions->setModel(filter.get());
        listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        heldTokens = HeldTokenIndex::forModel(model->getTokenTableModel());
        rootModel = new OverviewRootModel(heldTokens, this);
        listTokens->setModel(rootModel);
        connect(rootModel, SIGNAL(modelReset()), this, SLOT(updateTokenCount()));
        updateTokenCount();

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getStake(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(), model->getWatchStake(), model->getLockedBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
        updateTransactionLimit();
    }

    // update the display unit, to not use the default ("AOK")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentStake,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance, currentWatchOnlyStake, currentLockedBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    Q_UNUSED(warnings);
}

/** Ask for the figures of the chain status strip; nothing while the page is not shown. */
void OverviewPage::updateChainStrip()
{
    if (!clientModel || !isVisible() || statsPending)
        return;
    statsPending = true;
    Q_EMIT requestChainStatus();
}

void OverviewPage::chainStatusDone(const QString &json)
{
    statsPending = false;
    ExplorerPage::fillChainStats(chainStats, json);
}

/** The search of the overview is answered by the explorer. */
void OverviewPage::searchSubmitted()
{
    const QString query = searchEdit->text().trimmed();
    if (query.isEmpty())
        return;
    searchEdit->clear();
    Q_EMIT explorerRequested(query);
}

/** The window with the sub and unique tokens of a root. "Send" there leads to the send page. */
void OverviewPage::showTokensBelow(const QString &root)
{
    if (!heldTokens)
        return;
    TokenListDialog dialog(heldTokens, root, TokenListDialog::Browse, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.chosen().isEmpty())
        Q_EMIT sendTokenRequested(dialog.chosen());
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    outOfSync = fShow;
    syncHint->setVisible(fShow);
}

void OverviewPage::showTokens()
{
    tokenFrame->setVisible(AreTokensDeployed());
}

void OverviewPage::tokenSearchChanged()
{
    if (!rootModel)
        return;
    rootModel->setFilter(tokenSearch->text().trimmed());
    updateTokenCount();
}

void OverviewPage::updateTokenCount()
{
    if (!rootModel)
        return;
    tokenCount->setText(tr("%1 kinds").arg(rootModel->rowCount()));
}

/** Show as many recent transactions as fit into the card. */
void OverviewPage::updateTransactionLimit()
{
    if (!filter)
        return;
    const int rows = qBound(3, listTransactions->viewport()->height() / ROW_HEIGHT, 20);
    if (rows != transactionLimit) {
        transactionLimit = rows;
        filter->setLimit(rows);
        filter->invalidate();
    }
}

void OverviewPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { updateTransactionLimit(); });
}

void OverviewPage::hideEvent(QHideEvent *event)
{
    statsTimer->stop();
    QWidget::hideEvent(event);
}

void OverviewPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    statsTimer->start();
    QTimer::singleShot(0, this, [this] { updateChainStrip(); });
    QTimer::singleShot(0, this, [this] { updateTransactionLimit(); });
}

QWidget *OverviewPage::headerWidget() const
{
    return syncPill;
}
