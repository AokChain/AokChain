// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionview.h"

#include "addresstablemodel.h"
#include "aokchainunits.h"
#include "csvmodelwriter.h"
#include "editaddressdialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "sendcoinsdialog.h"
#include "transactiondescdialog.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "validation.h"
#include "walletmodel.h"
#include "guiconstants.h"

#include "ui_interface.h"

#include <QAbstractProxyModel>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSignalMapper>
#include <QTableView>
#include <QSet>
#include <QTimer>
#include <QHelpEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

/**
 * Shows one page of the filtered and sorted transactions, so that a history of
 * tens of thousands of transactions is never put into the view at once.
 * Paging comes after filter and sort: it is a window on the rows of the filter
 * proxy. The export uses the filter proxy and so writes all matching rows.
 */
class TransactionPageProxy : public QAbstractProxyModel
{
public:
    explicit TransactionPageProxy(QObject *parent) : QAbstractProxyModel(parent), pageSize(50), page(0) {}

    void setSourceModel(QAbstractItemModel *source) override
    {
        QAbstractProxyModel::setSourceModel(source);
        // whatever changes the rows of the source may change what is on the page
        connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] { beginResetModel(); });
        connect(source, &QAbstractItemModel::modelReset, this, [this] { clamp(); endResetModel(); });
        connect(source, &QAbstractItemModel::layoutAboutToBeChanged, this, [this] { beginResetModel(); });
        connect(source, &QAbstractItemModel::layoutChanged, this, [this] { clamp(); endResetModel(); });
        connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this, [this] { beginResetModel(); });
        connect(source, &QAbstractItemModel::rowsInserted, this, [this] { clamp(); endResetModel(); });
        connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] { beginResetModel(); });
        connect(source, &QAbstractItemModel::rowsRemoved, this, [this] { clamp(); endResetModel(); });
        connect(source, &QAbstractItemModel::dataChanged, this, [this] {
            if (rowCount() > 0)
                Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
        });
        connect(source, &QAbstractItemModel::headerDataChanged, this, &QAbstractItemModel::headerDataChanged);
    }

    int totalRows() const { return sourceModel() ? sourceModel()->rowCount() : 0; }
    int pageCount() const { return qMax(1, (totalRows() + pageSize - 1) / pageSize); }
    int currentPage() const { return page; }
    int rowsPerPage() const { return pageSize; }

    void setPage(int newPage)
    {
        newPage = qBound(0, newPage, pageCount() - 1);
        if (newPage == page)
            return;
        beginResetModel();
        page = newPage;
        endResetModel();
    }

    void setPageSize(int size)
    {
        beginResetModel();
        pageSize = qMax(1, size);
        page = 0;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return qBound(0, totalRows() - page * pageSize, pageSize);
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() || !sourceModel() ? 0 : sourceModel()->columnCount();
    }
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
            return QModelIndex();
        return createIndex(row, column);
    }
    QModelIndex parent(const QModelIndex &) const override { return QModelIndex(); }
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override
    {
        if (!proxyIndex.isValid() || !sourceModel())
            return QModelIndex();
        return sourceModel()->index(proxyIndex.row() + page * pageSize, proxyIndex.column());
    }
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override
    {
        if (!sourceIndex.isValid())
            return QModelIndex();
        return index(sourceIndex.row() - page * pageSize, sourceIndex.column());
    }
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override
    {
        if (sourceModel())
            sourceModel()->sort(column, order);
    }

private:
    void clamp() { page = qBound(0, page, pageCount() - 1); }

    int pageSize;
    int page;
};

/**
 * Paints the cells of the transaction table: a tile with the direction, the
 * description on two lines, the date in the fixed-width font, the status as a
 * badge and the amount with its sign and asset.
 */
class TransactionCellDelegate : public QStyledItemDelegate
{
public:
    explicit TransactionCellDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const GUIUtil::Theme& theme = GUIUtil::theme();
        const QRect r = option.rect;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (option.state & QStyle::State_Selected)
            painter->fillRect(r, theme.accentSoft);
        painter->setPen(theme.line);
        painter->drawLine(r.left(), r.top(), r.right(), r.top());

        const qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        const int type = index.data(TransactionTableModel::TypeRole).toInt();
        const int status = index.data(TransactionTableModel::StatusRole).toInt();
        const bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        const bool outgoing = amount < 0 || type == TransactionRecord::SendToSelf || type == TransactionRecord::TransferTo;
        const bool failed = status == TransactionStatus::Conflicted || status == TransactionStatus::Abandoned || status == TransactionStatus::NotAccepted;

