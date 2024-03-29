// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokencontroldialog.h"
#include "ui_tokencontroldialog.h"

#include "addresstablemodel.h"
#include "aokchainunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"

#include "wallet/coincontrol.h"
#include "init.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "validation.h" // For mempool
#include "wallet/fees.h"
#include "wallet/wallet.h"

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>
#include <QLineEdit>

QList<CAmount> TokenControlDialog::payAmounts;
CCoinControl* TokenControlDialog::tokenControl = new CCoinControl();
bool TokenControlDialog::fSubtractFeeFromAmount = false;

bool CTokenControlWidgetItem::operator<(const QTreeWidgetItem &other) const {
    int column = treeWidget()->sortColumn();
    if (column == TokenControlDialog::COLUMN_AMOUNT || column == TokenControlDialog::COLUMN_DATE || column == TokenControlDialog::COLUMN_CONFIRMATIONS)
        return data(column, Qt::UserRole).toLongLong() < other.data(column, Qt::UserRole).toLongLong();
    return QTreeWidgetItem::operator<(other);
}

TokenControlDialog::TokenControlDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TokenControlDialog),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    // context menu actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
             copyTransactionHashAction = new QAction(tr("Copy transaction ID"), this);  // we need to enable/disable this
             lockAction = new QAction(tr("Lock unspent"), this);                        // we need to enable/disable this
             unlockAction = new QAction(tr("Unlock unspent"), this);                    // we need to enable/disable this

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTransactionHashAction);
    contextMenu->addSeparator();
    contextMenu->addAction(lockAction);
    contextMenu->addAction(unlockAction);

    // context menu signals
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTransactionHashAction, SIGNAL(triggered()), this, SLOT(copyTransactionHash()));
    connect(lockAction, SIGNAL(triggered()), this, SLOT(lockCoin()));
    connect(unlockAction, SIGNAL(triggered()), this, SLOT(unlockCoin()));

    // clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);

    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(clipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(clipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(clipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(clipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(clipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(clipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(clipboardChange()));

    ui->labelTokenControlQuantity->addAction(clipboardQuantityAction);
    ui->labelTokenControlAmount->addAction(clipboardAmountAction);
    ui->labelTokenControlFee->addAction(clipboardFeeAction);
    ui->labelTokenControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelTokenControlBytes->addAction(clipboardBytesAction);
    ui->labelTokenControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelTokenControlChange->addAction(clipboardChangeAction);

    // toggle tree/list mode
    connect(ui->radioTreeMode, SIGNAL(toggled(bool)), this, SLOT(radioTreeMode(bool)));
    connect(ui->radioListMode, SIGNAL(toggled(bool)), this, SLOT(radioListMode(bool)));

    // click on checkbox
    connect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(viewItemChanged(QTreeWidgetItem*, int)));

    // click on header
#if QT_VERSION < 0x050000
    ui->treeWidget->header()->setClickable(true);
#else
    ui->treeWidget->header()->setSectionsClickable(true);
#endif
    connect(ui->treeWidget->header(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    // ok button
    connect(ui->buttonBox, SIGNAL(clicked( QAbstractButton*)), this, SLOT(buttonBoxClicked(QAbstractButton*)));

    // (un)select all
    connect(ui->pushButtonSelectAll, SIGNAL(clicked()), this, SLOT(buttonSelectAllClicked()));

    // change coin control first column label due Qt4 bug.
    // see https://github.com/AokChain/AokChain/issues/5716
    ui->treeWidget->headerItem()->setText(COLUMN_CHECKBOX, QString());

    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 110);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 190);
    ui->treeWidget->setColumnWidth(COLUMN_ADDRESS, 320);
    ui->treeWidget->setColumnWidth(COLUMN_DATE, 130);
    ui->treeWidget->setColumnWidth(COLUMN_CONFIRMATIONS, 110);
    ui->treeWidget->setColumnHidden(COLUMN_TXHASH, true);         // store transaction hash in this column, but don't show it
    ui->treeWidget->setColumnHidden(COLUMN_VOUT_INDEX, true);     // store vout index in this column, but don't show it

    // default view is sorted by amount desc
    sortView(COLUMN_AMOUNT, Qt::DescendingOrder);

    // restore list mode and sortorder as a convenience feature
    QSettings settings;
    if (settings.contains("nCoinControlMode") && !settings.value("nCoinControlMode").toBool())
        ui->radioTreeMode->click();
    if (settings.contains("nCoinControlSortColumn") && settings.contains("nCoinControlSortOrder"))
        sortView(settings.value("nCoinControlSortColumn").toInt(), (static_cast<Qt::SortOrder>(settings.value("nCoinControlSortOrder").toInt())));

    // Add the tokens into the dropdown menu
    connect(ui->viewAdministrator, SIGNAL(clicked()), this, SLOT(viewAdministratorClicked()));
    connect(ui->tokenList, SIGNAL(currentIndexChanged(QString)), this, SLOT(onTokenSelected(QString)));

    /** Setup the token list combobox */
    stringModel = new QStringListModel;

    proxy = new QSortFilterProxyModel;
    proxy->setSourceModel(stringModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tokenList->setModel(proxy);
    ui->tokenList->setEditable(true);
    ui->tokenList->lineEdit()->setPlaceholderText("Select an token");

    completer = new QCompleter(proxy,this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->tokenList->setCompleter(completer);
}

TokenControlDialog::~TokenControlDialog()
{
    QSettings settings;
    settings.setValue("nCoinControlMode", ui->radioListMode->isChecked());
    settings.setValue("nCoinControlSortColumn", sortColumn);
    settings.setValue("nCoinControlSortOrder", (int)sortOrder);

    delete ui;
}

void TokenControlDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel() && _model->getAddressTableModel())
    {
        updateView();
        updateTokenList(true);
        updateLabelLocked();
        TokenControlDialog::updateLabels(_model, this);
    }
}

// ok button
void TokenControlDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole) {
        if (TokenControlDialog::tokenControl->HasTokenSelected())
            TokenControlDialog::tokenControl->strTokenSelected = ui->tokenList->currentText().toStdString();
        done(QDialog::Accepted); // closes the dialog
    }
}

// (un)select all
void TokenControlDialog::buttonSelectAllClicked()
{
    Qt::CheckState state = Qt::Checked;
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
    {
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != Qt::Unchecked)
        {
            state = Qt::Unchecked;
            break;
        }
    }
    ui->treeWidget->setEnabled(false);
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != state)
                ui->treeWidget->topLevelItem(i)->setCheckState(COLUMN_CHECKBOX, state);
    ui->treeWidget->setEnabled(true);
    if (state == Qt::Unchecked)
        tokenControl->UnSelectAll(); // just to be sure
    TokenControlDialog::updateLabels(model, this);
}

