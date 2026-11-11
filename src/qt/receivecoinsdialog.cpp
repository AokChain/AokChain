// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "receivecoinsdialog.h"
#include "ui_receivecoinsdialog.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "aokchainunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "walletmodel.h"
#include "guiconstants.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>

ReceiveCoinsDialog::ReceiveCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReceiveCoinsDialog),
    columnResizingFixer(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);
    ui->receiveButton->setProperty("primary", true);

    if (!_platformStyle->getImagesOnButtons()) {
        ui->clearButton->setIcon(QIcon());
        ui->receiveButton->setIcon(QIcon());
        ui->showRequestButton->setIcon(QIcon());
        ui->removeRequestButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->receiveButton->setIcon(_platformStyle->SingleColorIcon(":/icons/receiving_addresses"));
        ui->showRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/edit"));
        ui->removeRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
    }

    // context menu actions
    QAction *copyURIAction = new QAction(tr("Copy URI"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyMessageAction = new QAction(tr("Copy message"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyURIAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyMessageAction);
    contextMenu->addAction(copyAmountAction);

    // context menu signals
    connect(ui->recentRequestsView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyURIAction, SIGNAL(triggered()), this, SLOT(copyURI()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyMessageAction, SIGNAL(triggered()), this, SLOT(copyMessage()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    setupRequestFrame(platformStyle);
    setupHistoryFrame(platformStyle);
    buildLayout();
}

/**
 * Top: the form for a payment request and, next to it, the address of the
 * request with its QR code. Below: the requests made so far.
 */
void ReceiveCoinsDialog::buildLayout()
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    // the layouts of the form go; the widgets stay
    delete layout();
    delete ui->frame2->layout();
    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(20);
    QHBoxLayout *top = new QHBoxLayout();
    top->setSpacing(20);

    // form
    ui->frame2->setStyleSheet(QString());
    ui->frame2->setProperty("role", "card");
    ui->frame2->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout *form = new QVBoxLayout(ui->frame2);
    form->setContentsMargins(24, 20, 24, 20);
    form->setSpacing(8);
    QHBoxLayout *formHeader = new QHBoxLayout();
    formHeader->setSpacing(8);
    QLabel *formTitle = new QLabel(tr("New payment request"), ui->frame2);
    formTitle->setProperty("role", "title");
    formTitle->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    QLabel *optional = new QLabel(tr("optional"), ui->frame2);
    optional->setProperty("role", "tag");
    optional->setToolTip(tr("All fields are optional. Without them you get a plain receiving address."));
    formHeader->addWidget(formTitle);
    formHeader->addWidget(optional);
    formHeader->addStretch();
    form->addLayout(formHeader);
    form->addSpacing(6);
    ui->label_5->hide();
    ui->label_4->hide();
    ui->label_7->hide();
    ui->label_2->setText(tr("&Label"));
    ui->label->setText(tr("&Amount"));
    ui->label_3->setText(tr("&Message"));
    for (QLabel *label : {ui->label_2, ui->label, ui->label_3}) {
        label->setProperty("role", "fieldLabel");
        label->setStyleSheet(QString());
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    ui->reqLabel->setFixedHeight(44);
    ui->reqMessage->setFixedHeight(44);
    ui->reqLabel->setPlaceholderText(tr("Who or what the payment is for"));
    ui->reqMessage->setPlaceholderText(tr("Shown to the payer when the request is opened"));
    // label and amount share a row: the label takes what the amount leaves
    QHBoxLayout *firstRow = new QHBoxLayout();
    firstRow->setSpacing(16);
    QVBoxLayout *labelColumn = new QVBoxLayout();
    labelColumn->setSpacing(8);
    labelColumn->addWidget(ui->label_2);
    labelColumn->addWidget(ui->reqLabel);
    QVBoxLayout *amountColumn = new QVBoxLayout();
    amountColumn->setSpacing(8);
    amountColumn->addWidget(ui->label);
    ui->reqAmount->setFixedHeight(44);
    ui->reqAmount->setFixedWidth(220);
    amountColumn->addWidget(ui->reqAmount);
    firstRow->addLayout(labelColumn, 1);
    firstRow->addLayout(amountColumn);
    form->addLayout(firstRow);
    form->addSpacing(4);
    form->addWidget(ui->label_3);
    form->addWidget(ui->reqMessage);
    form->addSpacing(8);

    // reusing an address: a secondary choice, explained in plain words
    ui->reuseAddress->setText(tr("R&euse an address I already gave to someone"));
    ui->reuseAddress->setStyleSheet(QString());
    form->addWidget(ui->reuseAddress);
    QLabel *reuseHint = new QLabel(tr("Normally leave this off: a new address for every request is safer for your privacy. "
                                      "Turn it on only when you want to receive again on an address that someone already has."), ui->frame2);
    reuseHint->setProperty("role", "caption");
    reuseHint->setFont(GUIUtil::appFont(12));
    reuseHint->setWordWrap(true);
    reuseHint->setContentsMargins(24, 0, 0, 0);
    form->addWidget(reuseHint);
    form->addStretch(1);
    QHBoxLayout *formButtons = new QHBoxLayout();
    formButtons->setSpacing(10);
    ui->receiveButton->setIcon(QIcon());
    ui->receiveButton->setProperty("look", "large");
    ui->receiveButton->setFixedHeight(46);
    ui->receiveButton->setCursor(Qt::PointingHandCursor);
    ui->clearButton->setIcon(QIcon());
    ui->clearButton->setProperty("look", "ghost");
    ui->clearButton->setFixedHeight(46);
    formButtons->addWidget(ui->receiveButton);
    formButtons->addWidget(ui->clearButton);
    formButtons->addStretch();
    form->addLayout(formButtons);
    top->addWidget(ui->frame2, 3);

    // address and QR code of the request
    QFrame *qrCard = new QFrame(this);
    qrCard->setProperty("role", "card");
    QVBoxLayout *qr = new QVBoxLayout(qrCard);
    qr->setContentsMargins(24, 20, 24, 20);
    qr->setSpacing(10);
    QLabel *qrTitle = new QLabel(tr("Receiving address"), qrCard);
    qrTitle->setProperty("role", "title");
    qrTitle->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    qr->addWidget(qrTitle);
    qr->addStretch(1);
    qrImage = new QLabel(qrCard);
    qrImage->setFixedSize(188, 188);
    qrImage->setAlignment(Qt::AlignCenter);
    qrImage->setStyleSheet(QString("QLabel { background-color: #FFFFFF; border: 1px solid %1; border-radius: 12px; }").arg(theme.border.name()));
    qr->addWidget(qrImage, 0, Qt::AlignHCenter);
    qrPlaceholder = new QLabel(tr("Press \"Request payment\" to get an address. It is shown here with its QR code."), qrCard);
    qrPlaceholder->setProperty("role", "secondary");
    qrPlaceholder->setWordWrap(true);
    qrPlaceholder->setAlignment(Qt::AlignCenter);
    qr->addWidget(qrPlaceholder);
    qrAddress = new QLabel(qrCard);
    qrAddress->setFont(GUIUtil::monoFont(13, QFont::Medium));
    qrAddress->setAlignment(Qt::AlignCenter);
    qrAddress->setWordWrap(true);
    qrAddress->setTextInteractionFlags(Qt::TextSelectableByMouse);
    qr->addWidget(qrAddress);
    qrDetails = new QLabel(qrCard);
    qrDetails->setProperty("role", "caption");
    qrDetails->setFont(GUIUtil::appFont(12));
    qrDetails->setAlignment(Qt::AlignCenter);
    qrDetails->setWordWrap(true);
    qr->addWidget(qrDetails);
    qr->addStretch(1);
    QHBoxLayout *qrButtons = new QHBoxLayout();
    qrButtons->setSpacing(10);
    copyQrButton = new QPushButton(tr("Copy QR code"), qrCard);
    copyQrButton->setIcon(QIcon(GUIUtil::lineIcon("qr", 15, theme.label)));
    copyAddressButton = new QPushButton(tr("Copy address"), qrCard);
    copyAddressButton->setIcon(QIcon(GUIUtil::lineIcon("copy", 15, theme.onFill)));
    copyAddressButton->setProperty("primary", true);
    for (QPushButton *button : {copyQrButton, copyAddressButton}) {
        button->setIconSize(QSize(15, 15));
        button->setFixedHeight(42);
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(false);
        qrButtons->addWidget(button, 1);
    }
    qr->addLayout(qrButtons);
    connect(copyQrButton, SIGNAL(clicked()), this, SLOT(copyQrImage()));
    connect(copyAddressButton, SIGNAL(clicked()), this, SLOT(copyShownAddress()));
    top->addWidget(qrCard, 2);
    page->addLayout(top);

    // history
    ui->frame->setStyleSheet(QString());
    ui->frame->setProperty("role", "card");
    ui->label_6->setText(tr("Request history"));
    ui->label_6->setProperty("role", "title");
    ui->label_6->setStyleSheet(QString());
    ui->label_6->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    if (ui->frame->layout())
        ui->frame->layout()->setContentsMargins(24, 18, 24, 16);
    ui->recentRequestsView->setFrameShape(QFrame::NoFrame);
    ui->recentRequestsView->setStyleSheet("QTableView { border: none; }");
    // a style sheet on the view makes Windows fall back to a system font for the header: name it again
    ui->recentRequestsView->setFont(GUIUtil::getSubLabelFont());
    ui->recentRequestsView->horizontalHeader()->setFont(GUIUtil::appFont(12, QFont::Medium));
    ui->recentRequestsView->setShowGrid(false);
    ui->recentRequestsView->setMinimumHeight(90);
    page->addWidget(ui->frame, 1);

    for (QPushButton *button : findChildren<QPushButton*>())
        button->setAutoDefault(false);
}

/** Show a request on the page: QR code of its URI, the address in full, label, amount and message. */
void ReceiveCoinsDialog::showRequest(const SendCoinsRecipient &info)
{
    shownAddress = info.address;
    const QImage image = ReceiveRequestDialog::qrCodeImage(GUIUtil::formatAokChainURI(info), 172);
    if (image.isNull()) {
        qrImage->setPixmap(QPixmap());
        qrImage->setText(tr("No QR code"));
    } else {
        qrImage->setPixmap(QPixmap::fromImage(image));
    }
    qrPlaceholder->hide();
    qrAddress->setText(info.address);
    QStringList details;
    if (!info.label.isEmpty())
        details << info.label;
    if (info.amount > 0 && model && model->getOptionsModel())
        details << AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), info.amount);
    if (!info.message.isEmpty())
        details << info.message;
    qrDetails->setText(details.join(QString::fromUtf8(" · ")));
    qrDetails->setVisible(!details.isEmpty());
    copyQrButton->setEnabled(!image.isNull());
    copyAddressButton->setEnabled(true);
}

void ReceiveCoinsDialog::copyQrImage()
{
    if (qrImage->pixmap() && !qrImage->pixmap()->isNull())
        QApplication::clipboard()->setImage(qrImage->pixmap()->toImage());
}

void ReceiveCoinsDialog::copyShownAddress()
{
    if (!shownAddress.isEmpty())
        GUIUtil::setClipboard(shownAddress);
}

void ReceiveCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        _model->getRecentRequestsTableModel()->sort(RecentRequestsTableModel::Date, Qt::DescendingOrder);
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        QTableView* tableView = ui->recentRequestsView;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tableView->setModel(_model->getRecentRequestsTableModel());
        // dates and amounts in the fixed-width font
        tableView->setItemDelegate(new GUIUtil::MonoColumnDelegate({RecentRequestsTableModel::Date}, this, {RecentRequestsTableModel::Amount}));
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

        connect(tableView->selectionModel(),
            SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this,
            SLOT(recentRequestsView_selectionChanged(QItemSelection, QItemSelection)));
        // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, AMOUNT_MINIMUM_COLUMN_WIDTH, DATE_COLUMN_WIDTH, this);
        tableView->show();

        // the latest request, if there is one, is shown with its QR code
        if (_model->getRecentRequestsTableModel()->rowCount(QModelIndex()) > 0)
            showRequest(_model->getRecentRequestsTableModel()->entry(0).recipient);
    }
}

ReceiveCoinsDialog::~ReceiveCoinsDialog()
{
    delete ui;
}

void ReceiveCoinsDialog::clear()
{
    ui->reqAmount->clear();
    ui->reqLabel->setText("");
    ui->reqMessage->setText("");
    ui->reuseAddress->setChecked(false);
    updateDisplayUnit();
}

void ReceiveCoinsDialog::reject()
{
    clear();
}

void ReceiveCoinsDialog::accept()
{
    clear();
}

void ReceiveCoinsDialog::setupRequestFrame(const PlatformStyle *platformStyle)
{
    /** Update the coincontrol frame */
    ui->frame2->setStyleSheet(GUIUtil::cardFrameStyle());
    /** Create the shadow effects on the frames */

    ui->label_5->setStyleSheet(STRING_LABEL_COLOR);

    ui->label_2->setStyleSheet(STRING_LABEL_COLOR);
    ui->label_2->setFont(GUIUtil::getSubLabelFont());

    ui->label->setStyleSheet(STRING_LABEL_COLOR);
    ui->label->setFont(GUIUtil::getSubLabelFont());

    ui->label_3->setStyleSheet(STRING_LABEL_COLOR);
    ui->label_3->setFont(GUIUtil::getSubLabelFont());

    ui->label_4->setStyleSheet(STRING_LABEL_COLOR);
    ui->label_4->setFont(GUIUtil::getSubLabelFont());

    ui->label_7->setStyleSheet(STRING_LABEL_COLOR);
    ui->label_7->setFont(GUIUtil::getSubLabelFont());

    ui->reuseAddress->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));
    ui->reqLabel->setFont(GUIUtil::getSubLabelFont());
    ui->reqAmount->setFont(GUIUtil::getSubLabelFont());
    ui->reqMessage->setFont(GUIUtil::getSubLabelFont());
    ui->receiveButton->setFont(GUIUtil::getSubLabelFont());
    ui->clearButton->setFont(GUIUtil::getSubLabelFont());
    ui->recentRequestsView->setFont(GUIUtil::getSubLabelFont());
    ui->showRequestButton->setFont(GUIUtil::getSubLabelFont());
    ui->removeRequestButton->setFont(GUIUtil::getSubLabelFont());
    ui->label_5->setFont(GUIUtil::getSubLabelFont());

    ui->label_6->setFont(GUIUtil::getSubLabelFontBolded());
}

