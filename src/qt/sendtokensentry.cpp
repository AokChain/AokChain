// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "sendtokensentry.h"
#include "ui_sendtokensentry.h"
//#include "sendcoinsentry.h"
//#include "ui_sendcoinsentry.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "aokchainunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "pagewidgets.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "tokencontroldialog.h"
#include "guiconstants.h"

#include <tokens/tokens.h>

#include "wallet/coincontrol.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <validation.h>
#include <core_io.h>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>

SendTokensEntry::SendTokensEntry(const PlatformStyle *_platformStyle, const QStringList myTokensNames, QWidget *parent) :
    QStackedWidget(parent),
    ui(new Ui::SendTokensEntry),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    ui->addressBookButton->setIcon(platformStyle->SingleColorIcon(":/icons/address-book"));
    ui->pasteButton->setIcon(platformStyle->SingleColorIcon(":/icons/editpaste"));
    ui->deleteButton->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->deleteButton_is->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->deleteButton_s->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));

    setCurrentWidget(ui->SendCoins);

    tokenBalance = 0;
    tokenUnits = 0;
    buildLayout();
#if QT_VERSION >= 0x040700
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
#endif

    // normal aokchain address field
    GUIUtil::setupAddressWidget(ui->payTo, this);
    // just a label for displaying aokchain address(es)
    ui->payTo_is->setFont(GUIUtil::fixedPitchFont());

    // Connect signals
    connect(ui->payTokenAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    connect(ui->useLockTime, SIGNAL(toggled(bool)), this, SLOT(toggleTimeLock(bool)));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->tokenSelectionBox, SIGNAL(activated(int)), this, SLOT(onTokenSelected(int)));
    connect(ui->administratorCheckbox, SIGNAL(clicked()), this, SLOT(onSendOwnershipChanged()));

    if (!gArgs.GetBoolArg("-advancedui", false)) {
        ui->administratorCheckbox->hide();
    }

    ui->administratorCheckbox->setToolTip(tr("Select to view administrator tokens to transfer"));

    /** Setup the token list combobox */
    stringModel = new QStringListModel;
    stringModel->insertRow(stringModel->rowCount());
    stringModel->setData(stringModel->index(stringModel->rowCount() - 1, 0), "", Qt::DisplayRole);

    for (auto name : myTokensNames)
    {
        stringModel->insertRow(stringModel->rowCount());
        stringModel->setData(stringModel->index(stringModel->rowCount() - 1, 0), name, Qt::DisplayRole);
    }

    proxy = new QSortFilterProxyModel;
    proxy->setSourceModel(stringModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tokenSelectionBox->setModel(proxy);
    ui->tokenSelectionBox->setEditable(true);

    completer = new QCompleter(proxy,this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->tokenSelectionBox->setCompleter(completer);

    ui->tokenSelectionBox->lineEdit()->setPlaceholderText(tr("Select an token to transfer"));
    ui->tokenSelectionBox->setMinimumWidth(32);

    /** Setup the amount box */
    ui->ownershipWarningMessage->hide();

    fShowAdministratorList = false;

    // the entries sit in the card of the tokens page
    this->setStyleSheet(".SendTokensEntry { background: transparent; border: none; }");
    ui->payTo->setFont(GUIUtil::monoFont(14));

    ui->tokenBoxLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->tokenBoxLabel->setFont(GUIUtil::getSubLabelFont());

    ui->payToLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->payToLabel->setFont(GUIUtil::getSubLabelFont());

    ui->labellLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->labellLabel->setFont(GUIUtil::getSubLabelFont());

    ui->tokenLockTimeLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->tokenLockTimeLabel->setFont(GUIUtil::getSubLabelFont());

    ui->amountLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->amountLabel->setFont(GUIUtil::getSubLabelFont());

    ui->messageLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->messageLabel->setFont(GUIUtil::getSubLabelFont());

    ui->payTokenAmount->setUnit(MAX_UNIT);
    ui->payTokenAmount->setDisabled(false);

    ui->administratorCheckbox->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->tokenSelectionBox->setFont(GUIUtil::getSubLabelFont());
    ui->administratorCheckbox->setFont(GUIUtil::getSubLabelFont());
#if defined(Q_OS_WIN)
    ui->useLockTime->setStyleSheet(QString(".QCheckBox{ %1; margin-left: 10px; }").arg(STRING_LABEL_COLOR));
#else
    ui->useLockTime->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));
