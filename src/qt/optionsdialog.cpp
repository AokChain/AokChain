// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include "aokchainunits.h"
#include "guiutil.h"
#include "optionsmodel.h"

#include "validation.h" // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
#include "netbase.h"
#include "txdb.h" // for -dbcache defaults

#include <QDataWidgetMapper>
#include <QDir>
#include <QComboBox>
#include <QVBoxLayout>
#include <QStyle>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>
#include <QIntValidator>
#include <QLocale>
#include <QMessageBox>
#include <QTimer>

OptionsDialog::OptionsDialog(QWidget *parent, bool enableWallet) :
    QDialog(parent),
    ui(new Ui::OptionsDialog),
    model(0),
    mapper(0),
    walletEnabled(enableWallet),
    embedded(false),
    extraColumn(0)
{
    ui->setupUi(this);

    /* Main elements init */
    ui->databaseCache->setMinimum(nMinDbCache);
    ui->databaseCache->setMaximum(nMaxDbCache);
    ui->threadsScriptVerif->setMinimum(-GetNumCores());
    ui->threadsScriptVerif->setMaximum(MAX_SCRIPTCHECK_THREADS);

    /* Network elements init */
#ifndef USE_UPNP
    ui->mapPortUpnp->setEnabled(false);
#endif

    ui->proxyIp->setEnabled(false);
    ui->proxyPort->setEnabled(false);
    ui->proxyPort->setValidator(new QIntValidator(1, 65535, this));

    ui->proxyIpTor->setEnabled(false);
    ui->proxyPortTor->setEnabled(false);
    ui->proxyPortTor->setValidator(new QIntValidator(1, 65535, this));

    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyIp, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyPort, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(toggled(bool)), this, SLOT(updateProxyValidationState()));

    connect(ui->connectSocksTor, SIGNAL(toggled(bool)), ui->proxyIpTor, SLOT(setEnabled(bool)));
    connect(ui->connectSocksTor, SIGNAL(toggled(bool)), ui->proxyPortTor, SLOT(setEnabled(bool)));
    connect(ui->connectSocksTor, SIGNAL(toggled(bool)), this, SLOT(updateProxyValidationState()));

    /* Window elements init */
#ifdef Q_OS_MAC
    /* remove Window tab on Mac */
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWindow));
#endif

    /* remove Wallet tab in case of -disablewallet */
    if (!enableWallet) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWallet));
    }

    /* Display elements init */
    QDir translations(":translations");

    ui->aokchainAtStartup->setToolTip(ui->aokchainAtStartup->toolTip().arg(tr(PACKAGE_NAME)));
    ui->aokchainAtStartup->setText(ui->aokchainAtStartup->text().arg(tr(PACKAGE_NAME)));

    ui->openAokChainConfButton->setToolTip(ui->openAokChainConfButton->toolTip().arg(tr(PACKAGE_NAME)));

    ui->lang->setToolTip(ui->lang->toolTip().arg(tr(PACKAGE_NAME)));
    ui->lang->addItem(QString("(") + tr("default") + QString(")"), QVariant(""));
    for (const QString &langStr : translations.entryList())
    {
        QLocale locale(langStr);

        /** check if the locale name consists of 2 parts (language_country) */
        if(langStr.contains("_"))
        {
#if QT_VERSION >= 0x040800
            /** display language strings as "native language - native country (locale name)", e.g. "Deutsch - Deutschland (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" - ") + locale.nativeCountryName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
#else
            /** display language strings as "language - country (locale name)", e.g. "German - Germany (de)" */
            ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" - ") + QLocale::countryToString(locale.country()) + QString(" (") + langStr + QString(")"), QVariant(langStr));
#endif
        }
        else
        {
#if QT_VERSION >= 0x040800
            /** display language strings as "native language (locale name)", e.g. "Deutsch (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
#else
            /** display language strings as "language (locale name)", e.g. "German (de)" */
            ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" (") + langStr + QString(")"), QVariant(langStr));