// context menu
void TokenControlDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;

        // disable some items (like Copy Transaction ID, lock, unlock) for tree roots in context menu
        if (item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree mode)
        {
            copyTransactionHashAction->setEnabled(true);
            if (model->isLockedCoin(uint256S(item->text(COLUMN_TXHASH).toStdString()), item->text(COLUMN_VOUT_INDEX).toUInt()))
            {
                lockAction->setEnabled(false);
                unlockAction->setEnabled(true);
            }
            else
            {
                lockAction->setEnabled(true);
                unlockAction->setEnabled(false);
            }
        }
        else // this means click on parent node in tree mode -> disable all
        {
            copyTransactionHashAction->setEnabled(false);
            lockAction->setEnabled(false);
            unlockAction->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void TokenControlDialog::copyAmount()
{
    GUIUtil::setClipboard(AokChainUnits::removeSpaces(contextMenuItem->text(COLUMN_AMOUNT)));
}

// context menu action: copy label
void TokenControlDialog::copyLabel()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_LABEL).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_LABEL));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_LABEL));
}

// context menu action: copy address
void TokenControlDialog::copyAddress()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_ADDRESS).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_ADDRESS));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_ADDRESS));
}

// context menu action: copy transaction id
void TokenControlDialog::copyTransactionHash()
{
    GUIUtil::setClipboard(contextMenuItem->text(COLUMN_TXHASH));
}

// context menu action: lock coin
void TokenControlDialog::lockCoin()
{
    if (contextMenuItem->checkState(COLUMN_CHECKBOX) == Qt::Checked)
        contextMenuItem->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

    COutPoint outpt(uint256S(contextMenuItem->text(COLUMN_TXHASH).toStdString()), contextMenuItem->text(COLUMN_VOUT_INDEX).toUInt());
    model->lockCoin(outpt);
    contextMenuItem->setDisabled(true);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, platformStyle->SingleColorIcon(":/icons/lock_closed"));
    updateLabelLocked();
}

