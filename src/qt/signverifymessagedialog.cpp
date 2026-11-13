// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "signverifymessagedialog.h"
#include "ui_signverifymessagedialog.h"

#include "addressbookpage.h"
#include "guiutil.h"
#include "platformstyle.h"
#include "walletmodel.h"

#include "base58.h"
#include "init.h"
#include "validation.h" // For strMessageMagic
#include "wallet/wallet.h"

#include <string>
#include <vector>

#include <QClipboard>
#include <QPushButton>
#include <QTabBar>
#include <QStyle>
#include <QLabel>

SignVerifyMessageDialog::SignVerifyMessageDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SignVerifyMessageDialog),
    model(0),
    platformStyle(_platformStyle),
    embedded(false)
{
    ui->setupUi(this);

    // square buttons with line icons next to the fields
    const GUIUtil::Theme& t = GUIUtil::theme();
    const std::pair<QPushButton*, const char*> squares[] = {
        {ui->addressBookButton_SM, "addressbook"}, {ui->pasteButton_SM, "paste"},
        {ui->copySignatureButton_SM, "copy"}, {ui->addressBookButton_VM, "addressbook"}};
    for (const std::pair<QPushButton*, const char*>& square : squares) {
        square.first->setIcon(QIcon(GUIUtil::lineIcon(square.second, 18, t.label, 1.8)));
        square.first->setIconSize(QSize(18, 18));
        square.first->setFixedSize(44, 44);
        square.first->setStyleSheet("padding: 0;");
        square.first->setCursor(Qt::PointingHandCursor);
    }
    for (QPushButton *button : {ui->signMessageButton_SM, ui->clearButton_SM, ui->verifyMessageButton_VM, ui->clearButton_VM}) {
        button->setIcon(QIcon());
        button->setFixedHeight(42);
        button->setMinimumWidth(130);
    }
    ui->signMessageButton_SM->setProperty("primary", true);
    ui->verifyMessageButton_VM->setProperty("primary", true);

    // a label above each field
    auto fieldLabel = [this](const QString &text) {
        QLabel *label = new QLabel(text, this);
        label->setProperty("role", "fieldLabel");
        return label;
    };
    ui->verticalLayout_SM->insertWidget(1, fieldLabel(tr("Address")));
    ui->verticalLayout_SM->insertWidget(3, fieldLabel(tr("Message")));
    ui->signatureLabel_SM->setProperty("role", "fieldLabel");
    ui->verticalLayout_VM->insertWidget(1, fieldLabel(tr("Address")));
    ui->verticalLayout_VM->insertWidget(3, fieldLabel(tr("Message")));
    ui->verticalLayout_VM->insertWidget(5, fieldLabel(tr("Signature")));
    ui->infoLabel_SM->setProperty("role", "secondary");
    ui->infoLabel_VM->setProperty("role", "secondary");
    for (QLineEdit *edit : {(QLineEdit*)ui->addressIn_SM, (QLineEdit*)ui->addressIn_VM, ui->signatureOut_SM, (QLineEdit*)ui->signatureIn_VM}) {
        edit->setProperty("look", "field");
        edit->setFixedHeight(44);
    }
    ui->addressIn_SM->setFont(GUIUtil::monoFont(13));
    ui->addressIn_VM->setFont(GUIUtil::monoFont(13));
    ui->messageIn_SM->setPlaceholderText(tr("Enter the message you want to sign here"));
    ui->messageIn_VM->setPlaceholderText(tr("The message that was signed, exactly as it was written"));
    ui->signatureIn_VM->setPlaceholderText(tr("The signature in base 64"));
    ui->verticalLayout_SM->setSpacing(8);
    ui->verticalLayout_VM->setSpacing(8);
    // the result is shown right after the buttons
    const std::pair<QHBoxLayout*, QLabel*> results[] = {{ui->horizontalLayout_3_SM, ui->statusLabel_SM}, {ui->horizontalLayout_2_VM, ui->statusLabel_VM}};
    for (const std::pair<QHBoxLayout*, QLabel*>& result : results) {
        result.first->removeWidget(result.second);
        result.first->insertSpacing(2, 8);
        result.first->insertWidget(3, result.second);
        result.second->setWordWrap(false);
        result.second->setFont(GUIUtil::appFont(13, QFont::DemiBold));
        result.second->hide();
    }

#if QT_VERSION >= 0x040700
    ui->signatureOut_SM->setPlaceholderText(tr("Click \"Sign Message\" to generate signature"));
#endif

    GUIUtil::setupAddressWidget(ui->addressIn_SM, this);
    GUIUtil::setupAddressWidget(ui->addressIn_VM, this);

    ui->addressIn_SM->installEventFilter(this);
    ui->messageIn_SM->installEventFilter(this);
    ui->signatureOut_SM->installEventFilter(this);
    ui->addressIn_VM->installEventFilter(this);
    ui->messageIn_VM->installEventFilter(this);
    ui->signatureIn_VM->installEventFilter(this);

    ui->signatureOut_SM->setFont(GUIUtil::monoFont(12));
    ui->signatureIn_VM->setFont(GUIUtil::monoFont(12));
}

