// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "addressbookpage.h"
#include "ui_addressbookpage.h"

#include "addresstablemodel.h"
#include "aokchaingui.h"
#include "csvmodelwriter.h"
#include "editaddressdialog.h"
#include "guiutil.h"
#include "receiverequestdialog.h"
#include "walletmodel.h"
#include "platformstyle.h"

#include <QIcon>
#include <QPushButton>
#include <QLineEdit>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QHelpEvent>
#include <QToolTip>

AddressBookPage::AddressBookPage(const PlatformStyle *platformStyle, Mode _mode, Tabs _tab, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddressBookPage),
    model(0),
    mode(_mode),
    tab(_tab),
    proxyModel(nullptr),
    optionsModel(0),
    embedded(false)
{
    ui->setupUi(this);
    setMinimumSize(760, 520);

    // search above the list, with the button for a new address
    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setSpacing(10);
    searchEdit = new QLineEdit(this);
    searchEdit->setProperty("look", "search");
    searchEdit->setPlaceholderText(tr("Search by label or address"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->addAction(QIcon(GUIUtil::lineIcon("search", 16, GUIUtil::theme().textSecondary, 2.0)), QLineEdit::LeadingPosition);
    searchEdit->setFixedHeight(40);
    topRow->addWidget(searchEdit, 1);
    ui->horizontalLayout->removeWidget(ui->newAddress);
    ui->newAddress->setText(_tab == SendingTab ? tr("Add address") : tr("New address"));
    ui->newAddress->setProperty("primary", true);
    ui->newAddress->setFixedHeight(40);
    topRow->addWidget(ui->newAddress);
    ui->verticalLayout->insertLayout(1, topRow);
    ui->verticalLayout->setSpacing(12);
    ui->labelExplanation->setProperty("role", "secondary");

    // the list: rows instead of a grid, the whole address in the mono font
    ui->tableView->setShowGrid(false);
    ui->tableView->setAlternatingRowColors(false);
    ui->tableView->verticalHeader()->setDefaultSectionSize(42);
    ui->tableView->horizontalHeader()->setHighlightSections(false);
    ui->tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->tableView->setFocusPolicy(Qt::StrongFocus);

    // what can be done with the chosen address
    editButton = new QPushButton(tr("Edit"), this);
    editButton->setToolTip(tr("Change the label of the selected address"));
    qrButton = new QPushButton(tr("QR code"), this);
    qrButton->setToolTip(tr("Show the QR code of the selected address"));
    sendButton = new QPushButton(tr("Send to this address"), this);
    sendButton->setToolTip(tr("Open the send page with the selected address"));
    ui->horizontalLayout->insertWidget(1, editButton);
    ui->horizontalLayout->insertWidget(2, qrButton);
    ui->horizontalLayout->insertWidget(3, sendButton);
    ui->horizontalLayout->setSpacing(8);
    for (QPushButton *button : {editButton, qrButton, sendButton, ui->copyAddress, ui->deleteAddress, ui->exportButton, ui->newAddress})
        button->setAutoDefault(false);
    qrButton->setVisible(_tab == ReceivingTab && _mode == ForEditing);
    sendButton->setVisible(_tab == SendingTab && _mode == ForEditing);
    connect(editButton, SIGNAL(clicked()), this, SLOT(onEditAction()));
    connect(qrButton, SIGNAL(clicked()), this, SLOT(showQrCode()));
    connect(sendButton, SIGNAL(clicked()), this, SLOT(sendToSelected()));

    Q_UNUSED(platformStyle);
    ui->newAddress->setIcon(QIcon(GUIUtil::lineIcon("plus", 14, GUIUtil::theme().onFill, 2.4)));
    ui->copyAddress->setIcon(QIcon(GUIUtil::lineIcon("copy", 14, GUIUtil::theme().text, 2.0)));
    ui->deleteAddress->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon(GUIUtil::lineIcon("download", 14, GUIUtil::theme().text, 2.2)));
    qrButton->setIcon(QIcon(GUIUtil::lineIcon("qr", 14, GUIUtil::theme().text, 2.0)));
    sendButton->setIcon(QIcon(GUIUtil::lineIcon("send", 14, GUIUtil::theme().text, 2.2)));

    switch(mode)
    {
    case ForSelection:
        switch(tab)
        {
        case SendingTab: setWindowTitle(tr("Choose the address to send coins to")); break;
        case ReceivingTab: setWindowTitle(tr("Choose the address to receive coins with")); break;
        }
        connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
        ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->tableView->setFocus();
        ui->closeButton->setText(tr("C&hoose"));
        ui->closeButton->setProperty("primary", true);
        ui->exportButton->hide();
        break;
    case ForEditing:
        switch(tab)
        {
        case SendingTab: setWindowTitle(tr("Sending addresses")); break;
        case ReceivingTab: setWindowTitle(tr("Receiving addresses")); break;
        }
        break;
    }
    switch(tab)
    {
    case SendingTab:
        ui->labelExplanation->setText(tr("These are your AokChain addresses for sending payments. Always check the amount and the receiving address before sending coins."));
        ui->deleteAddress->setVisible(true);
        break;
    case ReceivingTab:
        ui->labelExplanation->setText(tr("These are your AokChain addresses for receiving payments. It is recommended to use a new receiving address for each transaction."));
        ui->deleteAddress->setVisible(false);
        break;
    }

    // Context menu actions
    QAction *copyAddressAction = new QAction(tr("&Copy Address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy &Label"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);
    deleteAction = new QAction(ui->deleteAddress->text(), this);

    // Build context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(editAction);
    if(tab == SendingTab)
        contextMenu->addAction(deleteAction);
    contextMenu->addSeparator();

    // Connect signals for context menu actions
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(on_copyAddress_clicked()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(onCopyLabelAction()));
    connect(editAction, SIGNAL(triggered()), this, SLOT(onEditAction()));
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(on_deleteAddress_clicked()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(accept()));
}

/** The addresses of one kind (sending or receiving) that match the search text in label or address. */
class AddressBookFilterProxy : public QSortFilterProxyModel
{
public:
    AddressBookFilterProxy(const QString &_type, QObject *parent) : QSortFilterProxyModel(parent), type(_type) {}

    void setSearch(const QString &text)
    {
        search = text.trimmed();
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QAbstractItemModel *source = sourceModel();
        const QModelIndex label = source->index(row, AddressTableModel::Label, parent);
        if (label.data(AddressTableModel::TypeRole).toString() != type)
            return false;
        if (search.isEmpty())
            return true;
        const QModelIndex address = source->index(row, AddressTableModel::Address, parent);
        return label.data(Qt::EditRole).toString().contains(search, Qt::CaseInsensitive)
               || address.data(Qt::EditRole).toString().contains(search, Qt::CaseInsensitive);
    }

private:
    QString type;
    QString search;
};

/** The address as 4…4 in the mono font; the whole address is the tool tip. */
class ShortAddressDelegate : public QStyledItemDelegate
{
public:
    explicit ShortAddressDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (event->type() == QEvent::ToolTip && index.isValid()) {
            QToolTip::showText(event->globalPos(), GUIUtil::monoToolTip(index.data(Qt::EditRole).toString()), view);
            return true;
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text = GUIUtil::shortAddress(index.data(Qt::EditRole).toString());
        option->font = GUIUtil::monoFont(13);
    }
};

void AddressBookPage::setCompact()
{
    // one of two lists side by side: short addresses, short button texts, the explanation as a tool tip
    ui->tableView->setItemDelegateForColumn(AddressTableModel::Address, new ShortAddressDelegate(this));
    ui->labelExplanation->hide();
    searchEdit->setToolTip(ui->labelExplanation->text());
    searchEdit->setPlaceholderText(tr("Search"));
    searchEdit->setMinimumWidth(60);
    sendButton->setText(tr("Send"));
    ui->copyAddress->setText(tr("Copy"));
    ui->newAddress->setText(tab == SendingTab ? tr("Add") : tr("New"));
    // the export as an icon at the end of the row
    ui->exportButton->setText(QString());
    ui->exportButton->setFixedWidth(40);
    for (QPushButton *button : {editButton, qrButton, sendButton, ui->copyAddress, ui->deleteAddress}) {
        button->setIcon(QIcon());
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        button->setStyleSheet("padding-left: 10px; padding-right: 10px;");
    }
    ui->horizontalLayout->setSpacing(6);
    ui->tableView->setMinimumHeight(120);
}

int AddressBookPage::addressCount() const
{
    return proxyModel ? proxyModel->rowCount() : 0;
}

void AddressBookPage::embedAsPage()
{
    embedded = true;
    setWindowFlags(Qt::Widget);
    setMinimumSize(0, 0);
    // the card of the page is the background
    setAttribute(Qt::WA_StyledBackground, false);
    setStyleSheet(QString("#%1 { background: transparent; }").arg(objectName()));
    if (layout())
        layout()->setContentsMargins(0, 0, 0, 0);
    ui->closeButton->hide();
    ui->tableView->setFrameShape(QFrame::NoFrame);
    ui->tableView->setStyleSheet("QTableView { border: none; border-radius: 0; }");
}

void AddressBookPage::setOptionsModel(OptionsModel *_optionsModel)
{
    optionsModel = _optionsModel;
}

QString AddressBookPage::selectedAddress(int column) const
{
    if (!ui->tableView->selectionModel())
        return QString();
    const QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows(column);
    return indexes.isEmpty() ? QString() : indexes.at(0).data(Qt::EditRole).toString();
}

void AddressBookPage::showQrCode()
{
    const QString address = selectedAddress(AddressTableModel::Address);
    if (address.isEmpty())
        return;
    SendCoinsRecipient info;
    info.address = address;
    info.label = selectedAddress(AddressTableModel::Label);
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModel(optionsModel);
    dialog->setInfo(info);
    dialog->show();
}

void AddressBookPage::sendToSelected()
{
    const QString address = selectedAddress(AddressTableModel::Address);
    if (!address.isEmpty())
        Q_EMIT sendCoins(address);
}

AddressBookPage::~AddressBookPage()
{
    delete ui;
}

void AddressBookPage::setModel(AddressTableModel *_model)
{
    this->model = _model;
    if(!_model)
        return;

    AddressBookFilterProxy *filter = new AddressBookFilterProxy(tab == ReceivingTab ? AddressTableModel::Receive : AddressTableModel::Send, this);
    proxyModel = filter;
    proxyModel->setSourceModel(_model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    connect(searchEdit, &QLineEdit::textChanged, this, [this, filter](const QString &text) {
        filter->setSearch(text);
        selectionChanged();
    });
    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);
    for (const char *change : {SIGNAL(rowsInserted(QModelIndex,int,int)), SIGNAL(rowsRemoved(QModelIndex,int,int)), SIGNAL(modelReset()), SIGNAL(layoutChanged())})
        connect(proxyModel, change, this, SIGNAL(countChanged()));
    Q_EMIT countChanged();

    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(AddressTableModel::Label, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(AddressTableModel::Address, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Label, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Address, QHeaderView::ResizeToContents);
#endif

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
        this, SLOT(selectionChanged()));

    // Select row for newly created address
    connect(_model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewAddress(QModelIndex,int,int)));

    selectionChanged();
}

void AddressBookPage::on_copyAddress_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Address);
}