#endif
    ui->addAsLabel->setFont(GUIUtil::getSubLabelFont());
    ui->tokenLockTime->setFont(GUIUtil::getSubLabelFont());
    ui->tokenLockTime->setMinimumDate(QDate::currentDate());
    ui->messageTextLabel->setFont(GUIUtil::getSubLabelFont());
    ui->tokenAmountLabel->setFont(GUIUtil::getSubLabelFont());
    ui->ownershipWarningMessage->setFont(GUIUtil::getSubLabelFont());
}

/** The same card as a recipient of the AOK form; the token is chosen for the whole page. */
void SendTokensEntry::buildLayout()
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    // the grid of the form goes; its widgets stay
    delete ui->SendCoins->layout();
    ui->SendCoins->setProperty("role", "recipient");
    QVBoxLayout *layout = new QVBoxLayout(ui->SendCoins);
    layout->setContentsMargins(18, 12, 18, 16);
    layout->setSpacing(8);

    QHBoxLayout *header = new QHBoxLayout();
    cardTitle = new QLabel(ui->SendCoins);
    cardTitle->setFont(GUIUtil::appFont(13, QFont::Medium));
    cardTitle->setProperty("role", "secondary");
    header->addWidget(cardTitle);
    header->addStretch();
    ui->deleteButton->setIcon(QIcon(GUIUtil::lineIcon("remove", 14, theme.label, 2.2)));
    ui->deleteButton->setIconSize(QSize(14, 14));
    ui->deleteButton->setFixedSize(30, 30);
    ui->deleteButton->setProperty("look", "icon");
    ui->deleteButton->setToolTip(tr("Remove this recipient"));
    ui->deleteButton->setCursor(Qt::PointingHandCursor);
    header->addWidget(ui->deleteButton);
    layout->addLayout(header);

    ui->payToLabel->setText(tr("Recipient &address"));
    ui->amountLabel->setText(tr("Quantity"));
    for (QLabel *label : {ui->payToLabel, ui->amountLabel, ui->messageLabel, ui->tokenBoxLabel}) {
        label->setProperty("role", "fieldLabel");
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setStyleSheet(QString());
    }

    layout->addWidget(ui->payToLabel);
    QHBoxLayout *addressRow = new QHBoxLayout();
    addressRow->setSpacing(8);
    ui->payTo->setProperty("look", "field");
    ui->payTo->setFixedHeight(48);
    addressRow->addWidget(ui->payTo, 1);
    const std::pair<QToolButton*, const char*> squares[] = {{ui->addressBookButton, "addressbook"}, {ui->pasteButton, "paste"}};
    for (const std::pair<QToolButton*, const char*>& square : squares) {
        square.first->setIcon(QIcon(GUIUtil::lineIcon(square.second, 18, theme.label)));
        square.first->setIconSize(QSize(18, 18));
        square.first->setFixedSize(48, 48);
        square.first->setCursor(Qt::PointingHandCursor);
        addressRow->addWidget(square.first);
    }
    QPushButton *labelButton = new QPushButton(tr("Label"), ui->SendCoins);
    labelButton->setIcon(QIcon(GUIUtil::lineIcon("tag", 16, theme.label)));
    labelButton->setIconSize(QSize(16, 16));
    labelButton->setFixedHeight(48);
    labelButton->setAutoDefault(false);
    labelButton->setCursor(Qt::PointingHandCursor);
    labelButton->setToolTip(tr("Give this address a label for your address book"));
    connect(labelButton, SIGNAL(clicked()), this, SLOT(editLabel()));
    addressRow->addWidget(labelButton);
    layout->addLayout(addressRow);

    QHBoxLayout *chipRow = new QHBoxLayout();
    chipRow->setSpacing(8);
    addressHint = new QLabel(ui->SendCoins);
    addressHint->setFont(GUIUtil::appFont(12));
    addressHint->setMinimumHeight(26);
    chipRow->addWidget(addressHint);
    chipRow->addStretch();
    labelChip = new ChipButton(ui->SendCoins);
    labelChip->setToolTip(tr("Label of the address. Click to change it."));
    lockChip = new ChipButton(ui->SendCoins);
    lockChip->setToolTip(tr("The recipient cannot spend the tokens before this time. Click to change it."));
    chipRow->addWidget(labelChip);
    chipRow->addWidget(lockChip);
    layout->addLayout(chipRow);
    connect(labelChip, SIGNAL(clicked()), this, SLOT(editLabel()));
    connect(labelChip, SIGNAL(removeClicked()), this, SLOT(clearLabel()));
    connect(lockChip, SIGNAL(clicked()), this, SLOT(editLockTime()));
    connect(lockChip, SIGNAL(removeClicked()), this, SLOT(clearLockTime()));

    // the fields of the form hold label, lock time and token; they are not shown
    ui->labellLabel->hide();
    ui->addAsLabel->hide();
    ui->tokenLockTimeLabel->hide();
    ui->useLockTime->hide();
    ui->tokenLockTime->hide();
    ui->tokenAmountLabel->hide();
    connect(ui->addAsLabel, SIGNAL(textChanged(QString)), this, SLOT(updateChips()));
    connect(ui->useLockTime, SIGNAL(toggled(bool)), this, SLOT(updateChips()));

    // quantity
    layout->addWidget(ui->amountLabel);
    QFrame *amountBox = new QFrame(ui->SendCoins);
    amountBox->setProperty("role", "amountBox");
    amountBox->setFixedHeight(60);
    QHBoxLayout *amountLayout = new QHBoxLayout(amountBox);
    amountLayout->setContentsMargins(16, 0, 10, 0);
    amountLayout->setSpacing(12);
    ui->payTokenAmount->setHeroStyle();
    amountLayout->addWidget(ui->payTokenAmount, 1);
    unitLabel = new QLabel(amountBox);
    unitLabel->setFont(GUIUtil::appFont(14, QFont::Medium));
    unitLabel->setProperty("role", "secondary");
    amountLayout->addWidget(unitLabel);
    QPushButton *maxButton = new QPushButton(tr("All"), amountBox);
    maxButton->setProperty("look", "soft");
    maxButton->setToolTip(tr("Send everything the wallet holds of this token"));
    maxButton->setFixedHeight(40);
    maxButton->setAutoDefault(false);
    maxButton->setCursor(Qt::PointingHandCursor);
    amountLayout->addWidget(maxButton);
    connect(maxButton, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));
    layout->addWidget(amountBox);

    QHBoxLayout *optionRow = new QHBoxLayout();
    optionRow->addWidget(ui->ownershipWarningMessage);
    optionRow->addStretch();
    QPushButton *lockButton = new QPushButton(tr("Send with a lock"), ui->SendCoins);
    lockButton->setProperty("look", "chipButton");
    lockButton->setIcon(QIcon(GUIUtil::lineIcon("clock", 14, theme.label)));
    lockButton->setIconSize(QSize(14, 14));
    lockButton->setAutoDefault(false);
    lockButton->setCursor(Qt::PointingHandCursor);
    lockButton->setToolTip(tr("Send tokens that the recipient cannot spend before a time you choose"));
    connect(lockButton, SIGNAL(clicked()), this, SLOT(editLockTime()));
    optionRow->addWidget(lockButton);
    layout->addLayout(optionRow);

    // With -advancedui the token can also be chosen here, which is the way to transfer an owner token
    QHBoxLayout *advancedRow = new QHBoxLayout();
    advancedRow->setSpacing(8);
    advancedRow->addWidget(ui->tokenBoxLabel);
    advancedRow->addWidget(ui->tokenSelectionBox, 1);
    advancedRow->addWidget(ui->administratorCheckbox);
    layout->addLayout(advancedRow);
    if (!gArgs.GetBoolArg("-advancedui", false)) {
        ui->tokenBoxLabel->hide();
        ui->tokenSelectionBox->hide();
    }

    layout->addWidget(ui->messageLabel);
    layout->addWidget(ui->messageTextLabel);

    connect(ui->payTokenAmount, SIGNAL(valueChanged()), this, SIGNAL(recipientChanged()));
    setIndex(1);
    updateChips();
    updateAddressHint(QString());
}