void ReceiveCoinsDialog::setupHistoryFrame(const PlatformStyle *platformStyle)
{
    /** Update the coincontrol frame */
    ui->frame->setStyleSheet(GUIUtil::cardFrameStyle());
    /** Create the shadow effects on the frames */

    ui->label_6->setStyleSheet(STRING_LABEL_COLOR);

    contextMenu->setFont(GUIUtil::getSubLabelFont());

}

void ReceiveCoinsDialog::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        ui->reqAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void ReceiveCoinsDialog::on_receiveButton_clicked()
{
    if(!model || !model->getOptionsModel() || !model->getAddressTableModel() || !model->getRecentRequestsTableModel())
        return;

    QString address;
    QString label = ui->reqLabel->text();
    if(ui->reuseAddress->isChecked())
    {
        /* Choose existing receiving address */
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if(dlg.exec())
        {
            address = dlg.getReturnValue();
            if(label.isEmpty()) /* If no label provided, use the previously used label */
            {
                label = model->getAddressTableModel()->labelForAddress(address);
            }
        } else {
            return;
        }
    } else {
        /* Generate new receiving address */
        address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, label, "");
    }
    SendCoinsRecipient info(address, label,
        ui->reqAmount->value(), ui->reqMessage->text());
    // The request is shown on the page with its QR code. The dialog with the URI
    // and "save image" opens from the history ("Show" or a double click).
    showRequest(info);
    clear();

    /* Store request for later reference */
    model->getRecentRequestsTableModel()->addNewRequest(info);
}

