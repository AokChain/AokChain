// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "pastblocks.h"
#include "config/aokchain-config.h"
#endif

#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include "aokchainunits.h"
#include "bootstrap.h"
#include "guiutil.h"
#include "optionsmodel.h"

#include "validation.h" // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
#include "netbase.h"
#include "txdb.h" // for -dbcache defaults

#include <QAbstractButton>
#include <QDataWidgetMapper>
#include <QLineEdit>
#include <QDir>
#include <QTranslator>
#include <QApplication>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QCheckBox>
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
    languageNotice(nullptr),
    languageRestart(nullptr),
    walletEnabled(enableWallet),
    embedded(false),
    extraColumn(0),
    networkDialog(nullptr),
    networkStatus(nullptr),
    networkSave(nullptr)
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

    /* Block bootstrap: where the block archives come from */
    {
        QVBoxLayout *network = ui->verticalLayout_Network;
        int at = network->count();
        for (int i = 0; i < network->count(); ++i) {
            if (network->itemAt(i)->spacerItem())
                at = i;
        }
        bootstrapUse = new QCheckBox(tr("&Fast first synchronization from block archives"), ui->tabNetwork);
        bootstrapUse->setToolTip(tr("A new installation fetches the blocks as archives from the addresses below instead of block by block from other nodes. Every block is validated as usual, so the server does not have to be trusted."));
        QLabel *label = new QLabel(tr("Addresses of the block archives, one per line (mirrors are tried in this order):"), ui->tabNetwork);
        label->setWordWrap(true);
        bootstrapUrls = new QPlainTextEdit(ui->tabNetwork);
        bootstrapUrls->setFixedHeight(64);
        bootstrapUrls->setPlaceholderText(tr("Empty: the built-in address of the main network"));
        bootstrapUrls->setToolTip(QString::fromLatin1(DEFAULT_BOOTSTRAP_URL_MAIN));
        bootstrapUrls->setLineWrapMode(QPlainTextEdit::NoWrap);
        bootstrapUrls->setFont(GUIUtil::monoFont(12));
        QPushButton *folder = new QPushButton(tr("Choose a folder with block archives..."), ui->tabNetwork);
        folder->setToolTip(tr("Import block archives that you fetched yourself (the files and manifest.json in one folder) right now. Every block is validated."));
        folder->setAutoDefault(false);
        connect(folder, SIGNAL(clicked()), this, SLOT(chooseBootstrapFolder()));
        connect(bootstrapUse, SIGNAL(toggled(bool)), bootstrapUrls, SLOT(setEnabled(bool)));
        network->insertWidget(at++, bootstrapUse);
        network->insertWidget(at++, label);
        network->insertWidget(at++, bootstrapUrls);
        QHBoxLayout *row = new QHBoxLayout();
        row->addWidget(folder);
        row->addStretch();
        network->insertLayout(at++, row);
    }

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
    GUIUtil::fillLanguageCombo(ui->lang);
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
    QVBoxLayout *whole = new QVBoxLayout(body);
    whole->setContentsMargins(0, 0, 0, 0);
    whole->setSpacing(16);
    QHBoxLayout *columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 8, 0);
    columns->setSpacing(16);
    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(16);
    extraColumn = new QVBoxLayout();
    extraColumn->setSpacing(16);
    columns->addLayout(left, 1);
    columns->addLayout(extraColumn, 1);

    // The language first, across the page, with a label that can be read in any language.
    {
        QVBoxLayout *cardBody = nullptr;
        QFrame *languageCard = makeCard(QString(), this, &cardBody);
        QHBoxLayout *row = new QHBoxLayout();
        row->setSpacing(14);
        QLabel *globe = new QLabel(languageCard);
        globe->setFixedSize(40, 40);
        globe->setAlignment(Qt::AlignCenter);
        globe->setPixmap(GUIUtil::lineIcon("globe", 22, GUIUtil::theme().accent, 1.8));
        globe->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 12px; }").arg(GUIUtil::theme().accentSoft.name()));
        row->addWidget(globe);
        const QString word = tr("Language");
        QLabel *label = new QLabel(word == "Language" ? QString::fromUtf8("Language / 언어") : QString("Language / %1").arg(word), languageCard);
        label->setProperty("role", "title");
        row->addWidget(label);
        if (QLayout *displayLayout = ui->tabDisplay->layout()) {
            // out of the display card
            for (QLayout *inner : displayLayout->findChildren<QLayout*>())
                inner->removeWidget(ui->lang);
            displayLayout->removeWidget(ui->lang);
        }
        ui->langLabel->hide();
        ui->lang->setParent(languageCard);
        ui->lang->setFixedHeight(42);
        ui->lang->setMinimumWidth(180);
        ui->lang->setMaximumWidth(360);
        row->addWidget(ui->lang, 1);
        row->addStretch(1);
        // (the notice goes below the row: the row must not make the page wider than the window)
        languageNotice = new QLabel(languageCard);
        languageNotice->setProperty("role", "secondary");
        languageNotice->hide();
        languageRestart = new QPushButton(languageCard);
        languageRestart->setProperty("primary", true);
        languageRestart->setFixedHeight(40);

        languageRestart->setAutoDefault(false);
        languageRestart->hide();
        row->addWidget(languageRestart);
        cardBody->addLayout(row);
        cardBody->addWidget(languageNotice);
        whole->addWidget(languageCard);
        QWidget *columnsHost = new QWidget(body);
        columnsHost->setObjectName("settingsColumns");
        columnsHost->setStyleSheet("QWidget#settingsColumns { background: transparent; }");
        columnsHost->setLayout(columns);
        whole->addWidget(columnsHost, 1);
        connect(languageRestart, &QPushButton::clicked, this, [] {
            GUIUtil::requestRestart();
            QApplication::quit();
        });
    }

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
    // The network options are not for everyone: they are in a window that the information card opens.
    if (pages.contains(ui->tabNetwork)) {
        networkDialog = new QDialog(this);
        networkDialog->setWindowTitle(tr("Network settings"));
        networkDialog->setMinimumWidth(620);
        QVBoxLayout *dialogLayout = new QVBoxLayout(networkDialog);
        dialogLayout->setContentsMargins(24, 22, 24, 18);
        dialogLayout->setSpacing(12);
        QLabel *title = new QLabel(tr("Network settings"), networkDialog);
        title->setProperty("role", "title");
        dialogLayout->addWidget(title);
        QLabel *hint = new QLabel(tr("The defaults work for most people. Change these only if you know that your network needs it."), networkDialog);
        hint->setProperty("role", "secondary");
        hint->setWordWrap(true);
        dialogLayout->addWidget(hint);
        if (QLayout *pageLayout = ui->tabNetwork->layout()) {
            pageLayout->setContentsMargins(0, 0, 0, 0);
            for (int i = pageLayout->count() - 1; i >= 0; --i) {
                if (pageLayout->itemAt(i)->spacerItem())
                    delete pageLayout->takeAt(i);
            }
        }
        dialogLayout->addWidget(ui->tabNetwork);
        ui->tabNetwork->show();
        // a wallet that started from a chain state snapshot: the state of the past blocks, with the button to download them
        if (QFrame *pastBlocks = PastBlocks::settingsCard(networkDialog))
            dialogLayout->addWidget(pastBlocks);
        networkStatus = new QLabel(networkDialog);
        networkStatus->setProperty("role", "warning");
        networkStatus->setWordWrap(true);
        networkStatus->hide();
        dialogLayout->addWidget(networkStatus);
        QHBoxLayout *buttons = new QHBoxLayout();
        buttons->addStretch();
        QPushButton *cancel = new QPushButton(tr("Cancel"), networkDialog);
        networkSave = new QPushButton(tr("Save"), networkDialog);
        networkSave->setProperty("primary", true);
        networkSave->setDefault(true);
        for (QPushButton *button : {cancel, networkSave}) {
            button->setFixedHeight(40);
            button->setMinimumWidth(110);
            buttons->addWidget(button);
        }
        dialogLayout->addLayout(buttons);
        connect(cancel, SIGNAL(clicked()), networkDialog, SLOT(reject()));
        connect(networkSave, SIGNAL(clicked()), networkDialog, SLOT(accept()));
    }
    addPages(left, tr("Display"), {ui->tabDisplay, ui->tabWindow});
    addPages(extraColumn, tr("Wallet"), {ui->tabWallet});

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

    // command-line options that override the options above: the information card of the page takes
    // this note (overrideBlock()); it is hidden when there are none
    ui->frame->setFrameShape(QFrame::NoFrame);
    if (ui->frame->layout()) {
        ui->frame->layout()->setContentsMargins(0, 4, 0, 0);
        ui->frame->layout()->setSpacing(2);
    }
    ui->overriddenByCommandLineInfoLabel->setProperty("role", "caption");
    ui->overriddenByCommandLineLabel->setFont(GUIUtil::monoFont(11));
    ui->overriddenByCommandLineLabel->setProperty("role", "caption");
    ui->overriddenByCommandLineLabel->setWordWrap(true);
    ui->overriddenByCommandLineLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui->frame->setParent(this);
    ui->frame->hide();
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