void SignVerifyMessageDialog::embedAsPage()
{
    embedded = true;
    setWindowFlags(Qt::Widget);
    // the tabs are chosen with the buttons above the card
    ui->tabWidget->tabBar()->hide();
    ui->tabWidget->setDocumentMode(true);
    ui->tabWidget->setStyleSheet("QTabWidget::pane { border: none; background: transparent; top: 0; }");
    setStyleSheet(QString("#%1 { background: transparent; }").arg(objectName()));
    for (QWidget *page : {ui->tabSignMessage, ui->tabVerifyMessage}) {
        if (page->layout())
            page->layout()->setContentsMargins(0, 0, 0, 0);
    }
    if (layout())
        layout()->setContentsMargins(0, 0, 0, 0);
}

void SignVerifyMessageDialog::done(int retval)
{
    // inside a page there is nothing to close
    if (!embedded)
        QDialog::done(retval);
}

void SignVerifyMessageDialog::showStatus(QLabel *label, bool fine, const QString &text)
{
    label->setProperty("role", fine ? "resultOk" : "resultError");
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->setText(text);
    label->show();
}

void SignVerifyMessageDialog::clearStatus(QLabel *label)
{
    label->clear();
    label->hide();
}

SignVerifyMessageDialog::~SignVerifyMessageDialog()
{
    delete ui;
}

void SignVerifyMessageDialog::setModel(WalletModel *_model)
{
    this->model = _model;
}

void SignVerifyMessageDialog::setAddress_SM(const QString &address)
{
    ui->addressIn_SM->setText(address);
    ui->messageIn_SM->setFocus();
}

void SignVerifyMessageDialog::setAddress_VM(const QString &address)
{
    ui->addressIn_VM->setText(address);
    ui->messageIn_VM->setFocus();
}

void SignVerifyMessageDialog::showTab_SM(bool fShow)
{
    ui->tabWidget->setCurrentIndex(0);
    if (fShow)
        this->show();
}

void SignVerifyMessageDialog::showTab_VM(bool fShow)
{
    ui->tabWidget->setCurrentIndex(1);
    if (fShow)
        this->show();
}

void SignVerifyMessageDialog::on_addressBookButton_SM_clicked()
{
    if (model && model->getAddressTableModel())
    {
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec())
        {
            setAddress_SM(dlg.getReturnValue());
        }
    }
}

void SignVerifyMessageDialog::on_pasteButton_SM_clicked()
{
    setAddress_SM(QApplication::clipboard()->text());
}