        switch (index.column()) {
        case TransactionTableModel::Type:
            GUIUtil::paintDirectionTile(painter, QRect(r.left(), r.top() + (r.height() - 36) / 2, 36, 36), outgoing, type == TransactionRecord::Generated);
            break;
        case TransactionTableModel::Watchonly: {
            QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            icon.paint(painter, QRect(r.left(), r.top() + (r.height() - 16) / 2, 16, 16));
            break;
        }
        case TransactionTableModel::ToAddress: {
            const QRect topRect(r.left(), r.top() + 9, r.width() - 12, 20);
            const QRect bottomRect(r.left(), r.top() + 29, r.width() - 12, 18);
            painter->setFont(GUIUtil::appFont(14, QFont::DemiBold));
            painter->setPen(failed ? theme.textDisabled : theme.text);
            const bool owner = index.data(TransactionTableModel::OwnerTokenRole).toBool();
            const QString title = painter->fontMetrics().elidedText(index.data(TransactionTableModel::TitleRole).toString(), Qt::ElideRight, topRect.width() - (owner ? 80 : 0));
            painter->drawText(topRect, Qt::AlignLeft | Qt::AlignVCenter, title);
            if (owner)
                GUIUtil::paintChip(painter, r.left() + painter->fontMetrics().horizontalAdvance(title) + 8, topRect.center().y() + 1, TransactionView::tr("Owner"));
            const QString label = index.data(TransactionTableModel::LabelRole).toString();
            const QString address = index.data(TransactionTableModel::AddressRole).toString();
            painter->setPen(theme.textSecondary);
            if (!label.isEmpty()) {
                painter->setFont(GUIUtil::appFont(12));
                painter->drawText(bottomRect, Qt::AlignLeft | Qt::AlignVCenter, painter->fontMetrics().elidedText(label, Qt::ElideRight, bottomRect.width()));
            } else if (!address.isEmpty() && type != TransactionRecord::SendToSelf) {
                painter->setFont(GUIUtil::monoFont(12));
                // the full address when it fits, the short form otherwise
                const QString shown = painter->fontMetrics().horizontalAdvance(address) <= bottomRect.width() ? address : GUIUtil::shortAddress(address);
                painter->drawText(bottomRect, Qt::AlignLeft | Qt::AlignVCenter, shown);
            } else {
                painter->setFont(GUIUtil::appFont(12));
                painter->drawText(bottomRect, Qt::AlignLeft | Qt::AlignVCenter, TransactionView::tr("No label"));
            }
            break;
        }
        case TransactionTableModel::Date:
            painter->setFont(GUIUtil::monoFont(13));
            painter->setPen(theme.label);
            painter->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::isoDateTime(index.data(TransactionTableModel::DateRole).toDateTime()));
            break;
        case TransactionTableModel::Status: {
            QColor fg = theme.negative, bg = theme.negativeSoft;
            if (status == TransactionStatus::Confirmed) {
                fg = theme.ok;
                bg = theme.okSoft;
            } else if (failed) {
                fg = theme.error;
                bg = theme.errorSoft;
            }
            GUIUtil::paintBadge(painter, r.adjusted(0, 0, -8, 0), index.data(TransactionTableModel::StatusTextRole).toString(), fg, bg);
            break;
        }
        case TransactionTableModel::Amount: {
            const QString asset = index.data(TransactionTableModel::TokenNameRole).toString();
            painter->setFont(GUIUtil::appFont(12));
            painter->setPen(theme.textSecondary);
            const QRect cell = r.adjusted(0, 0, -10, 0);
            const int assetWidth = qMin(painter->fontMetrics().horizontalAdvance(asset) + 2, cell.width() / 2);
            painter->drawText(cell, Qt::AlignRight | Qt::AlignVCenter, painter->fontMetrics().elidedText(asset, Qt::ElideRight, assetWidth));
            QColor color = outgoing ? theme.negative : theme.positive;
            if (!confirmed || failed)
                color = theme.textDisabled;
            const QRect amountRect = cell.adjusted(0, 0, -(assetWidth + 6), 0);
            GUIUtil::paintAmount(painter, amountRect, index.data(TransactionTableModel::DisplayAmountRole).toString(), GUIUtil::monoFont(15, QFont::DemiBold), color);
            break;
        }
        default:
            break;
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(40, TransactionView::ROW_HEIGHT);
    }

    /** The full address in the fixed-width font when the pointer is over the description */
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (event->type() == QEvent::ToolTip && index.isValid() && index.column() == TransactionTableModel::ToAddress) {
            const QString address = index.data(TransactionTableModel::AddressRole).toString();
            if (!address.isEmpty()) {
                QToolTip::showText(event->globalPos(), GUIUtil::monoToolTip(address, index.data(TransactionTableModel::LabelRole).toString()), view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
};

TransactionView::TransactionView(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent), model(0), transactionProxyModel(0),
    transactionView(0), abandonAction(0), bumpFeeAction(0), columnResizingFixer(0), assetRowsSeen(0), assetRescan(false), pageProxy(0)
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    // Filter row: type as a segmented control, search, asset, period
    setContentsMargins(0,0,0,0);
    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setContentsMargins(0,0,0,0);
    hlayout->setSpacing(10);

    QFrame *segFrame = new QFrame(this);
    segFrame->setProperty("role", "segGroup");
    QHBoxLayout *segLayout = new QHBoxLayout(segFrame);
    segLayout->setContentsMargins(3, 3, 3, 3);
    segLayout->setSpacing(4);
    typeButtons = new QButtonGroup(this);
    const quint32 received = TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) | TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther) |
                             TransactionFilterProxy::TYPE(TransactionRecord::TransferFrom);
    const quint32 sent = TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) | TransactionFilterProxy::TYPE(TransactionRecord::SendToOther) |
                         TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf) | TransactionFilterProxy::TYPE(TransactionRecord::TransferTo);
    const std::pair<QString, quint32> segments[] = {
        {tr("All"), TransactionFilterProxy::ALL_TYPES},
        {tr("Received"), received},
        {tr("Sent"), sent},
        {tr("Staking"), TransactionFilterProxy::TYPE(TransactionRecord::Generated)},
    };
    int segmentId = 0;
    for (const std::pair<QString, quint32>& segment : segments) {
        QPushButton *button = new QPushButton(segment.first, segFrame);
        button->setProperty("look", "seg");
        button->setProperty("typeFilter", segment.second);
        button->setCheckable(true);
        button->setFixedHeight(40);
        button->setCursor(Qt::PointingHandCursor);
        typeButtons->addButton(button, segmentId++);
        segLayout->addWidget(button);
    }
    typeButtons->button(0)->setChecked(true);
    hlayout->addWidget(segFrame);

    addressWidget = new QLineEdit(this);
    addressWidget->setProperty("look", "search");
    addressWidget->setPlaceholderText(tr("Search address or label"));
    addressWidget->setFixedHeight(48);
    addressWidget->addAction(QIcon(GUIUtil::lineIcon("search", 16, theme.textSecondary, 2.2)), QLineEdit::LeadingPosition);
    addressWidget->setClearButtonEnabled(true);
    hlayout->addWidget(addressWidget, 1);

    // the coins and tokens that occur in the transactions of the wallet
    assetWidget = new QComboBox(this);
    assetWidget->setFixedSize(170, 48);
    assetWidget->addItem(tr("All assets"), QString());
    assetWidget->setToolTip(tr("Show only the transactions of one coin or of one token with its sub tokens"));
    hlayout->addWidget(assetWidget);
    assetListTimer = new QTimer(this);
    assetListTimer->setSingleShot(true);
    assetListTimer->setInterval(500);
    connect(assetListTimer, SIGNAL(timeout()), this, SLOT(updateAssetList()));

    dateWidget = new QComboBox(this);
    dateWidget->setFixedSize(140, 48);
    dateWidget->addItem(tr("All time"), All);
    dateWidget->addItem(tr("Past year"), PastYear);
    dateWidget->addItem(tr("Today"), Today);
    dateWidget->addItem(tr("This week"), ThisWeek);
    dateWidget->addItem(tr("This month"), ThisMonth);
    dateWidget->addItem(tr("Last month"), LastMonth);
    dateWidget->addItem(tr("This year"), ThisYear);
    dateWidget->addItem(tr("Range..."), Range);
    hlayout->addWidget(dateWidget);

    QToolButton *moreButton = new QToolButton(this);
    moreButton->setIcon(QIcon(GUIUtil::lineIcon("manage", 18, theme.label)));
    moreButton->setIconSize(QSize(18, 18));
    moreButton->setFixedSize(48, 48);
    moreButton->setCheckable(true);
    moreButton->setToolTip(tr("More filters: other transaction types, minimum amount, watch-only"));
    moreButton->setCursor(Qt::PointingHandCursor);
    hlayout->addWidget(moreButton);

    // Second filter row with the remaining filters, hidden until asked for
    QWidget *moreRow = new QWidget(this);
    QHBoxLayout *moreLayout = new QHBoxLayout(moreRow);
    moreLayout->setContentsMargins(0,0,0,0);
    moreLayout->setSpacing(10);
    QLabel *typeLabel = new QLabel(tr("Type"), moreRow);
    typeLabel->setProperty("role", "fieldLabel");
    moreLayout->addWidget(typeLabel);
    typeWidget = new QComboBox(this);
    typeWidget->setFixedWidth(190);

    typeWidget->addItem(tr("All"), TransactionFilterProxy::ALL_TYPES);
    typeWidget->addItem(tr("Received with"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                        TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    typeWidget->addItem(tr("Sent to"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                  TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
    typeWidget->addItem(tr("To yourself"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
    typeWidget->addItem(tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated));
    typeWidget->addItem(tr("Other"), TransactionFilterProxy::TYPE(TransactionRecord::Other));
    moreLayout->addWidget(typeWidget);

    amountWidget = new QLineEdit(this);
    amountWidget->setPlaceholderText(tr("Min amount"));
    amountWidget->setFixedWidth(170);
    amountWidget->setFont(GUIUtil::monoFont(13));
    amountWidget->setValidator(new QDoubleValidator(0, 1e20, 8, this));
    moreLayout->addWidget(amountWidget);

    watchOnlyWidget = new QComboBox(this);
    watchOnlyWidget->setFixedWidth(64);
    watchOnlyWidget->addItem("", TransactionFilterProxy::WatchOnlyFilter_All);
    watchOnlyWidget->addItem(platformStyle->SingleColorIcon(":/icons/eye_plus"), "", TransactionFilterProxy::WatchOnlyFilter_Yes);
    watchOnlyWidget->addItem(platformStyle->SingleColorIcon(":/icons/eye_minus"), "", TransactionFilterProxy::WatchOnlyFilter_No);
    moreLayout->addWidget(watchOnlyWidget);
    moreLayout->addStretch();
    moreRow->hide();
    connect(moreButton, SIGNAL(toggled(bool)), moreRow, SLOT(setVisible(bool)));

    // Delay before filtering transactions in ms
    static const int input_filter_delay = 200;

    QTimer* amount_typing_delay = new QTimer(this);
    amount_typing_delay->setSingleShot(true);
    amount_typing_delay->setInterval(input_filter_delay);

    QTimer* prefix_typing_delay = new QTimer(this);
    prefix_typing_delay->setSingleShot(true);
    prefix_typing_delay->setInterval(input_filter_delay);



    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(12);

    // The table sits in a card
    QFrame *card = new QFrame(this);
    card->setProperty("role", "card");
    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 4, 12, 12);
    cardLayout->setSpacing(0);

    QTableView *view = new QTableView(card);
    cardLayout->addWidget(view, 1);
    QFrame *footerLine = new QFrame(card);
    footerLine->setProperty("role", "hline");
    footerLine->setFixedHeight(1);
    cardLayout->addWidget(footerLine);
    QHBoxLayout *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 10, 12, 0);
    // number of matching transactions, and the pages
    pageCountLabel = new QLabel(card);
    pageCountLabel->setProperty("role", "caption");
    pageCountLabel->setToolTip(tr("Double-click a row for the details; the menu of the right mouse button looks a transaction up in the explorer"));
    footer->addWidget(pageCountLabel);
    footer->addStretch();
    const std::pair<QPushButton**, QString> pageButtons[] = {
        {&pageFirst, QString::fromUtf8("«")}, {&pagePrevious, QString::fromUtf8("‹ ") + tr("Previous")},
        {&pageNext, tr("Next") + QString::fromUtf8(" ›")}, {&pageLast, QString::fromUtf8("»")}};
    for (const std::pair<QPushButton**, QString>& button : pageButtons) {
        *button.first = new QPushButton(button.second, card);
        (*button.first)->setProperty("look", "chipButton");
        (*button.first)->setCursor(Qt::PointingHandCursor);
        (*button.first)->setFixedHeight(30);
    }
    pageFirst->setToolTip(tr("First page"));
    pageLast->setToolTip(tr("Last page"));
    pageLabel = new QLabel(card);
    pageLabel->setFont(GUIUtil::monoFont(13, QFont::DemiBold));
    pageLabel->setMinimumWidth(80);
    pageLabel->setAlignment(Qt::AlignCenter);
    footer->addWidget(pageFirst);
    footer->addSpacing(6);
    footer->addWidget(pagePrevious);
    footer->addWidget(pageLabel);
    footer->addWidget(pageNext);
    footer->addSpacing(6);
    footer->addWidget(pageLast);
    footer->addSpacing(12);
    pageSizeWidget = new QComboBox(card);
    for (int size : {50, 100, 200})
        pageSizeWidget->addItem(tr("%1 per page").arg(size), size);
    pageSizeWidget->setFixedHeight(32);
    footer->addWidget(pageSizeWidget);
    footer->addSpacing(12);
    QPushButton *explorerLink = new QPushButton(tr("Open the explorer"), card);
    explorerLink->setProperty("look", "link");
    explorerLink->setCursor(Qt::PointingHandCursor);
    footer->addWidget(explorerLink);
    connect(pageFirst, &QPushButton::clicked, this, [this] { gotoPage(0); });
    connect(pagePrevious, &QPushButton::clicked, this, [this] { gotoPage(pageProxy ? pageProxy->currentPage() - 1 : 0); });
    connect(pageNext, &QPushButton::clicked, this, [this] { gotoPage(pageProxy ? pageProxy->currentPage() + 1 : 0); });
    connect(pageLast, &QPushButton::clicked, this, [this] { gotoPage(pageProxy ? pageProxy->pageCount() - 1 : 0); });
    connect(pageSizeWidget, SIGNAL(activated(int)), this, SLOT(pageSizeChosen(int)));
    cardLayout->addLayout(footer);
    connect(explorerLink, SIGNAL(clicked()), this, SLOT(openExplorer()));

    vlayout->addLayout(hlayout);
    vlayout->addWidget(moreRow);
    vlayout->addWidget(createDateRangeWidget());
    vlayout->addWidget(card, 1);

    view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet("QTableView { border: none; background: transparent; } QTableView::item { padding: 0; border: none; }");

    view->installEventFilter(this);
    view->setShowGrid(false);
    view->setWordWrap(false);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setItemDelegate(new TransactionCellDelegate(view));
    // fixed row heights: the view never has to measure the rows
    view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view->verticalHeader()->setDefaultSectionSize(ROW_HEIGHT);
    view->verticalHeader()->setMinimumSectionSize(ROW_HEIGHT);
    view->horizontalHeader()->setHighlightSections(false);
    view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    transactionView = view;
    transactionView->setObjectName("transactionView");

    // Actions
    abandonAction = new QAction(tr("Abandon transaction"), this);
//    bumpFeeAction = new QAction(tr("Increase transaction fee"), this);
//    bumpFeeAction->setObjectName("bumpFeeAction");
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyTxIDAction = new QAction(tr("Copy transaction ID"), this);
    QAction *copyTxHexAction = new QAction(tr("Copy raw transaction"), this);
    QAction *copyTxPlainText = new QAction(tr("Copy full transaction details"), this);
    QAction *editLabelAction = new QAction(tr("Edit label"), this);
    QAction *showDetailsAction = new QAction(tr("Show transaction details"), this);
    QAction *showInExplorerAction = new QAction(tr("Show in the explorer"), this);

    contextMenu = new QMenu(this);
    contextMenu->setObjectName("contextMenu");
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTxIDAction);
    contextMenu->addAction(copyTxHexAction);
    contextMenu->addAction(copyTxPlainText);
    contextMenu->addAction(showDetailsAction);
    contextMenu->addAction(showInExplorerAction);
    contextMenu->addSeparator();
//    contextMenu->addAction(bumpFeeAction);
    contextMenu->addAction(abandonAction);
    contextMenu->addAction(editLabelAction);

    mapperThirdPartyTxUrls = new QSignalMapper(this);

    // Connect actions
    connect(mapperThirdPartyTxUrls, SIGNAL(mapped(QString)), this, SLOT(openThirdPartyTxUrl(QString)));

    connect(dateWidget, SIGNAL(activated(int)), this, SLOT(chooseDate(int)));
    connect(typeWidget, SIGNAL(activated(int)), this, SLOT(chooseType(int)));
    connect(watchOnlyWidget, SIGNAL(activated(int)), this, SLOT(chooseWatchonly(int)));
    connect(amountWidget, SIGNAL(textChanged(QString)), amount_typing_delay, SLOT(start()));
    connect(amount_typing_delay, SIGNAL(timeout()), this, SLOT(changedAmount()));
    connect(assetWidget, SIGNAL(activated(int)), this, SLOT(changedTokenName()));
    connect(addressWidget, SIGNAL(textChanged(QString)), prefix_typing_delay, SLOT(start()));
    connect(prefix_typing_delay, SIGNAL(timeout()), this, SLOT(changedPrefix()));

    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SIGNAL(doubleClicked(QModelIndex)));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

//    connect(bumpFeeAction, SIGNAL(triggered()), this, SLOT(bumpFee()));
    connect(abandonAction, SIGNAL(triggered()), this, SLOT(abandonTx()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTxIDAction, SIGNAL(triggered()), this, SLOT(copyTxID()));
    connect(copyTxHexAction, SIGNAL(triggered()), this, SLOT(copyTxHex()));
    connect(copyTxPlainText, SIGNAL(triggered()), this, SLOT(copyTxPlainText()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(editLabel()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(showDetails()));
    connect(showInExplorerAction, SIGNAL(triggered()), this, SLOT(showInExplorer()));
    connect(typeButtons, SIGNAL(buttonClicked(int)), this, SLOT(chooseTypeSegment(int)));

    // Trigger the call to show the tokens table if tokens are active
    showingTokens = false;
    showTokens();

    dateWidget->setFont(GUIUtil::getSubLabelFont());
    typeWidget->setFont(GUIUtil::getSubLabelFont());
    addressWidget->setFont(GUIUtil::getSubLabelFont());
    assetWidget->setFont(GUIUtil::getSubLabelFont());
    contextMenu->setFont(GUIUtil::getSubLabelFont());
    transactionView->setFont(GUIUtil::getSubLabelFont());

}

void TransactionView::setModel(WalletModel *_model)
{
    this->model = _model;
    if(_model)
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        transactionProxyModel->setSourceModel(_model->getTransactionTableModel());
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        transactionProxyModel->setSortRole(Qt::EditRole);

        transactionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // the view shows one page of the filtered and sorted rows
        pageProxy = new TransactionPageProxy(this);
        pageProxy->setSourceModel(transactionProxyModel);
        transactionView->setModel(pageProxy);
        connect(pageProxy, &QAbstractItemModel::modelAboutToBeReset, this, [this] { rememberSelection(); });
        connect(pageProxy, &QAbstractItemModel::modelReset, this, [this] { updatePageControls(); restoreSelection(); });
        updatePageControls();
        transactionView->setAlternatingRowColors(false);
        transactionView->setSelectionBehavior(QAbstractItemView::SelectRows);
        transactionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        transactionView->setSortingEnabled(true);
        transactionView->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);
        transactionView->verticalHeader()->hide();

        // Column order: direction, (watch-only), description, date, status, amount with the asset.
        // The widths are fixed and the description takes the rest, so that the view does not
        // have to measure the contents of thousands of rows.
        QHeaderView *header = transactionView->horizontalHeader();
        header->setMinimumSectionSize(MINIMUM_COLUMN_WIDTH);
        header->setSectionsMovable(false);
        const int order[] = {TransactionTableModel::Type, TransactionTableModel::Watchonly, TransactionTableModel::ToAddress,
                             TransactionTableModel::Date, TransactionTableModel::Status, TransactionTableModel::Amount, TransactionTableModel::TokenName};
        for (int visual = 0; visual < 7; ++visual)
            header->moveSection(header->visualIndex(order[visual]), visual);
        for (int column : {TransactionTableModel::Type, TransactionTableModel::Watchonly, TransactionTableModel::Date,
                           TransactionTableModel::Status, TransactionTableModel::Amount})
            header->setSectionResizeMode(column, QHeaderView::Fixed);
        header->setSectionResizeMode(TransactionTableModel::ToAddress, QHeaderView::Stretch);
        header->setStretchLastSection(false);
        transactionView->setColumnWidth(TransactionTableModel::Type, TYPE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Watchonly, WATCHONLY_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Date, DATE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Status, STATUS_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Amount, AMOUNT_COLUMN_WIDTH);
        // the asset is shown next to the amount
        transactionView->setColumnHidden(TransactionTableModel::TokenName, true);

        if (_model->getOptionsModel())
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = _model->getOptionsModel()->getThirdPartyTxUrls().split("|", QString::SkipEmptyParts);
            for (int i = 0; i < listUrls.size(); ++i)
            {
                QString host = QUrl(listUrls[i].trimmed(), QUrl::StrictMode).host();
                if (!host.isEmpty())
                {
                    QAction *thirdPartyTxUrlAction = new QAction(host, this); // use host as menu item label
                    if (i == 0)
                        contextMenu->addSeparator();
                    contextMenu->addAction(thirdPartyTxUrlAction);
                    connect(thirdPartyTxUrlAction, SIGNAL(triggered()), mapperThirdPartyTxUrls, SLOT(map()));
                    mapperThirdPartyTxUrls->setMapping(thirdPartyTxUrlAction, listUrls[i].trimmed());
                }
            }
        }

        chooseDate(dateWidget->currentIndex());
        updateAssetList();
        connect(_model->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(assetRowsInserted(QModelIndex,int,int)));

        // show/hide column Watch-only
        updateWatchOnlyColumn(_model->haveWatchOnly());

        // Watch-only signal
        connect(_model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyColumn(bool)));
    }
}