void SendTokensEntry::setIndex(int index)
{
    cardTitle->setText(tr("Recipient %1").arg(index));
}

void SendTokensEntry::selectToken(const QString &name)
{
    int index = name.isEmpty() ? 0 : ui->tokenSelectionBox->findText(name);
    if (index < 0) {
        // the list of the entry may be older than the wallet
        refreshTokenList();
        index = ui->tokenSelectionBox->findText(name);
    }
    if (index < 0)
        index = 0;
    ui->tokenSelectionBox->setCurrentIndex(index);
    onTokenSelected(index);
}

void SendTokensEntry::updateAddressHint(const QString &address)
{
    const QString text = address.trimmed();
    if (text.isEmpty() || !model) {
        addressHint->setText(QString());
        return;
    }
    if (model->validateAddress(text)) {
        addressHint->setProperty("role", "ok");
        addressHint->setText(tr("This is a valid AokChain address"));
    } else if (IsUsernameValid(text.toStdString())) {
        addressHint->setProperty("role", "secondary");
        addressHint->setText(tr("A user name: it is resolved to its address when sending"));
    } else {
        addressHint->setProperty("role", "error");
        addressHint->setText(tr("This is not a valid AokChain address"));
    }
    addressHint->style()->unpolish(addressHint);
    addressHint->style()->polish(addressHint);
}