void AddressBookPage::onCopyLabelAction()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Label);
}

void AddressBookPage::onEditAction()
{
    if(!model)
        return;

    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditAddressDialog dlg(
        tab == SendingTab ?
        EditAddressDialog::EditSendingAddress :
        EditAddressDialog::EditReceivingAddress, this);
    dlg.setModel(model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}

void AddressBookPage::on_newAddress_clicked()
{
    if(!model)
        return;

    EditAddressDialog dlg(
        tab == SendingTab ?
        EditAddressDialog::NewSendingAddress :
        EditAddressDialog::NewReceivingAddress, this);
    dlg.setModel(model);
    if(dlg.exec())
    {
        newAddressToSelect = dlg.getAddress();
    }
}

void AddressBookPage::on_deleteAddress_clicked()
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    QModelIndexList indexes = table->selectionModel()->selectedRows();
    if(!indexes.isEmpty())
    {
        table->model()->removeRow(indexes.at(0).row());
    }
}

void AddressBookPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        switch(tab)
        {
        case SendingTab:
            // In sending tab, allow deletion of selection
            ui->deleteAddress->setEnabled(true);
            ui->deleteAddress->setVisible(true);
            deleteAction->setEnabled(true);
            break;
        case ReceivingTab:
            // Deleting receiving addresses, however, is not allowed
            ui->deleteAddress->setEnabled(false);
            ui->deleteAddress->setVisible(false);
            deleteAction->setEnabled(false);
            break;
        }
        ui->copyAddress->setEnabled(true);
    }
    else
    {
        ui->deleteAddress->setEnabled(false);
        ui->copyAddress->setEnabled(false);
    }
    const bool chosen = table->selectionModel()->hasSelection();
    editButton->setEnabled(chosen);
    qrButton->setEnabled(chosen);
    sendButton->setEnabled(chosen);
}

void AddressBookPage::done(int retval)
{
    // inside a page there is nothing to close
    if (embedded)
        return;

    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;

    // Figure out which address was selected, and return it
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    for (const QModelIndex& index : indexes) {
        QVariant address = table->model()->data(index);
        returnValue = address.toString();
    }

    if(returnValue.isEmpty())
    {
        // If no address entry selected, return rejected
        retval = Rejected;
    }

    QDialog::done(retval);
}

void AddressBookPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Address List"), QString(),
        tr("Comma separated file (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Label", AddressTableModel::Label, Qt::EditRole);
    writer.addColumn("Address", AddressTableModel::Address, Qt::EditRole);

    if(!writer.write()) {
        QMessageBox::critical(this, tr("Exporting Failed"),
            tr("There was an error trying to save the address list to %1. Please try again.").arg(filename));
    }
}

void AddressBookPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void AddressBookPage::selectNewAddress(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, AddressTableModel::Address, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newAddressToSelect))
    {
        // Select row of newly created address, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newAddressToSelect.clear();
    }
}