void TransactionView::chooseDate(int idx)
{
    if(!transactionProxyModel)
        return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        firstPage(), transactionProxyModel->setDateRange(
                TransactionFilterProxy::MIN_DATE,
                TransactionFilterProxy::MAX_DATE);
        break;
    case Today:
        firstPage(), transactionProxyModel->setDateRange(
                QDateTime(current),
                TransactionFilterProxy::MAX_DATE);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        firstPage(), transactionProxyModel->setDateRange(
                QDateTime(startOfWeek),
                TransactionFilterProxy::MAX_DATE);

        } break;
    case ThisMonth:
        firstPage(), transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), current.month(), 1)),
                TransactionFilterProxy::MAX_DATE);
        break;
    case LastMonth:
        firstPage(), transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), current.month(), 1).addMonths(-1)),
                QDateTime(QDate(current.year(), current.month(), 1)));
        break;
    case PastYear:
        firstPage(), transactionProxyModel->setDateRange(
                QDateTime::currentDateTime().addYears(-1),
                TransactionFilterProxy::MAX_DATE);
        break;
    case ThisYear:
        firstPage(), transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), 1, 1)),
                TransactionFilterProxy::MAX_DATE);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    firstPage(), transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
    // the segmented control shows a choice only when it made it
    typeButtons->setExclusive(false);
    for (QAbstractButton *button : typeButtons->buttons())
        button->setChecked(idx == 0 && typeButtons->id(button) == 0);
    typeButtons->setExclusive(true);
}

