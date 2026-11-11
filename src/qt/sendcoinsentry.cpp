// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendcoinsentry.h"
#include "ui_sendcoinsentry.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "guiconstants.h"
#include "validation.h"

#include <tokens/tokens.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>

SendCoinsEntry::SendCoinsEntry(const PlatformStyle *_platformStyle, QWidget *parent) :
    QStackedWidget(parent),
    ui(new Ui::SendCoinsEntry),
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

    buildLayout();
#if QT_VERSION >= 0x040700
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
#endif

    // normal aokchain address field
    GUIUtil::setupAddressWidget(ui->payTo, this);
    // just a label for displaying aokchain address(es)
    ui->payTo_is->setFont(GUIUtil::getSubLabelFont());

    // Connect signals
    connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    connect(ui->checkboxSubtractFeeFromAmount, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
    connect(ui->useLockTime, SIGNAL(toggled(bool)), this, SLOT(toggleTimeLock(bool)));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));

    // the entries sit in the card of the send page
    this->setStyleSheet(".SendCoinsEntry { background: transparent; border: none; }");

    ui->payToLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->payToLabel->setFont(GUIUtil::getSubLabelFont());

    ui->labellLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->labellLabel->setFont(GUIUtil::getSubLabelFont());

    ui->amountLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->amountLabel->setFont(GUIUtil::getSubLabelFont());

    ui->messageLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->messageLabel->setFont(GUIUtil::getSubLabelFont());

    ui->lockTimeLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->lockTimeLabel->setFont(GUIUtil::getSubLabelFont());

    ui->checkboxSubtractFeeFromAmount->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));
#if defined(Q_OS_WIN)
    ui->useLockTime->setStyleSheet(QString(".QCheckBox{ %1; margin-left: 10px; }").arg(STRING_LABEL_COLOR));
#else
    ui->useLockTime->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));
#endif
    ui->payTo->setFont(GUIUtil::monoFont(14));
    ui->addAsLabel->setFont(GUIUtil::getSubLabelFont());
    ui->coinLockTime->setFont(GUIUtil::getSubLabelFont());
    ui->coinLockTime->setMinimumDate(QDate::currentDate());
    ui->messageTextLabel->setFont(GUIUtil::getSubLabelFont());
}

/** Labels above the fields, the amount in a large box of its own. */
void SendCoinsEntry::buildLayout()
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    // the grid of the form goes; its widgets stay
    delete ui->SendCoins->layout();
    QVBoxLayout *layout = new QVBoxLayout(ui->SendCoins);
    layout->setContentsMargins(0, 0, 0, 12);
    layout->setSpacing(8);

    ui->payToLabel->setText(tr("Recipient &address"));
    ui->labellLabel->setText(tr("&Label"));
    ui->amountLabel->setText(tr("A&mount"));
    for (QLabel *label : {ui->payToLabel, ui->labellLabel, ui->amountLabel, ui->lockTimeLabel, ui->messageLabel}) {
        label->setProperty("role", "fieldLabel");
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    // address with the address book, paste and remove buttons
    layout->addWidget(ui->payToLabel);
    QHBoxLayout *addressRow = new QHBoxLayout();
    addressRow->setSpacing(8);
    ui->payTo->setProperty("look", "field");
    ui->payTo->setFixedHeight(48);
    addressRow->addWidget(ui->payTo, 1);
    const std::pair<QToolButton*, const char*> squares[] = {
        {ui->addressBookButton, "addressbook"}, {ui->pasteButton, "paste"}, {ui->deleteButton, "remove"}};
    for (const std::pair<QToolButton*, const char*>& square : squares) {
        square.first->setIcon(QIcon(GUIUtil::lineIcon(square.second, 18, theme.label)));
        square.first->setIconSize(QSize(18, 18));
        square.first->setFixedSize(48, 48);
        square.first->setCursor(Qt::PointingHandCursor);
        addressRow->addWidget(square.first);
    }
    layout->addLayout(addressRow);
    addressHint = new QLabel(ui->SendCoins);
    addressHint->setFont(GUIUtil::appFont(12));
    addressHint->setMinimumHeight(16);
    layout->addWidget(addressHint);
    layout->addSpacing(6);

    layout->addWidget(ui->labellLabel);
    ui->addAsLabel->setProperty("look", "field");
    ui->addAsLabel->setFixedHeight(48);
    layout->addWidget(ui->addAsLabel);
    layout->addSpacing(10);

    // amount
    layout->addWidget(ui->amountLabel);
    QFrame *amountBox = new QFrame(ui->SendCoins);
    amountBox->setProperty("role", "amountBox");
    amountBox->setFixedHeight(60);
    QHBoxLayout *amountLayout = new QHBoxLayout(amountBox);
    amountLayout->setContentsMargins(16, 0, 10, 0);
    amountLayout->setSpacing(12);
    ui->payAmount->setHeroStyle();
    amountLayout->addWidget(ui->payAmount, 1);
    QPushButton *maxButton = new QPushButton(tr("All"), amountBox);
    maxButton->setProperty("look", "soft");
    maxButton->setToolTip(tr("Use the whole available balance. The fee is subtracted from the amount."));
    maxButton->setFixedHeight(40);
    maxButton->setCursor(Qt::PointingHandCursor);
    amountLayout->addWidget(maxButton);
    connect(maxButton, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));
    layout->addWidget(amountBox);
    ui->checkboxSubtractFeeFromAmount->setText(tr("S&ubtract the fee from the amount"));
    layout->addWidget(ui->checkboxSubtractFeeFromAmount);

    // time lock
    QHBoxLayout *lockRow = new QHBoxLayout();
    lockRow->setSpacing(10);
    ui->lockTimeLabel->hide();
    ui->coinLockTime->setFont(GUIUtil::monoFont(13));
    ui->coinLockTime->setDisplayFormat("yyyy-MM-dd HH:mm");
    lockRow->addWidget(ui->useLockTime);
    lockRow->addWidget(ui->coinLockTime);
    lockRow->addStretch();
    layout->addLayout(lockRow);

    // message of an aokchain: URI
    layout->addWidget(ui->messageLabel);
    layout->addWidget(ui->messageTextLabel);

    updateAddressHint(QString());
}