// context menu action: unlock coin
void TokenControlDialog::unlockCoin()
{
    COutPoint outpt(uint256S(contextMenuItem->text(COLUMN_TXHASH).toStdString()), contextMenuItem->text(COLUMN_VOUT_INDEX).toUInt());
    model->unlockCoin(outpt);
    contextMenuItem->setDisabled(false);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon());
    updateLabelLocked();
}

// copy label "Quantity" to clipboard
void TokenControlDialog::clipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelTokenControlQuantity->text());
}

// copy label "Amount" to clipboard
void TokenControlDialog::clipboardAmount()
{
    GUIUtil::setClipboard(ui->labelTokenControlAmount->text().left(ui->labelTokenControlAmount->text().indexOf(" ")));
}

// copy label "Fee" to clipboard
void TokenControlDialog::clipboardFee()
{
    GUIUtil::setClipboard(ui->labelTokenControlFee->text().left(ui->labelTokenControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// copy label "After fee" to clipboard
void TokenControlDialog::clipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelTokenControlAfterFee->text().left(ui->labelTokenControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// copy label "Bytes" to clipboard
void TokenControlDialog::clipboardBytes()
{
    GUIUtil::setClipboard(ui->labelTokenControlBytes->text().replace(ASYMP_UTF8, ""));
}

// copy label "Dust" to clipboard
void TokenControlDialog::clipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelTokenControlLowOutput->text());
}

// copy label "Change" to clipboard
void TokenControlDialog::clipboardChange()
{
    GUIUtil::setClipboard(ui->labelTokenControlChange->text().left(ui->labelTokenControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// treeview: sort
void TokenControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

// treeview: clicked on header
void TokenControlDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    }
    else
    {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS) ? Qt::AscendingOrder : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

// toggle tree mode
void TokenControlDialog::radioTreeMode(bool checked)
{
    if (checked && model)
        updateView();
}

// toggle list mode
void TokenControlDialog::radioListMode(bool checked)
{
    if (checked && model)
        updateView();
}

// checkbox clicked by user
void TokenControlDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    if (column == COLUMN_CHECKBOX && item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree mode)
    {
        COutPoint outpt(uint256S(item->text(COLUMN_TXHASH).toStdString()), item->text(COLUMN_VOUT_INDEX).toUInt());

        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked)
            tokenControl->UnSelectToken(outpt);
        else if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else
            tokenControl->SelectToken(outpt);

        // selection changed -> update labels
        if (ui->treeWidget->isEnabled()) // do not update on every click for (un)select all
            TokenControlDialog::updateLabels(model, this);
    }

    // TODO: Remove this temporary qt5 fix after Qt5.3 and Qt5.4 are no longer used.
    //       Fixed in Qt5.5 and above: https://bugreports.qt.io/browse/QTBUG-43473
#if QT_VERSION >= 0x050000
    else if (column == COLUMN_CHECKBOX && item->childCount() > 0)
    {
        if (item->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked && item->child(0)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
    }
#endif
}

// shows count of locked unspent outputs
void TokenControlDialog::updateLabelLocked()
{
    std::vector<COutPoint> vOutpts;
    model->listLockedCoins(vOutpts);
    if (vOutpts.size() > 0)
    {
       ui->labelLocked->setText(tr("(%1 locked)").arg(vOutpts.size()));
       ui->labelLocked->setVisible(true);
    }
    else ui->labelLocked->setVisible(false);
}

void TokenControlDialog::updateLabels(WalletModel *model, QDialog* dialog)
{
    if (!model)
        return;

    // nPayAmount
    CAmount nPayAmount = 0;
    bool fDust = false;
    CMutableTransaction txDummy;
    for (const CAmount &amount : TokenControlDialog::payAmounts)
    {
        nPayAmount += amount;

        if (amount > 0)
        {
            CTxOut txout(amount, static_cast<CScript>(std::vector<unsigned char>(24, 0)));
            txDummy.vout.push_back(txout);
            fDust |= IsDust(txout, ::dustRelayFee);
        }
    }

    std::string strTokenName = "";
    CAmount nTokenAmount        = 0;
    CAmount nAmount             = 0;
    CAmount nPayFee             = 0;
    CAmount nAfterFee           = 0;
    CAmount nChange             = 0;
    unsigned int nBytes         = 0;
    unsigned int nBytesInputs   = 0;
    unsigned int nQuantity      = 0;
    bool fWitness               = false;

    std::vector<COutPoint> vCoinControl;
    std::vector<COutput>   vOutputs;
    tokenControl->ListSelectedTokens(vCoinControl);
    model->getOutputs(vCoinControl, vOutputs);

    for (const COutput& out : vOutputs) {
        // unselect already spent, very unlikely scenario, this could happen
        // when selected are spent elsewhere, like rpc or another computer
        uint256 txhash = out.tx->GetHash();
        COutPoint outpt(txhash, out.i);
        if (model->isSpent(outpt))
        {
            tokenControl->UnSelectToken(outpt);
            continue;
        }

        // Quantity
        nQuantity++;

        // Amount
        CAmount nCoinAmount;
        uint32_t nTokenLockTime;
        GetTokenInfoFromScript(out.tx->tx->vout[out.i].scriptPubKey, strTokenName, nCoinAmount, nTokenLockTime);
        nTokenAmount += nCoinAmount;

        // Bytes
        CTxDestination address;
        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;
        if (out.tx->tx->vout[out.i].scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram))
        {
            nBytesInputs += (32 + 4 + 1 + (107 / WITNESS_SCALE_FACTOR) + 4);
            fWitness = true;
        }
        else if(ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, address))
        {
            CPubKey pubkey;
            CKeyID *keyid = boost::get<CKeyID>(&address);
            if (keyid && model->getPubKey(*keyid, pubkey))
            {
                nBytesInputs += (pubkey.IsCompressed() ? 148 : 180);
            }
            else
                nBytesInputs += 148; // in all error cases, simply assume 148 here
        }
        else nBytesInputs += 148;
    }

    // calculation
    if (nQuantity > 0)
    {
        // Bytes
        nBytes = nBytesInputs + ((TokenControlDialog::payAmounts.size() > 0 ? TokenControlDialog::payAmounts.size() + 1 : 2) * 34) + 10; // always assume +1 output for change here
        if (fWitness)
        {
            // there is some fudging in these numbers related to the actual virtual transaction size calculation that will keep this estimate from being exact.
            // usually, the result will be an overestimate within a couple of satoshis so that the confirmation dialog ends up displaying a slightly smaller fee.
            // also, the witness stack size value is a variable sized integer. usually, the number of stack items will be well under the single byte var int limit.
            nBytes += 2; // account for the serialized marker and flag bytes
            nBytes += nQuantity; // account for the witness byte that holds the number of stack items for each input.
        }

        // in the subtract fee from amount case, we can tell if zero change already and subtract the bytes, so that fee calculation afterwards is accurate
        if (TokenControlDialog::fSubtractFeeFromAmount)
            if (nAmount - nPayAmount == 0)
                nBytes -= 34;

        // Fee
        nPayFee = GetMinimumFee(nBytes, *tokenControl, ::mempool, ::feeEstimator, nullptr /* FeeCalculation */);

        if (nPayAmount > 0)
        {
            nChange = nTokenAmount - nPayAmount;

            if (nChange == 0 && !TokenControlDialog::fSubtractFeeFromAmount)
                nBytes -= 34;
        }

        // after fee
        nAfterFee = std::max<CAmount>(nPayFee, 0);
    }

    // actually update labels
    int nDisplayUnit = AokChainUnits::AOK;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    QLabel *l1 = dialog->findChild<QLabel *>("labelTokenControlQuantity");
    QLabel *l2 = dialog->findChild<QLabel *>("labelTokenControlAmount");
    QLabel *l3 = dialog->findChild<QLabel *>("labelTokenControlFee");
    QLabel *l4 = dialog->findChild<QLabel *>("labelTokenControlAfterFee");
    QLabel *l5 = dialog->findChild<QLabel *>("labelTokenControlBytes");
    QLabel *l7 = dialog->findChild<QLabel *>("labelTokenControlLowOutput");
    QLabel *l8 = dialog->findChild<QLabel *>("labelTokenControlChange");

    // enable/disable "dust" and "change"
    dialog->findChild<QLabel *>("labelTokenControlLowOutputText")->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelTokenControlLowOutput")    ->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelTokenControlChangeText")   ->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelTokenControlChange")       ->setEnabled(nPayAmount > 0);

    // stats
    l1->setText(QString::number(nQuantity));                                 // Quantity
    l2->setText(AokChainUnits::formatWithCustomName(QString::fromStdString(strTokenName), nTokenAmount));        // Amount
    l3->setText(AokChainUnits::formatWithUnit(nDisplayUnit, nPayFee));        // Fee
    l4->setText(AokChainUnits::formatWithUnit(nDisplayUnit, nAfterFee));      // After Fee
    l5->setText(((nBytes > 0) ? ASYMP_UTF8 : "") + QString::number(nBytes));        // Bytes
    l7->setText(fDust ? tr("yes") : tr("no"));                               // Dust
    l8->setText(AokChainUnits::formatWithCustomName(QString::fromStdString(strTokenName), nChange));        // Change
    if (nPayFee > 0)
    {
        l3->setText(ASYMP_UTF8 + l3->text());
        l4->setText(ASYMP_UTF8 + l4->text());
    }

    // turn label red when dust
    l7->setStyleSheet((fDust) ? "color:red;" : "");

    // tool tips
    QString toolTipDust = tr("This label turns red if any recipient receives an amount smaller than the current dust threshold.");

    // how many satoshis the estimated fee can vary per byte we guess wrong
    double dFeeVary = (nBytes != 0) ? (double)nPayFee / nBytes : 0;

    QString toolTip4 = tr("Can vary +/- %1 satoshi(s) per input.").arg(dFeeVary);

    l3->setToolTip(toolTip4);
    l4->setToolTip(toolTip4);
    l7->setToolTip(toolTipDust);
    l8->setToolTip(toolTip4);
    dialog->findChild<QLabel *>("labelTokenControlFeeText")      ->setToolTip(l3->toolTip());
    dialog->findChild<QLabel *>("labelTokenControlAfterFeeText") ->setToolTip(l4->toolTip());
    dialog->findChild<QLabel *>("labelTokenControlBytesText")    ->setToolTip(l5->toolTip());
    dialog->findChild<QLabel *>("labelTokenControlLowOutputText")->setToolTip(l7->toolTip());
    dialog->findChild<QLabel *>("labelTokenControlChangeText")   ->setToolTip(l8->toolTip());

    // Insufficient funds
    QLabel *label = dialog->findChild<QLabel *>("labelTokenControlInsuffFunds");
    if (label)
        label->setVisible(nChange < 0);
}