#endif
        }
    }
#if QT_VERSION >= 0x040700
    ui->thirdPartyTxUrls->setPlaceholderText("https://example.com/tx/%s");
#endif

    ui->unit->setModel(new AokChainUnits(this));

    /* Widget-to-option mapper */
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper->setOrientation(Qt::Vertical);

    /* setup/change UI elements when proxy IPs are invalid/valid */
    ui->proxyIp->setCheckValidator(new ProxyAddressValidator(parent));
    ui->proxyIpTor->setCheckValidator(new ProxyAddressValidator(parent));
    connect(ui->proxyIp, SIGNAL(validationDidChange(QValidatedLineEdit *)), this, SLOT(updateProxyValidationState()));
    connect(ui->proxyIpTor, SIGNAL(validationDidChange(QValidatedLineEdit *)), this, SLOT(updateProxyValidationState()));
    connect(ui->proxyPort, SIGNAL(textChanged(const QString&)), this, SLOT(updateProxyValidationState()));
    connect(ui->proxyPortTor, SIGNAL(textChanged(const QString&)), this, SLOT(updateProxyValidationState()));
}

QFrame *OptionsDialog::makeCard(const QString &title, QWidget *parent, QVBoxLayout **body)
{
    QFrame *card = new QFrame(parent);
    card->setProperty("role", "card");
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(12);
    if (!title.isEmpty()) {
        QLabel *label = new QLabel(title, card);
        label->setProperty("role", "title");
        layout->addWidget(label);
    }
    if (body)
        *body = layout;
    return card;
}