void TransactionView::chooseTypeSegment(int id)
{
    if(!transactionProxyModel || !typeButtons->button(id))
        return;
    firstPage(), transactionProxyModel->setTypeFilter(typeButtons->button(id)->property("typeFilter").toUInt());
    typeWidget->setCurrentIndex(0);
}

void TransactionView::showInExplorer()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
        Q_EMIT explorerRequested(selection.at(0).data(TransactionTableModel::TxHashRole).toString());
}

void TransactionView::openExplorer()
{
    Q_EMIT explorerRequested(QString());
}

void TransactionView::chooseWatchonly(int idx)
{
    if(!transactionProxyModel)
        return;
    firstPage(), transactionProxyModel->setWatchOnlyFilter(
        static_cast<TransactionFilterProxy::WatchOnlyFilter>(watchOnlyWidget->itemData(idx).toInt()));
}

void TransactionView::changedPrefix()
{
    if(!transactionProxyModel)
        return;
    firstPage(), transactionProxyModel->setAddressPrefix(addressWidget->text());
}

void TransactionView::changedAmount()
{
    if(!transactionProxyModel)
        return;
    CAmount amount_parsed = 0;
    if (AokChainUnits::parse(model->getOptionsModel()->getDisplayUnit(), amountWidget->text(), &amount_parsed)) {
        firstPage(), transactionProxyModel->setMinAmount(amount_parsed);
    }
    else
    {
        firstPage(), transactionProxyModel->setMinAmount(0);
    }
}