void SendTokensEntry::useAvailableBalanceClicked()
{
    if (tokenBalance > 0)
        ui->payTokenAmount->setValue(tokenBalance / AokChainUnits::factorToken(8 - tokenUnits));
}

void SendTokensEntry::editLabel()
{
    QString label = ui->addAsLabel->text();
    if (PageDialogs::askLabel(this, label))
        ui->addAsLabel->setText(label);
}

void SendTokensEntry::clearLabel()
{
    ui->addAsLabel->clear();
}

void SendTokensEntry::editLockTime()
{
    QDateTime until = lockTime();
    if (PageDialogs::askLockTime(this, until)) {
        ui->tokenLockTime->setEnabled(true);
        ui->tokenLockTime->setDateTime(until);
        ui->useLockTime->setChecked(true);
        updateChips();
    }
}

void SendTokensEntry::clearLockTime()
{
    ui->useLockTime->setChecked(false);
}

QDateTime SendTokensEntry::lockTime() const
{
    return ui->useLockTime->isChecked() ? ui->tokenLockTime->dateTime() : QDateTime();
}

void SendTokensEntry::updateChips()
{
    const QString label = ui->addAsLabel->text();
    labelChip->setText(tr("Label: %1").arg(QFontMetrics(GUIUtil::appFont(12, QFont::Medium)).elidedText(label, Qt::ElideRight, 160)));
    labelChip->setVisible(!label.isEmpty());
    const QDateTime until = lockTime();
    lockChip->setText(tr("Locked until %1").arg(GUIUtil::isoDateTime(until)));
    lockChip->setVisible(until.isValid());
    Q_EMIT recipientChanged();
}

QString SendTokensEntry::addressText() const
{
    return ui->payTo->text().trimmed();
}

QString SendTokensEntry::labelText() const
{
    return ui->addAsLabel->text();
}

CAmount SendTokensEntry::amount() const
{
    return ui->payTokenAmount->value();
}

bool SendTokensEntry::isComplete() const
{
    if (recipient.paymentRequest.IsInitialized())
        return true;
    if (!model || ui->tokenSelectionBox->currentIndex() <= 0)
        return false;
    const QString address = ui->payTo->text().trimmed();
    if (!model->validateAddress(address) && !IsUsernameValid(address.toStdString()))
        return false;
    return ui->payTokenAmount->value() > 0;
}

void SendTokensEntry::toggleTimeLock(bool checked)
{
    ui->tokenLockTime->setEnabled(checked);
}

SendTokensEntry::~SendTokensEntry()
{
    delete ui;
}

void SendTokensEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendTokensEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payTokenAmount->setFocus();
    }
}

void SendTokensEntry::on_payTo_textChanged(const QString &address)
{
    updateAddressHint(address);
    Q_EMIT recipientChanged();
    updateLabel(address);
}

void SendTokensEntry::setModel(WalletModel *_model)
{
    this->model = _model;

//    if (_model && _model->getOptionsModel())
//        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendTokensEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->tokenLockTime->clear();
    ui->useLockTime->setCheckState(Qt::Unchecked);
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
    // clear UI elements for unauthenticated payment request
    ui->memoTextLabel_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();

    ui->payTokenAmount->clear();

    // Reset the selected token
    ui->tokenSelectionBox->setCurrentIndex(0);
}

void SendTokensEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