// tr("Restart to apply") and tr("Restart now") are looked up in the translation of the chosen language
static const char *languageStrings[] = {QT_TRANSLATE_NOOP("OptionsDialog", "Restart to apply"), QT_TRANSLATE_NOOP("OptionsDialog", "Restart now")};

QWidget *OptionsDialog::overrideBlock()
{
    return embedded ? ui->frame : nullptr;
}

void OptionsDialog::showNetworkDialog()
{
    if (!networkDialog || !model)
        return;
    // what the fields hold now, so that "Cancel" leaves other unsaved changes of the page alone
    const QList<QWidget*> fields = ui->tabNetwork->findChildren<QWidget*>();
    QList<QPair<QWidget*, QVariant> > before;
    for (QWidget *field : fields) {
        if (QAbstractButton *button = qobject_cast<QAbstractButton*>(field)) {
            if (button->isCheckable())
                before.append(qMakePair(field, QVariant(button->isChecked())));
        } else if (QLineEdit *edit = qobject_cast<QLineEdit*>(field)) {
            before.append(qMakePair(field, QVariant(edit->text())));
        } else if (QPlainTextEdit *text = qobject_cast<QPlainTextEdit*>(field)) {
            before.append(qMakePair(field, QVariant(text->toPlainText())));
        }
    }
    networkStatus->hide();
    if (networkDialog->exec() == QDialog::Accepted) {
        // the same as "Save" of the page
        on_okButton_clicked();
        return;
    }
    for (const QPair<QWidget*, QVariant> &entry : before) {
        if (QAbstractButton *button = qobject_cast<QAbstractButton*>(entry.first))
            button->setChecked(entry.second.toBool());
        else if (QLineEdit *edit = qobject_cast<QLineEdit*>(entry.first))
            edit->setText(entry.second.toString());
        else if (QPlainTextEdit *text = qobject_cast<QPlainTextEdit*>(entry.first))
            text->setPlainText(entry.second.toString());
    }
    clearStatusLabel();
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
        if (embedded)
            ui->frame->setVisible(!strLabel.isEmpty());
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
    connect(bootstrapUse, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    connect(bootstrapUrls, SIGNAL(textChanged()), this, SLOT(showRestartWarning()));
    /* Display */
    connect(ui->lang, SIGNAL(valueChanged()), this, SLOT(showRestartWarning()));
    connect(ui->lang, SIGNAL(activated(int)), this, SLOT(languageChosen()));
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
    mapper->addMapping(bootstrapUse, OptionsModel::BootstrapUse);
    mapper->addMapping(bootstrapUrls, OptionsModel::BootstrapUrls, "plainText");

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
    if (networkSave)
        networkSave->setEnabled(fState);
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

/** The language is stored at once: no "Save" in a language that the user may not read. */
void OptionsDialog::languageChosen()
{
    if (!embedded || !model || !languageNotice)
        return;
    const QString code = ui->lang->currentData().toString();
    model->setData(model->index(OptionsModel::Language, 0), code, Qt::EditRole);
    // in the language that was chosen and in English
    QString chosen = "Restart to apply";
    QString button = "Restart now";
    QTranslator translator;
    QString name = code.isEmpty() ? QLocale::system().name() : code;
    if (translator.load(name, ":/translations/") || translator.load(name.left(name.indexOf('_')), ":/translations/")) {
        const QString localNotice = translator.translate("OptionsDialog", "Restart to apply");
        const QString localButton = translator.translate("OptionsDialog", "Restart now");
        if (!localNotice.isEmpty())
            chosen = QString("%1 / Restart to apply").arg(localNotice);
        if (!localButton.isEmpty())
            button = localButton; // the line below says it in English as well
    }
    languageNotice->setText(chosen);
    languageNotice->show();
    languageRestart->setText(button);
    languageRestart->show();
}

void OptionsDialog::chooseBootstrapFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder with block archives (manifest.json and the blocks-... files)"));
    if (folder.isEmpty())
        return;
    std::string error;
    if (StartBootstrapFromFolder(folder.toStdString(), error))
        QMessageBox::information(this, tr("Block archives"), tr("The import has started. The side bar shows how far it is."));
    else
        QMessageBox::warning(this, tr("Block archives"), error == "a bootstrap is running already"
            ? tr("A fast sync is running. Skip it first (sync window), then choose the folder.")
            : tr("This folder has no manifest.json. Choose the folder that holds manifest.json and the blocks-... files."));
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
        if (networkStatus && networkDialog && networkDialog->isVisible()) {
            networkStatus->setText(ui->statusLabel->text());
            networkStatus->show();
        }
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
        if (networkStatus)
            networkStatus->hide();
    }
    else
    {
        setOkButtonState(false);
        ui->statusLabel->setProperty("role", embedded ? "resultError" : "error");
    ui->statusLabel->style()->unpolish(ui->statusLabel);
    ui->statusLabel->style()->polish(ui->statusLabel);
    ui->statusLabel->show();
        ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
        if (networkStatus) {
            networkStatus->setText(ui->statusLabel->text());
            networkStatus->show();
        }
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