void TransactionView::changedTokenName()
{
    if (!transactionProxyModel)
        return;
    firstPage(), transactionProxyModel->setAssetName(assetWidget->currentData().toString());
}

/** Remember the root of the asset of a row of the transaction table. */
void TransactionView::noteAssetRow(int row)
{
    const QString name = model->getTransactionTableModel()->index(row, 0).data(TransactionTableModel::AssetNameRole).toString();
    int end = 0;
    while (end < name.size() && name[end] != '/' && name[end] != '#' && name[end] != '!')
        ++end;
    assetRoots.insert(name.left(end));
}

void TransactionView::assetRowsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(parent);
    if (!model || !model->getTransactionTableModel())
        return;
    // a few new rows are looked at right away; a large batch is left to one pass over the table
    if (last - first > 2000) {
        assetRescan = true;
    } else {
        for (int row = first; row <= last; ++row)
            noteAssetRow(row);
    }
    assetListTimer->start();
}

/** Fill the asset filter with what occurs in the transactions: AOK first, then the root tokens by
    name. Sub and unique tokens are not listed (there can be tens of thousands); choosing a root
    shows them as well. */
void TransactionView::updateAssetList()
{
    if (!model || !model->getTransactionTableModel())
        return;
    GUIUtil::LogTimer timer("transfers asset list");
    TransactionTableModel *ttm = model->getTransactionTableModel();
    // New rows are looked at when they are inserted (assetRowsInserted); the whole table is
    // read once, and again when rows were removed or very many arrived at once.
    const int rows = ttm->rowCount(QModelIndex());
    const int before = assetRoots.size();
    const bool all = assetRowsSeen == 0 || rows < assetRowsSeen || assetRescan;
    if (all) {
        assetRoots.clear();
        for (int row = 0; row < rows; ++row)
            noteAssetRow(row);
        assetRescan = false;
    }
    assetRowsSeen = rows;
    timer.setDetail(QString("%1 rows, %2").arg(rows).arg(all ? "all rows" : "list only"));
    if (!all && assetRoots.size() == before && assetWidget->count() > 1)
        return; // nothing new to list
    QSet<QString> names = assetRoots;
    names.remove(QString());
    QStringList tokens = names.values();
    tokens.removeAll("AOK");
    tokens.sort();
    if (names.contains("AOK"))
        tokens.prepend("AOK");

    const QString current = assetWidget->currentData().toString();
    assetWidget->blockSignals(true);
    assetWidget->clear();
    assetWidget->addItem(tr("All assets"), QString());
    for (const QString& name : tokens)
        assetWidget->addItem(name, name);
    const int index = assetWidget->findData(current);
    assetWidget->setCurrentIndex(index < 0 ? 0 : index);
    assetWidget->blockSignals(false);
    // the chosen asset may have gone with a removed transaction
    if (index < 0 && !current.isEmpty())
        changedTokenName();
}