void SendCoinsEntry::updateAddressHint(const QString &address)
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
    // the role selects the color
    addressHint->style()->unpolish(addressHint);
    addressHint->style()->polish(addressHint);
}

void SendCoinsEntry::useAvailableBalanceClicked()
{
    Q_EMIT useAvailableBalance(this);
}

QString SendCoinsEntry::addressText() const
{
    if (recipient.paymentRequest.IsInitialized())
        return recipient.authenticatedMerchant.isEmpty() ? recipient.address : recipient.authenticatedMerchant;
    return ui->payTo->text().trimmed();
}

QString SendCoinsEntry::labelText() const
{
    return ui->addAsLabel->text();
}

CAmount SendCoinsEntry::amount() const
{
    if (recipient.paymentRequest.IsInitialized())
        return recipient.amount;
    return ui->payAmount->value();
}

bool SendCoinsEntry::subtractsFee() const
{
    return ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked;
}

void SendCoinsEntry::setAmount(const CAmount &amount)
{
    ui->payAmount->setValue(amount);
}

void SendCoinsEntry::checkSubtractFeeFromAmount()
{
    ui->checkboxSubtractFeeFromAmount->setChecked(true);
}

void SendCoinsEntry::toggleTimeLock(bool checked)
{
    ui->coinLockTime->setEnabled(checked);
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    updateAddressHint(address);
    Q_EMIT recipientChanged();
    updateLabel(address);
}

void SendCoinsEntry::setModel(WalletModel *_model)
{
    this->model = _model;

    if (_model && _model->getOptionsModel())
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->coinLockTime->clear();
    ui->payAmount->clear();
    ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    ui->useLockTime->setCheckState(Qt::Unchecked);
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    // update the display unit, to not use the default ("AOK")
    updateDisplayUnit();
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

bool SendCoinsEntry::validate()
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

    if (!ui->payAmount->validate())
    {
        retval = false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(0) <= 0)
    {
        ui->payAmount->setValid(false);
        retval = false;
    }

    // Reject dust outputs:
    if (retval && GUIUtil::isDust(ui->payTo->text(), ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    
    if (IsUsernameValid(ui->payTo->text().toStdString()))
    {
        recipient.username = ui->payTo->text();
        recipient.address = QString::fromStdString(ptokensdb->UsernameAddress(ui->payTo->text().toStdString()));
    } else {
        recipient.address = ui->payTo->text();
    }

    recipient.label = ui->addAsLabel->text();
    recipient.label = ui->addAsLabel->text();

    if (ui->useLockTime->checkState() == Qt::Checked) {
        recipient.coinLockTime = ui->coinLockTime->dateTime().toTime_t();
    } else {
        recipient.coinLockTime = 0;
    }

    recipient.amount = ui->payAmount->value();
    recipient.message = ui->messageTextLabel->text();
    recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
    QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    recipient = value;

    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
    {
        // message
        ui->messageTextLabel->setText(recipient.message);
        ui->messageTextLabel->setVisible(!recipient.message.isEmpty());
        ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->addAsLabel->clear();
        ui->payTo->setText(recipient.address); // this may set a label from addressbook
        if (!recipient.label.isEmpty()) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText(recipient.label);
        ui->payAmount->setValue(recipient.amount);
    }
}

void SendCoinsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

bool SendCoinsEntry::updateLabel(const QString &address)
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