bool SendTokensEntry::validate()
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    if (!model->validateAddress(ui->payTo->text()) && !IsUsernameValid(ui->payTo->text().toStdString()))
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (ui->tokenSelectionBox->currentIndex() == 0) {
        ui->tokenSelectionBox->lineEdit()->setStyleSheet(STYLE_INVALID);
        retval = false;
    }

    if (!ui->payTokenAmount->validate())
    {
        retval = false;
    }

    if (ui->payTokenAmount->value(0) <= 0)
    {
        ui->payTokenAmount->setValid(false);
        retval = false;
    }

    // TODO check to make sure the payAmount value is within the constraints of how much you own

    return retval;
}

SendTokensRecipient SendTokensEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    recipient.tokenName = ui->tokenSelectionBox->currentText();
    
    if (IsUsernameValid(ui->payTo->text().toStdString()))
    {
        recipient.username = ui->payTo->text();
        recipient.address = QString::fromStdString(ptokensdb->UsernameAddress(ui->payTo->text().toStdString()));
    } else {
        recipient.address = ui->payTo->text();
    }

    recipient.label = ui->addAsLabel->text();

    if (ui->useLockTime->checkState() == Qt::Checked) {
        recipient.tokenLockTime = ui->tokenLockTime->dateTime().toTime_t();
    } else {
        recipient.tokenLockTime = 0;
    }

    recipient.amount = ui->payTokenAmount->value();
    recipient.message = ui->messageTextLabel->text();

    return recipient;
}

QWidget *SendTokensEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendTokensEntry::setValue(const SendTokensRecipient &value)
{
    recipient = value;

    if (recipient.tokenName != "") {
        int index = ui->tokenSelectionBox->findText(recipient.tokenName);
        ui->tokenSelectionBox->setCurrentIndex(index);
        onTokenSelected(index);
    }
}

void SendTokensEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payTokenAmount->setFocus();
}

bool SendTokensEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendTokensEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendTokensEntry::setFocusTokenListBox()
{
    ui->tokenSelectionBox->setFocus();
}

bool SendTokensEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}

void SendTokensEntry::onTokenSelected(int index)
{
    ui->tokenSelectionBox->lineEdit()->setStyleSheet("");
    QString name = ui->tokenSelectionBox->currentText();
    // If the name
    if (index == 0) {
        tokenBalance = 0;
        tokenUnits = 0;
        unitLabel->clear();
        Q_EMIT recipientChanged();
        ui->tokenAmountLabel->clear();
        if(!ui->administratorCheckbox->isChecked()) {
            ui->payTokenAmount->setDisabled(false);
            
        }
        ui->payTokenAmount->clear();
        return;
    }

    // Check to see if the token selected is an ownership token
    bool fIsOwnerToken = false;
    if (IsTokenNameAnOwner(name.toStdString())) {
        fIsOwnerToken = true;
        name = name.split("!").first();
    }

    LOCK(cs_main);
    auto currentActiveTokenCache = GetCurrentTokenCache();
    CNewToken token;

    // Get the token metadata if it exists. This isn't called on the administrator token because that doesn't have metadata
    if (!currentActiveTokenCache->GetTokenMetaDataIfExists(name.toStdString(), token)) {
        // This should only happen if the user, selected an token that was issued from tokencontrol and tries to transfer it before it is mined.
        clear();
        ui->messageLabel->show();
        ui->messageTextLabel->show();
        ui->messageTextLabel->setText(tr("Failed to get token metadata for: ") + name + "." + tr(" The transaction in which the token was issued must be mined into a block before you can transfer it"));
        ui->tokenAmountLabel->clear();
        return;
    }

    CAmount amount = 0;

    if(!model || !model->getWallet())
        return;

    std::map<std::string, std::vector<COutput> > mapTokens;
    model->getWallet()->AvailableTokens(mapTokens, true, TokenControlDialog::tokenControl);

    // Add back the OWNER_TAG (!) that was removed above
    if (fIsOwnerToken)
        name = name + OWNER_TAG;


    if (!mapTokens.count(name.toStdString())) {
        clear();
        ui->messageLabel->show();
        ui->messageTextLabel->show();
        ui->messageTextLabel->setText(tr("Failed to get token outpoints from database"));
        return;
    }

    auto vec = mapTokens.at(name.toStdString());

    // Go through all of the mapTokens to get the total count of tokens
    for (auto txout : vec) {
        CTokenOutputEntry data;
        if (GetTokenData(txout.tx->tx->vout[txout.i].scriptPubKey, data))
            amount += data.nAmount;
    }

    int units = fIsOwnerToken ? OWNER_UNITS : token.units;
    tokenBalance = amount;
    tokenUnits = units;
    unitLabel->setText(name);
    Q_EMIT recipientChanged();

    QString displayBalance = TokenControlDialog::tokenControl->HasTokenSelected() ? tr("Selected Balance") : tr("Wallet Balance");

    ui->tokenAmountLabel->setText(
            displayBalance + ": <b>" + QString::fromStdString(ValueFromAmountString(amount, units)) + "</b> " + name);

    ui->messageLabel->hide();
    ui->messageTextLabel->hide();

    // If it is an ownership token lock the amount
    if (!fIsOwnerToken) {
        ui->payTokenAmount->setUnit(token.units);
        ui->payTokenAmount->setDisabled(false);
    }
}