void TransactionView::exportClicked()
{
    if (!model || !model->getOptionsModel()) {
        return;
    }

    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Transaction History"), QString(),
        tr("Comma separated file (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    if (model && model->haveWatchOnly())
        writer.addColumn(tr("Watch-only"), TransactionTableModel::Watchonly);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    writer.addColumn(AokChainUnits::getAmountColumnTitle(model->getOptionsModel()->getDisplayUnit()), 0, TransactionTableModel::FormattedAmountRole);
    if (AreTokensDeployed()) {
        writer.addColumn(tr("Token"), 0, TransactionTableModel::TokenNameRole);
    }
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxIDRole);

    if(!writer.write()) {
        Q_EMIT message(tr("Exporting Failed"), tr("There was an error trying to save the transaction history to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
    }
    else {
        Q_EMIT message(tr("Exporting Successful"), tr("The transaction history was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void TransactionView::showTokens()
{
    if (AreTokensDeployed() && !showingTokens) {
        typeWidget->addItem(tr("Token Issued"), TransactionFilterProxy::TYPE(TransactionRecord::Issue));
        typeWidget->addItem(tr("Token Reissued"), TransactionFilterProxy::TYPE(TransactionRecord::Reissue));
        typeWidget->addItem(tr("Token Received"), TransactionFilterProxy::TYPE(TransactionRecord::TransferFrom));
        typeWidget->addItem(tr("Token Sent"), TransactionFilterProxy::TYPE(TransactionRecord::TransferTo));


        QAction *copyTokenNameAction = new QAction(tr("Copy token name"), this);
        contextMenu->addAction(copyTokenNameAction);
        connect(copyTokenNameAction, SIGNAL(triggered()), this, SLOT(copyTokenName()));

        showingTokens = true;
    }

    assetListTimer->start();
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionView->indexAt(point);
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    // check if transaction can be abandoned, disable context menu action in case it doesn't
    uint256 hash;
    hash.SetHex(selection.at(0).data(TransactionTableModel::TxHashRole).toString().toStdString());
    abandonAction->setEnabled(model->transactionCanBeAbandoned(hash));
//    bumpFeeAction->setEnabled(model->transactionCanBeBumped(hash));

    if(index.isValid())
    {
        contextMenu->popup(transactionView->viewport()->mapToGlobal(point));
    }
}

void TransactionView::abandonTx()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    uint256 hash;
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    hash.SetHex(hashQStr.toStdString());

    // Abandon the wallet transaction over the walletModel
    model->abandonTransaction(hash);

    // Update the table
    model->getTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, false);
}

void TransactionView::bumpFee()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    uint256 hash;
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    hash.SetHex(hashQStr.toStdString());

    // Bump tx fee over the walletModel
    if (model->bumpFee(hash)) {
        // Update the table
        model->getTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, true);
    }
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyTokenName()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TokenNameRole);
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxIDRole);
}