void SignVerifyMessageDialog::on_signMessageButton_SM_clicked()
{
    if (!model)
        return;

    /* Clear old signature to ensure users don't get confused on error with an old signature displayed */
    ui->signatureOut_SM->clear();

    CTxDestination destination = DecodeDestination(ui->addressIn_SM->text().toStdString());
    if (!IsValidDestination(destination)) {
        showStatus(ui->statusLabel_SM, false, tr("The entered address is invalid.") + QString(" ") + tr("Please check the address and try again."));
        return;
    }
    const CKeyID* keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        ui->addressIn_SM->setValid(false);
        showStatus(ui->statusLabel_SM, false, tr("The entered address does not refer to a key.") + QString(" ") + tr("Please check the address and try again."));
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!ctx.isValid())
    {
        showStatus(ui->statusLabel_SM, false, tr("Wallet unlock was cancelled."));
        return;
    }

    CKey key;
    if (!model->getPrivKey(*keyID, key))
    {
        showStatus(ui->statusLabel_SM, false, tr("Private key for the entered address is not available."));
        return;
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << ui->messageIn_SM->document()->toPlainText().toStdString();

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
    {
        showStatus(ui->statusLabel_SM, false, QString("<nobr>") + tr("Message signing failed.") + QString("</nobr>"));
        return;
    }

    showStatus(ui->statusLabel_SM, true, QString("<nobr>") + tr("Message signed.") + QString("</nobr>"));

    ui->signatureOut_SM->setText(QString::fromStdString(EncodeBase64(vchSig.data(), vchSig.size())));
}

void SignVerifyMessageDialog::on_copySignatureButton_SM_clicked()
{
    GUIUtil::setClipboard(ui->signatureOut_SM->text());
}

void SignVerifyMessageDialog::on_clearButton_SM_clicked()
{
    ui->addressIn_SM->clear();
    ui->messageIn_SM->clear();
    ui->signatureOut_SM->clear();
    clearStatus(ui->statusLabel_SM);

    ui->addressIn_SM->setFocus();
}

void SignVerifyMessageDialog::on_addressBookButton_VM_clicked()
{
    if (model && model->getAddressTableModel())
    {
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec())
        {
            setAddress_VM(dlg.getReturnValue());
        }
    }
}

void SignVerifyMessageDialog::on_verifyMessageButton_VM_clicked()
{
    CTxDestination destination = DecodeDestination(ui->addressIn_VM->text().toStdString());
    if (!IsValidDestination(destination)) {
        showStatus(ui->statusLabel_VM, false, tr("The entered address is invalid.") + QString(" ") + tr("Please check the address and try again."));
        return;
    }
    if (!boost::get<CKeyID>(&destination)) {
        ui->addressIn_VM->setValid(false);
        showStatus(ui->statusLabel_VM, false, tr("The entered address does not refer to a key.") + QString(" ") + tr("Please check the address and try again."));
        return;
    }

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(ui->signatureIn_VM->text().toStdString().c_str(), &fInvalid);

    if (fInvalid)
    {
        ui->signatureIn_VM->setValid(false);
        showStatus(ui->statusLabel_VM, false, tr("The signature could not be decoded.") + QString(" ") + tr("Please check the signature and try again."));
        return;
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << ui->messageIn_VM->document()->toPlainText().toStdString();

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
    {
        ui->signatureIn_VM->setValid(false);
        showStatus(ui->statusLabel_VM, false, tr("The signature did not match the message digest.") + QString(" ") + tr("Please check the signature and try again."));
        return;
    }

    if (!(CTxDestination(pubkey.GetID()) == destination)) {
        showStatus(ui->statusLabel_VM, false, QString("<nobr>") + tr("Message verification failed.") + QString("</nobr>"));
        return;
    }

    showStatus(ui->statusLabel_VM, true, QString("<nobr>") + tr("Message verified.") + QString("</nobr>"));
}

void SignVerifyMessageDialog::on_clearButton_VM_clicked()
{
    ui->addressIn_VM->clear();
    ui->signatureIn_VM->clear();
    ui->messageIn_VM->clear();
    clearStatus(ui->statusLabel_VM);

    ui->addressIn_VM->setFocus();
}

bool SignVerifyMessageDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn)
    {
        if (ui->tabWidget->currentIndex() == 0)
        {
            /* Clear status message on focus change */
            clearStatus(ui->statusLabel_SM);

            /* Select generated signature */
            if (object == ui->signatureOut_SM)
            {
                ui->signatureOut_SM->selectAll();
                return true;
            }
        }
        else if (ui->tabWidget->currentIndex() == 1)
        {
            /* Clear status message on focus change */
            clearStatus(ui->statusLabel_VM);
        }
    }
    return QDialog::eventFilter(object, event);
}