void TokenControlDialog::updateView()
{
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        return;

    bool treeMode = ui->radioTreeMode->isChecked();

    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false); // performance, otherwise updateLabels would be called for every checked checkbox
    ui->treeWidget->setAlternatingRowColors(!treeMode);
    QFlags<Qt::ItemFlag> flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    QFlags<Qt::ItemFlag> flgTristate = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsTristate;

    int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    std::map<QString, std::map<QString, std::vector<COutput> > > mapCoins;
    model->listTokens(mapCoins);

    QString tokenToDisplay = ui->tokenList->currentText();

    // Double check to make sure that the token selected has coins in the map
    if (!mapCoins.count(tokenToDisplay))
        return;

    // For now we only support for one tokens coins being shown at a time
    // So we only loop through coins for that specific token
    auto mapTokenCoins = mapCoins.at(tokenToDisplay);

    for (const std::pair<QString, std::vector<COutput>> &coins : mapTokenCoins) {
        CTokenControlWidgetItem *itemWalletAddress = new CTokenControlWidgetItem();
        itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        QString sWalletAddress = coins.first;
        QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
        if (sWalletLabel.isEmpty())
            sWalletLabel = tr("(no label)");

        if (treeMode) {
            // wallet address
            ui->treeWidget->addTopLevelItem(itemWalletAddress);

            itemWalletAddress->setFlags(flgTristate);
            itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

            // label
            itemWalletAddress->setText(COLUMN_LABEL, sWalletLabel);

            // address
            itemWalletAddress->setText(COLUMN_ADDRESS, sWalletAddress);

            // token name
            itemWalletAddress->setText(COLUMN_TOKEN_NAME, tokenToDisplay);
        }

        CAmount nSum = 0;
        int nChildren = 0;
        for (const COutput &out : coins.second) {
            std::string strTokenName;
            CAmount nAmount;
            uint32_t nTokenLockTime;
            if (!GetTokenInfoFromScript(out.tx->tx->vout[out.i].scriptPubKey, strTokenName, nAmount, nTokenLockTime))
                continue;

            if (strTokenName != tokenToDisplay.toStdString())
                continue;
            
            nSum += nAmount;
            nChildren++;

            CTokenControlWidgetItem *itemOutput;
            if (treeMode) itemOutput = new CTokenControlWidgetItem(itemWalletAddress);
            else itemOutput = new CTokenControlWidgetItem(ui->treeWidget);
            itemOutput->setFlags(flgCheckbox);
            itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

            // address
            CTxDestination outputAddress;
            QString sAddress = "";
            if (ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, outputAddress)) {
                sAddress = QString::fromStdString(EncodeDestination(outputAddress));

                // if listMode or change => show aokchain address. In tree mode, address is not shown again for direct wallet address outputs
                if (!treeMode || (!(sAddress == sWalletAddress))) {
                    itemOutput->setText(COLUMN_ADDRESS, sAddress);
                    // token name
                    itemOutput->setText(COLUMN_TOKEN_NAME, QString::fromStdString(strTokenName));
                }
            }

            // label
            if (!(sAddress == sWalletAddress)) // change
            {
                // tooltip from where the change comes from
                itemOutput->setToolTip(COLUMN_LABEL,
                                       tr("change from %1 (%2)").arg(sWalletLabel).arg(sWalletAddress));
                itemOutput->setText(COLUMN_LABEL, tr("(change)"));
            } else if (!treeMode) {
                QString sLabel = model->getAddressTableModel()->labelForAddress(sAddress);
                if (sLabel.isEmpty())
                    sLabel = tr("(no label)");
                itemOutput->setText(COLUMN_LABEL, sLabel);
            }

            // amount
            itemOutput->setText(COLUMN_AMOUNT, AokChainUnits::format(nDisplayUnit, nAmount));
            itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole,
                                QVariant((qlonglong) nAmount)); // padding so that sorting works correctly

            // date
            itemOutput->setText(COLUMN_DATE, GUIUtil::dateTimeStr(out.tx->GetTxTime()));
            itemOutput->setData(COLUMN_DATE, Qt::UserRole, QVariant((qlonglong) out.tx->GetTxTime()));

            // confirmations
            itemOutput->setText(COLUMN_CONFIRMATIONS, QString::number(out.nDepth));
            itemOutput->setData(COLUMN_CONFIRMATIONS, Qt::UserRole, QVariant((qlonglong) out.nDepth));

            // transaction hash
            uint256 txhash = out.tx->GetHash();
            itemOutput->setText(COLUMN_TXHASH, QString::fromStdString(txhash.GetHex()));

            // vout index
            itemOutput->setText(COLUMN_VOUT_INDEX, QString::number(out.i));

            // disable locked coins
            if (model->isLockedCoin(txhash, out.i)) {
                COutPoint outpt(txhash, out.i);
                tokenControl->UnSelectToken(outpt); // just to be sure
                itemOutput->setDisabled(true);
                itemOutput->setIcon(COLUMN_CHECKBOX, platformStyle->SingleColorIcon(":/icons/lock_closed"));
            }

            // set checkbox
            if (tokenControl->IsTokenSelected(COutPoint(txhash, out.i)))
                itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        }

        // amount
        if (treeMode) {
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
            itemWalletAddress->setText(COLUMN_AMOUNT, AokChainUnits::format(nDisplayUnit, nSum));
            itemWalletAddress->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong) nSum));
        }
    }

    // expand all partially selected
    if (treeMode)
    {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
                ui->treeWidget->topLevelItem(i)->setExpanded(true);
    }

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
}