void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHexRole);
}

void TransactionView::copyTxPlainText()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        AddressTableModel *addressBook = model->getAddressTableModel();
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI.
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            EditAddressDialog dlg(
                type == AddressTableModel::Receive
                ? EditAddressDialog::EditReceivingAddress
                : EditAddressDialog::EditSendingAddress, this);
            dlg.setModel(addressBook);
            dlg.loadRow(idx);
            dlg.exec();
        }
        else
        {
            // Add sending address
            EditAddressDialog dlg(EditAddressDialog::NewSendingAddress,
                this);
            dlg.setModel(addressBook);
            dlg.setAddress(address);
            dlg.exec();
        }
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

void TransactionView::openThirdPartyTxUrl(QString url)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

QWidget *TransactionView::createDateRangeWidget()
{
    dateRangeWidget = new QFrame();
    dateRangeWidget->setProperty("role", "plain");
    QHBoxLayout *layout = new QHBoxLayout(dateRangeWidget);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(10);
    layout->addWidget(new QLabel(tr("Range:")));

    dateFrom = new QDateTimeEdit(this);
    dateFrom->setDisplayFormat("yyyy-MM-dd");
    dateFrom->setFont(GUIUtil::monoFont(13));
    dateFrom->setCalendarPopup(true);
    dateFrom->setMinimumWidth(140);
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    layout->addWidget(dateFrom);
    layout->addWidget(new QLabel(tr("to")));

    dateTo = new QDateTimeEdit(this);
    dateTo->setDisplayFormat("yyyy-MM-dd");
    dateTo->setFont(GUIUtil::monoFont(13));
    dateTo->setCalendarPopup(true);
    dateTo->setMinimumWidth(140);
    dateTo->setDate(QDate::currentDate());
    layout->addWidget(dateTo);
    layout->addStretch();

    // Hide by default
    dateRangeWidget->setVisible(false);

    // Notify on change
    connect(dateFrom, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));
    connect(dateTo, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));

    return dateRangeWidget;
}