void ReceiveCoinsDialog::on_recentRequestsView_doubleClicked(const QModelIndex &index)
{
    const RecentRequestsTableModel *submodel = model->getRecentRequestsTableModel();
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setModel(model->getOptionsModel());
    dialog->setInfo(submodel->entry(index.row()).recipient);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ReceiveCoinsDialog::recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    // the selected request is the one shown with its QR code
    if (model && model->getRecentRequestsTableModel()) {
        const QModelIndexList rows = ui->recentRequestsView->selectionModel()->selectedRows();
        if (!rows.isEmpty())
            showRequest(model->getRecentRequestsTableModel()->entry(rows.first().row()).recipient);
    }
    // Enable Show/Remove buttons only if anything is selected.
    bool enable = !ui->recentRequestsView->selectionModel()->selectedRows().isEmpty();
    ui->showRequestButton->setEnabled(enable);
    ui->removeRequestButton->setEnabled(enable);
}

void ReceiveCoinsDialog::on_showRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();

    for (const QModelIndex& index : selection) {
        on_recentRequestsView_doubleClicked(index);
    }
}

void ReceiveCoinsDialog::on_removeRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return;
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    model->getRecentRequestsTableModel()->removeRows(firstIndex.row(), selection.length(), firstIndex.parent());
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void ReceiveCoinsDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(RecentRequestsTableModel::Message);
}

void ReceiveCoinsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return)
    {
        // press return -> submit form
        if (ui->reqLabel->hasFocus() || ui->reqAmount->hasFocus() || ui->reqMessage->hasFocus())
        {
            event->ignore();
            on_receiveButton_clicked();
            return;
        }
    }

    this->QDialog::keyPressEvent(event);
}

QModelIndex ReceiveCoinsDialog::selectedRow()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return QModelIndex();
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return QModelIndex();
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    return firstIndex;
}

// copy column of selected row to clipboard
void ReceiveCoinsDialog::copyColumnToClipboard(int column)
{
    QModelIndex firstIndex = selectedRow();
    if (!firstIndex.isValid()) {
        return;
    }
    GUIUtil::setClipboard(model->getRecentRequestsTableModel()->data(firstIndex.child(firstIndex.row(), column), Qt::EditRole).toString());
}

// context menu
void ReceiveCoinsDialog::showMenu(const QPoint &point)
{
    if (!selectedRow().isValid()) {
        return;
    }
    contextMenu->exec(QCursor::pos());
}

// context menu action: copy URI
void ReceiveCoinsDialog::copyURI()
{
    QModelIndex sel = selectedRow();
    if (!sel.isValid()) {
        return;
    }

    const RecentRequestsTableModel * const submodel = model->getRecentRequestsTableModel();
    const QString uri = GUIUtil::formatAokChainURI(submodel->entry(sel.row()).recipient);
    GUIUtil::setClipboard(uri);
}

// context menu action: copy label
void ReceiveCoinsDialog::copyLabel()
{
    copyColumnToClipboard(RecentRequestsTableModel::Label);
}

// context menu action: copy message
void ReceiveCoinsDialog::copyMessage()
{
    copyColumnToClipboard(RecentRequestsTableModel::Message);
}

// context menu action: copy amount
void ReceiveCoinsDialog::copyAmount()
{
    copyColumnToClipboard(RecentRequestsTableModel::Amount);
}