void TokenControlDialog::viewAdministratorClicked()
{
    tokenControl->UnSelectAll();
    TokenControlDialog::updateLabels(model, this);
    updateTokenList();
}

void TokenControlDialog::updateTokenList(bool fSetOnStart)
{
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        return;

    bool showAdministrator = ui->viewAdministrator->isChecked();
    if (fSetOnStart) {
        showAdministrator = IsTokenNameAnOwner(tokenControl->strTokenSelected);
        ui->viewAdministrator->setChecked(showAdministrator);
    }
    // Get the tokens
    std::vector<std::string> tokens;
    if (showAdministrator)
        GetAllAdministrativeTokens(model->getWallet(), tokens, 0);
    else
        GetAllMyTokens(model->getWallet(), tokens, 0);

    QStringList list;
    list << "";
    for (auto name : tokens) {
        list << QString::fromStdString(name);
    }

    stringModel->setStringList(list);

    int index = ui->tokenList->findText(QString::fromStdString(tokenControl->strTokenSelected));
    if (index != -1 ) { // -1 for not found
        fOnStartUp = fSetOnStart;
        ui->tokenList->setCurrentIndex(index);
    }

    updateView();
}

void TokenControlDialog::onTokenSelected(QString name)
{
    if (fOnStartUp) {
        fOnStartUp = false;
    } else {
        tokenControl->UnSelectAll();
    }

    TokenControlDialog::updateLabels(model, this);
    updateView();
}