void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    firstPage(), transactionProxyModel->setDateRange(
            QDateTime(dateFrom->date()),
            QDateTime(dateTo->date()).addDays(1));
}

void TransactionView::gotoPage(int page)
{
    if (!pageProxy)
        return;
    selectedHash.clear();
    pageProxy->setPage(page);
    updatePageControls();
    transactionView->scrollToTop();
}

/** A change of the filter starts at the first page again. */
void TransactionView::firstPage()
{
    if (!pageProxy)
        return;
    selectedHash.clear();
    pageProxy->setPage(0);
}

void TransactionView::pageSizeChosen(int idx)
{
    if (!pageProxy)
        return;
    selectedHash.clear();
    pageProxy->setPageSize(pageSizeWidget->itemData(idx).toInt());
    updatePageControls();
}

/** "1 / 61", the number of matching transactions, and which of the buttons make sense. */
void TransactionView::updatePageControls()
{
    if (!pageProxy)
        return;
    const int page = pageProxy->currentPage(), pages = pageProxy->pageCount();
    pageLabel->setText(QString("%1 / %2").arg(page + 1).arg(pages));
    pageCountLabel->setText(tr("%1 transactions").arg(GUIUtil::withThousands(QString::number(pageProxy->totalRows()))));
    pageFirst->setEnabled(page > 0);
    pagePrevious->setEnabled(page > 0);
    pageNext->setEnabled(page < pages - 1);
    pageLast->setEnabled(page < pages - 1);
}

/** The page is laid out again when a block or a transaction arrives: keep the selected row. */
void TransactionView::rememberSelection()
{
    if (!transactionView->selectionModel())
        return;
    const QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if (!selection.isEmpty())
        selectedHash = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
}

void TransactionView::restoreSelection()
{
    if (selectedHash.isEmpty() || !pageProxy)
        return;
    for (int row = 0; row < pageProxy->rowCount(); ++row) {
        if (pageProxy->index(row, 0).data(TransactionTableModel::TxHashRole).toString() == selectedHash) {
            transactionView->selectRow(row);
            return;
        }
    }
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel || !pageProxy)
        return;
    // the page that has the transaction, then the row on it
    const QModelIndex filtered = transactionProxyModel->mapFromSource(idx);
    if (!filtered.isValid())
        return;
    gotoPage(filtered.row() / pageProxy->rowsPerPage());
    QModelIndex targetIdx = pageProxy->mapFromSource(filtered);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TransactionView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
             GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
             return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// show/hide column Watch-only
void TransactionView::updateWatchOnlyColumn(bool fHaveWatchOnly)
{
    watchOnlyWidget->setVisible(fHaveWatchOnly);
    transactionView->setColumnHidden(TransactionTableModel::Watchonly, !fHaveWatchOnly);
}