void OptionsDialog::embedAsPage()
{
    embedded = true;
    setWindowFlags(Qt::Widget);

    // the buttons and the status line move to a bar below the cards
    ui->verticalLayout_Buttons->removeWidget(ui->openAokChainConfButton);
    ui->verticalLayout_Buttons->removeWidget(ui->resetButton);
    ui->horizontalLayout_Buttons->removeWidget(ui->statusLabel);
    ui->horizontalLayout->removeWidget(ui->okButton);
    ui->horizontalLayout->removeWidget(ui->cancelButton);
    ui->verticalLayout->removeWidget(ui->frame);
    for (int i = 0; i < ui->verticalLayout->count(); ++i) {
        if (ui->verticalLayout->itemAt(i)->layout() == ui->horizontalLayout_Buttons) {
            delete ui->verticalLayout->takeAt(i);
            break;
        }
    }
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->setSpacing(14);

    // each tab becomes a card; two columns of cards that scroll
    QList<QWidget*> pages;
    while (ui->tabWidget->count() > 0) {
        pages.append(ui->tabWidget->widget(0));
        ui->tabWidget->removeTab(0);
    }
    ui->tabWidget->hide();

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *body = new QWidget();
    body->setObjectName("settingsBody");
    body->setStyleSheet("QWidget#settingsBody { background: transparent; }");
    scroll->setWidget(body);
    scroll->viewport()->setAutoFillBackground(false);
    QHBoxLayout *columns = new QHBoxLayout(body);
    columns->setContentsMargins(0, 0, 8, 0);
    columns->setSpacing(16);
    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(16);
    extraColumn = new QVBoxLayout();
    extraColumn->setSpacing(16);
    columns->addLayout(left, 1);
    columns->addLayout(extraColumn, 1);

    auto addPages = [this, &pages](QVBoxLayout *column, const QString &title, const QList<QWidget*> &wanted) {
        QVBoxLayout *cardBody = nullptr;
        QFrame *card = nullptr;
        for (QWidget *page : wanted) {
            if (!pages.contains(page))
                continue; // a tab that this build or this start does not have
            if (!card)
                card = makeCard(title, this, &cardBody);
            if (QLayout *pageLayout = page->layout()) {
                pageLayout->setContentsMargins(0, 0, 0, 0);
                // the tabs were stretched with spacers; a card is as high as what it holds
                for (int i = pageLayout->count() - 1; i >= 0; --i) {
                    if (pageLayout->itemAt(i)->spacerItem())
                        delete pageLayout->takeAt(i);
                }
            }
            cardBody->addWidget(page);
            page->show();
        }
        if (card)
            column->addWidget(card);
    };
    addPages(left, tr("General"), {ui->tabMain});
    addPages(left, tr("Network"), {ui->tabNetwork});
    addPages(extraColumn, tr("Wallet"), {ui->tabWallet});
    addPages(extraColumn, tr("Display"), {ui->tabDisplay, ui->tabWindow});

    // the cards share the width: long lists do not widen a column
    for (QComboBox *combo : {(QComboBox*)ui->lang, (QComboBox*)ui->unit}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    ui->thirdPartyTxUrls->setMinimumWidth(80);
    ui->proxyPort->setMinimumWidth(76);
    ui->proxyPortTor->setMinimumWidth(76);
    body->setMinimumWidth(0);

    // command-line options that override the options above
    QVBoxLayout *overrideBody = nullptr;
    QFrame *overrideCard = makeCard(QString(), this, &overrideBody);
    ui->frame->setFrameShape(QFrame::NoFrame);
    if (ui->frame->layout())
        ui->frame->layout()->setContentsMargins(0, 0, 0, 0);
    ui->overriddenByCommandLineInfoLabel->setProperty("role", "fieldLabel");
    ui->overriddenByCommandLineLabel->setFont(GUIUtil::monoFont(12));
    ui->overriddenByCommandLineLabel->setWordWrap(true);
    overrideBody->addWidget(ui->frame);
    left->addWidget(overrideCard);
    left->addStretch(1);
    extraColumn->addStretch(1);
    ui->verticalLayout->addWidget(scroll, 1);

    QHBoxLayout *bar = new QHBoxLayout();
    bar->setSpacing(8);
    ui->statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->statusLabel->setMinimumWidth(0);
    ui->statusLabel->hide();
    ui->statusLabel->setWordWrap(false);
    bar->addWidget(ui->statusLabel);
    bar->addStretch(1);
    bar->addWidget(ui->openAokChainConfButton);
    bar->addWidget(ui->resetButton);
    bar->addSpacing(12);
    bar->addWidget(ui->cancelButton);
    bar->addWidget(ui->okButton);
    ui->cancelButton->setText(tr("Revert"));
    ui->cancelButton->setToolTip(tr("Show the saved options again"));
    ui->okButton->setText(tr("Save"));
    ui->okButton->setProperty("primary", true);
    ui->okButton->setDefault(false);
    ui->okButton->setAutoDefault(false);
    ui->cancelButton->setAutoDefault(false);
    for (QPushButton *button : {ui->openAokChainConfButton, ui->resetButton, ui->cancelButton, ui->okButton})
        button->setFixedHeight(40);
    ui->okButton->setMinimumWidth(110);
    ui->verticalLayout->addLayout(bar);
}

void OptionsDialog::addCard(QWidget *card)
{
    if (extraColumn)
        extraColumn->insertWidget(extraColumn->count() - 1, card);
}

void OptionsDialog::done(int retval)
{
    // inside a page there is nothing to close
    if (!embedded)
        QDialog::done(retval);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::setModel(OptionsModel *_model)
{
    this->model = _model;

    if(_model)
    {
        /* check if client restart is needed and show persistent message */
        if (_model->isRestartRequired())
            showRestartWarning(true);

        QString strLabel = _model->getOverriddenByCommandLine();
        if (strLabel.isEmpty())
            strLabel = tr("none");
        ui->overriddenByCommandLineLabel->setText(strLabel);

        mapper->setModel(_model);
        setMapper();
        mapper->toFirst();

        updateDefaultProxyNets();
    }

    /* warn when one of the following settings changes by user action (placed here so init via mapper doesn't trigger them) */

    /* Main */
    connect(ui->databaseCache, SIGNAL(valueChanged(int)), this, SLOT(showRestartWarning()));
    connect(ui->threadsScriptVerif, SIGNAL(valueChanged(int)), this, SLOT(showRestartWarning()));
    /* Wallet */
    connect(ui->spendZeroConfChange, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    /* Network */
    connect(ui->allowIncoming, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    connect(ui->connectSocks, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    connect(ui->connectSocksTor, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    /* Display */
    connect(ui->lang, SIGNAL(valueChanged()), this, SLOT(showRestartWarning()));
    connect(ui->thirdPartyTxUrls, SIGNAL(textChanged(const QString &)), this, SLOT(showRestartWarning()));
}

void OptionsDialog::setMapper()
{
    /* Main */
    mapper->addMapping(ui->aokchainAtStartup, OptionsModel::StartAtStartup);
    mapper->addMapping(ui->threadsScriptVerif, OptionsModel::ThreadsScriptVerif);
    mapper->addMapping(ui->databaseCache, OptionsModel::DatabaseCache);

    /* Wallet */
    mapper->addMapping(ui->spendZeroConfChange, OptionsModel::SpendZeroConfChange);
    mapper->addMapping(ui->coinControlFeatures, OptionsModel::CoinControlFeatures);
    mapper->addMapping(ui->customFeeFeatures, OptionsModel::CustomFeeFeatures);

    /* Network */
    mapper->addMapping(ui->mapPortUpnp, OptionsModel::MapPortUPnP);
    mapper->addMapping(ui->allowIncoming, OptionsModel::Listen);

    mapper->addMapping(ui->connectSocks, OptionsModel::ProxyUse);
    mapper->addMapping(ui->proxyIp, OptionsModel::ProxyIP);
    mapper->addMapping(ui->proxyPort, OptionsModel::ProxyPort);

    mapper->addMapping(ui->connectSocksTor, OptionsModel::ProxyUseTor);
    mapper->addMapping(ui->proxyIpTor, OptionsModel::ProxyIPTor);
    mapper->addMapping(ui->proxyPortTor, OptionsModel::ProxyPortTor);

    /* Window */
#ifndef Q_OS_MAC
    mapper->addMapping(ui->hideTrayIcon, OptionsModel::HideTrayIcon);
    mapper->addMapping(ui->minimizeToTray, OptionsModel::MinimizeToTray);
    mapper->addMapping(ui->minimizeOnClose, OptionsModel::MinimizeOnClose);
#endif

    /* Display */
    mapper->addMapping(ui->lang, OptionsModel::Language);
    mapper->addMapping(ui->unit, OptionsModel::DisplayUnit);
    mapper->addMapping(ui->thirdPartyTxUrls, OptionsModel::ThirdPartyTxUrls);
}

void OptionsDialog::setOkButtonState(bool fState)
{
    ui->okButton->setEnabled(fState);
}

void OptionsDialog::on_resetButton_clicked()
{
    if(model)
    {
        // confirmation dialog
        QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm options reset"),
            tr("Client restart required to activate changes.") + "<br><br>" + tr("Client will be shut down. Do you want to proceed?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

        if(btnRetVal == QMessageBox::Cancel)
            return;

        /* reset all options and close GUI */
        model->Reset();
        QApplication::quit();
    }
}

void OptionsDialog::on_openAokChainConfButton_clicked()
{
    /* explain the purpose of the config file */
    QMessageBox::information(this, tr("Configuration options"),
        tr("The configuration file is used to specify advanced user options which override GUI settings. "
           "Additionally, any command-line options will override this configuration file."));

    /* show an error if there was some problem opening the file */
    if (!GUIUtil::openAokChainConf())
        QMessageBox::critical(this, tr("Error"), tr("The configuration file could not be opened."));
}

void OptionsDialog::on_okButton_clicked()
{
    mapper->submit();
    accept();
    updateDefaultProxyNets();
    if (embedded) {
        // the page stays: say what happened
        if (model && model->isRestartRequired()) {
            showRestartWarning(true);
        } else {
            ui->statusLabel->setProperty("role", "resultOk");
            ui->statusLabel->style()->unpolish(ui->statusLabel);
            ui->statusLabel->style()->polish(ui->statusLabel);
            ui->statusLabel->setText(tr("The options were saved."));
            ui->statusLabel->show();
            QTimer::singleShot(4000, this, SLOT(clearStatusLabel()));
        }
    }
}

void OptionsDialog::on_cancelButton_clicked()
{
    if (embedded) {
        // back to what is saved
        mapper->revert();
        clearStatusLabel();
        return;
    }
    reject();
}

void OptionsDialog::on_hideTrayIcon_stateChanged(int fState)
{
    if(fState)
    {
        ui->minimizeToTray->setChecked(false);
        ui->minimizeToTray->setEnabled(false);
    }
    else
    {
        ui->minimizeToTray->setEnabled(true);
    }
}

void OptionsDialog::showRestartWarning(bool fPersistent)
{
    ui->statusLabel->setProperty("role", embedded ? "warning" : "error");
    ui->statusLabel->style()->unpolish(ui->statusLabel);
    ui->statusLabel->style()->polish(ui->statusLabel);
    ui->statusLabel->show();

    if(fPersistent)
    {
        ui->statusLabel->setText(tr("Client restart required to activate changes."));
    }
    else
    {
        ui->statusLabel->setText(tr("This change would require a client restart."));
        // clear non-persistent status label after 10 seconds
        // Todo: should perhaps be a class attribute, if we extend the use of statusLabel
        QTimer::singleShot(10000, this, SLOT(clearStatusLabel()));
    }
}

void OptionsDialog::clearStatusLabel()
{
    ui->statusLabel->clear();
    if (embedded)
        ui->statusLabel->hide();
    if (model && model->isRestartRequired()) {
        showRestartWarning(true);
    }
}

void OptionsDialog::updateProxyValidationState()
{
    QValidatedLineEdit *pUiProxyIp = ui->proxyIp;
    QValidatedLineEdit *otherProxyWidget = (pUiProxyIp == ui->proxyIpTor) ? ui->proxyIp : ui->proxyIpTor;
    if (pUiProxyIp->isValid() && (!ui->proxyPort->isEnabled() || ui->proxyPort->text().toInt() > 0) && (!ui->proxyPortTor->isEnabled() || ui->proxyPortTor->text().toInt() > 0))
    {
        setOkButtonState(otherProxyWidget->isValid()); //only enable ok button if both proxys are valid
        clearStatusLabel();
    }
    else
    {
        setOkButtonState(false);
        ui->statusLabel->setProperty("role", embedded ? "resultError" : "error");
    ui->statusLabel->style()->unpolish(ui->statusLabel);
    ui->statusLabel->style()->polish(ui->statusLabel);
    ui->statusLabel->show();
        ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
    }
}

void OptionsDialog::updateDefaultProxyNets()
{
    proxyType proxy;
    std::string strProxy;
    QString strDefaultProxyGUI;

    GetProxy(NET_IPV4, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachIPv4->setChecked(true) : ui->proxyReachIPv4->setChecked(false);

    GetProxy(NET_IPV6, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachIPv6->setChecked(true) : ui->proxyReachIPv6->setChecked(false);

    GetProxy(NET_TOR, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachTor->setChecked(true) : ui->proxyReachTor->setChecked(false);
}

ProxyAddressValidator::ProxyAddressValidator(QObject *parent) :
QValidator(parent)
{
}

QValidator::State ProxyAddressValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    // Validate the proxy
    CService serv(LookupNumeric(input.toStdString().c_str(), 9050));
    proxyType addrProxy = proxyType(serv, true);
    if (addrProxy.IsValid())
        return QValidator::Acceptable;

    return QValidator::Invalid;
}