void SendTokensEntry::onSendOwnershipChanged()
{
    switchAdministratorList(true);
}

void SendTokensEntry::CheckOwnerBox() {
    fUsingTokenControl = true;
    switchAdministratorList();
}

void SendTokensEntry::IsTokenControl(bool fIsTokenControl, bool fIsOwner)
{
    if (fIsOwner) {
        CheckOwnerBox();
    }
    if (fIsTokenControl) {
        ui->administratorCheckbox->setDisabled(true);
        fUsingTokenControl = true;
    }
}

void SendTokensEntry::setCurrentIndex(int index)
{
    if (index < ui->tokenSelectionBox->count()) {
        ui->tokenSelectionBox->setCurrentIndex(index);
        ui->tokenSelectionBox->activated(index);
    }
}

void SendTokensEntry::refreshTokenList()
{
    switchAdministratorList(false);
}

void SendTokensEntry::switchAdministratorList(bool fSwitchStatus)
{
    if(!model)
        return;

    if (fSwitchStatus)
        fShowAdministratorList = !fShowAdministratorList;

    if (fShowAdministratorList) {
        ui->administratorCheckbox->setChecked(true);
        if (!TokenControlDialog::tokenControl->HasTokenSelected()) {
            std::vector<std::string> names;
            GetAllAdministrativeTokens(model->getWallet(), names, 0);

            QStringList list;
            list << "";
            for (auto name: names)
                list << QString::fromStdString(name);

            stringModel->setStringList(list);

            ui->tokenSelectionBox->lineEdit()->setPlaceholderText(tr("Select an administrator token to transfer"));
            ui->tokenSelectionBox->setFocus();
        } else {
            ui->payTo->setFocus();
        }

        ui->payTokenAmount->setUnit(MIN_UNIT); // Min unit because this is an administrator token
        ui->payTokenAmount->setValue(1); // When using TokenAmountField, you must use 1 instead of 1 * COIN, because of the way that TokenAmountField uses the unit and value to display the amount
        ui->payTokenAmount->setDisabled(true);

        ui->tokenLockTime->setDisabled(true);
        ui->tokenLockTime->clear();

        ui->tokenAmountLabel->clear();

        ui->ownershipWarningMessage->setText(tr("Warning: Transferring administrator token"));
        ui->ownershipWarningMessage->setStyleSheet("color: red");
        ui->ownershipWarningMessage->show();
    } else {
        ui->administratorCheckbox->setChecked(false);
        if (!TokenControlDialog::tokenControl->HasTokenSelected()) {
            std::vector<std::string> names;
            GetAllMyTokens(model->getWallet(), names, 0);
            QStringList list;
            list << "";
            for (auto name : names) {
                if (!IsTokenNameAnOwner(name))
                    list << QString::fromStdString(name);
            }

            stringModel->setStringList(list);
            ui->tokenSelectionBox->lineEdit()->setPlaceholderText(tr("Select an token to transfer"));
            ui->payTokenAmount->clear();
            ui->payTokenAmount->setUnit(MAX_UNIT);
            ui->payTokenAmount->setDisabled(false);
            ui->tokenAmountLabel->clear();

            ui->tokenLockTime->setDisabled(true);
            ui->tokenLockTime->clear();

            ui->tokenSelectionBox->setFocus();
        } else {
            ui->payTo->setFocus();
        }
        ui->ownershipWarningMessage->hide();
    }
}
