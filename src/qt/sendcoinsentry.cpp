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
#include "darkstyle.h"
#include "validation.h"

#include <tokens/tokens.h>

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

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);
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

    this->setStyleSheet(QString(".SendCoinsEntry {background-color: %1; padding-top: 10px; padding-right: 30px; border: none;}").arg(platformStyle->SendEntriesBackGroundColor().name()));

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
    ui->payTo->setFont(GUIUtil::getSubLabelFont());
    ui->addAsLabel->setFont(GUIUtil::getSubLabelFont());
    ui->coinLockTime->setFont(GUIUtil::getSubLabelFont());
    ui->coinLockTime->setMinimumDate(QDate::currentDate());
    ui->payAmount->setFont(GUIUtil::getSubLabelFont());
    ui->messageTextLabel->setFont(GUIUtil::getSubLabelFont());
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
