// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokensdialog.h"
#include "sendconfirmdialog.h"
#include "tokenlists.h"
#include "sendcoinsdialog.h"
#include "ui_tokensdialog.h"

#include "addresstablemodel.h"
#include "aokchainunits.h"
#include "clientmodel.h"
#include "tokencontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "pagewidgets.h"
#include "tokenlists.h"
#include "platformstyle.h"
#include "sendtokensentry.h"
#include "walletmodel.h"
#include "tokentablemodel.h"

#include "base58.h"
#include "chainparams.h"
#include "wallet/coincontrol.h"
#include "validation.h" // mempool and minRelayTxFee
#include "ui_interface.h"
#include "txmempool.h"
#include "policy/fees.h"
#include "wallet/fees.h"
#include "createtokendialog.h"
#include "reissuetokendialog.h"
#include "guiconstants.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>
#include <policy/policy.h>
#include <core_io.h>
#include <rpc/mining.h>

TokensDialog::TokensDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::TokensDialog),
        clientModel(0),
        model(0),
        fNewRecipientAllowed(true),
        fFeeMinimized(true),
        platformStyle(_platformStyle)
{
    ui->setupUi(this);
    ui->sendButton->setProperty("primary", true);
    buildLayout();

    ui->addButton->setIcon(QIcon(GUIUtil::lineIcon("plus", 14, GUIUtil::theme().label, 2.4)));
    ui->addButton->setIconSize(QSize(14, 14));
    ui->sendButton->setIcon(QIcon());

    GUIUtil::setupAddressWidget(ui->lineEditTokenControlChange, this);

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    connect(ui->pushButtonTokenControl, SIGNAL(clicked()), this, SLOT(tokenControlButtonClicked()));
    connect(ui->checkBoxTokenControlChange, SIGNAL(stateChanged(int)), this, SLOT(tokenControlChangeChecked(int)));
    connect(ui->lineEditTokenControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(tokenControlChangeEdited(const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(tokenControlClipboardChange()));
    ui->labelTokenControlQuantity->addAction(clipboardQuantityAction);
    ui->labelTokenControlAmount->addAction(clipboardAmountAction);
    ui->labelTokenControlFee->addAction(clipboardFeeAction);
    ui->labelTokenControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelTokenControlBytes->addAction(clipboardBytesAction);
    ui->labelTokenControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelTokenControlChange->addAction(clipboardChangeAction);

    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_TRANSACTION_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);
    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

    /** TOKENS START */
    setupTokenControlFrame(platformStyle);
    setupScrollView(platformStyle);
    setupFeeControl(platformStyle);
    /** TOKENS END */
}

void TokensDialog::buildLayout()
{
    const GUIUtil::Theme& theme = GUIUtil::theme();

    // the layouts of the form go; the widgets stay
    delete layout();
    QHBoxLayout *columns = new QHBoxLayout(this);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(20);

    QFrame *formCard = new QFrame(this);
    formCard->setProperty("role", "card");
    QVBoxLayout *form = new QVBoxLayout(formCard);
    form->setContentsMargins(22, 16, 16, 18);
    form->setSpacing(12);

    QHBoxLayout *formHeader = new QHBoxLayout();
    formHeader->setContentsMargins(0, 0, 6, 0);
    QLabel *formTitle = new QLabel(tr("Token to send"), formCard);
    formTitle->setProperty("role", "title");
    formTitle->setFont(GUIUtil::appFont(15, QFont::Medium));
    formHeader->addWidget(formTitle);
    formHeader->addStretch();
    ui->clearButton->setText(QString());
    ui->clearButton->setProperty("look", "icon");
    ui->clearButton->setIcon(QIcon(GUIUtil::lineIcon("remove", 16, theme.label, 2.2)));
    ui->clearButton->setIconSize(QSize(16, 16));
    ui->clearButton->setFixedSize(32, 32);
    ui->clearButton->setToolTip(tr("Clear all"));
    ui->clearButton->setCursor(Qt::PointingHandCursor);
    formHeader->addWidget(ui->clearButton);
    form->addLayout(formHeader);

    // The root tokens of the wallet as round buttons; sub tokens and unique tokens are chosen
    // from a list with a search field. Below: the chosen token with its name and what is available.
    heldTokens = nullptr; // the index of the wallet's token table, known with the model
    picker = new TokenPicker(formCard);
    form->addWidget(picker);
    QHBoxLayout *pickedRow = new QHBoxLayout();
    pickedRow->setContentsMargins(0, 0, 6, 0);
    pickedRow->setSpacing(10);
    // the small logo of the chosen token sits under the first round button
    pickedLogo = new QLabel(formCard);
    pickedLogo->setFixedSize(40, 24);
    pickedLogo->setAlignment(Qt::AlignCenter);
    pickedLogo->hide();
    pickedRow->addWidget(pickedLogo);
    pickedName = new QLabel(formCard);
    pickedName->setFont(GUIUtil::appFont(14, QFont::Medium));
    pickedRow->addWidget(pickedName);
    belowButton = new QPushButton(formCard);
    belowButton->setProperty("look", "chipButton");
    belowButton->setLayoutDirection(Qt::RightToLeft);
    belowButton->setIcon(QIcon(GUIUtil::lineIcon("chevron-right", 11, theme.label, 2.6)));
    belowButton->setIconSize(QSize(11, 11));
    belowButton->setCursor(Qt::PointingHandCursor);
    belowButton->setToolTip(tr("Choose a sub token or unique token of this root"));
    belowButton->hide();
    connect(belowButton, SIGNAL(clicked()), this, SLOT(chooseBelow()));
    pickedRow->addWidget(belowButton);
    pickedRow->addStretch();
    QLabel *availableCaption = new QLabel(tr("Available"), formCard);
    availableCaption->setProperty("role", "secondary");
    pickedRow->addWidget(availableCaption);
    pickedAvailable = new AmountLabel(15, QFont::Medium, formCard);
    pickedRow->addWidget(pickedAvailable);
    form->addLayout(pickedRow);
    connect(picker, SIGNAL(tokenSelected(QString)), this, SLOT(tokenPicked(QString)));

    form->addWidget(ui->scrollArea, 1);
    ui->addButton->setProperty("look", "dashed");
    ui->addButton->setText(tr("Add recipient"));
    ui->addButton->setFixedHeight(40);
    ui->addButton->setCursor(Qt::PointingHandCursor);
    form->addWidget(ui->addButton, 0, Qt::AlignLeft);
    form->addWidget(ui->frameFee);
    form->addWidget(ui->frameTokenControl);
    columns->addWidget(formCard, 3);

    summary = new SummaryCard(tr("Summary"), this);
    summary->setNote(tr("A transaction that has been sent cannot be cancelled. Check the address once more."));
    ui->label->hide();
    ui->labelBalance->hide();
    ui->sendButton->setProperty("look", "send");
    ui->sendButton->setFixedHeight(52);
    ui->sendButton->setCursor(Qt::PointingHandCursor);
    summary->addFooterWidget(ui->sendButton);
    columns->addWidget(summary, 2);
}

void TokensDialog::renumberEntries()
{
    int number = 0;
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if (entry && !entry->isHidden())
            entry->setIndex(++number);
    }
}

/** The token index follows the token table of the wallet; make sure the choice is still shown. */
void TokensDialog::refreshTokens()
{
    tokenPicked(picker->current());
}

void TokensDialog::chooseBelow()
{
    const QString root = TokenNames::root(picker->current());
    if (!root.isEmpty())
        picker->chooseBelow(root);
}

void TokensDialog::selectToken(const QString &name)
{
    picker->setCurrent(name);
    tokenPicked(picker->current());
}

void TokensDialog::tokenPicked(const QString &name)
{
    SendTokensEntry *first = nullptr;
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if (entry && !entry->isHidden()) {
            entry->selectToken(name);
            if (!first)
                first = entry;
        }
    }
    // "N sub tokens ›" next to the name of a root that has held tokens below it
    const RootToken *root = name.isEmpty() || !heldTokens ? nullptr : heldTokens->findRoot(TokenNames::root(name));
    belowButton->setVisible(root && root->children > 0);
    if (root)
        belowButton->setText(tr("%1 sub tokens").arg(root->children));
    pickedLogo->setVisible(!name.isEmpty());
    if (name.isEmpty()) {
        pickedName->setText(picker->isEmpty() ? tr("The wallet holds no tokens") : tr("Choose the token to send"));
        pickedName->setProperty("role", "secondary");
        pickedAvailable->setAmount(QString::fromUtf8("–"));
    } else {
        // a sub or unique token: its full name with the logo of its root
        pickedLogo->setPixmap(GUIUtil::tokenLogo(name, 24));
        pickedName->setText(QFontMetrics(pickedName->font()).elidedText(name, Qt::ElideMiddle, 260));
        pickedName->setToolTip(name);
        pickedName->setProperty("role", QVariant());
        pickedAvailable->setAmount(first ? GUIUtil::withThousands(QString::fromStdString(ValueFromAmountString(first->tokenBalance, first->tokenUnits))) : QString::fromUtf8("–"));
    }
    pickedName->style()->unpolish(pickedName);
    pickedName->style()->polish(pickedName);
    updateSummary();
}

void TokensDialog::updateSummary()
{
    if (!model || !model->getOptionsModel())
        return;
    const int unit = model->getOptionsModel()->getDisplayUnit();
    const QString unitName = AokChainUnits::name(unit);
    const QString dash = QString::fromUtf8("–");
    const QString token = picker->current();

    QList<SendTokensEntry*> entries;
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if (entry && !entry->isHidden())
            entries << entry;
    }
    CAmount total = 0, available = 0;
    int units = 0;
    bool complete = !entries.isEmpty();
    for (SendTokensEntry *entry : entries) {
        total += entry->amount();
        complete = complete && entry->isComplete();
        available = entry->tokenBalance;
        units = entry->tokenUnits;
    }
    auto quantity = [units](const CAmount& amount) {
        return GUIUtil::withThousands(QString::fromStdString(ValueFromAmountString(amount, units)));
    };

    // fee of a typical token transfer at the chosen rate
    CCoinControl ctrl;
    updateTokenControlState(ctrl);
    const CAmount fee = GetMinimumFee(300 + 80 * entries.size(), ctrl, ::mempool, ::feeEstimator, nullptr);
    const CAmount balance = model->getBalance();

    QList<SummaryCard::Row> rows;
    rows << SummaryCard::Row(tr("Token"), token.isEmpty() ? dash : token, QString(), false);
    int number = 0;
    for (SendTokensEntry *entry : entries) {
        ++number;
        const QString address = entry->addressText();
        const bool valid = model->validateAddress(address);
        SummaryCard::Row row(entries.size() > 1 ? tr("Recipient %1").arg(number) : tr("Recipient"), dash, QString(), false);
        if (!address.isEmpty()) {
            row.value = !entry->labelText().isEmpty() ? entry->labelText() : (valid ? GUIUtil::shortAddress(address) : address);
            row.value = QFontMetrics(GUIUtil::appFont(13, QFont::Medium)).elidedText(row.value, Qt::ElideMiddle, 200);
            row.mono = valid && entry->labelText().isEmpty();
            row.toolTip = GUIUtil::monoToolTip(address, entry->labelText());
        }
        rows << row;
        if (entries.size() > 1)
            rows << SummaryCard::Row(tr("Quantity %1").arg(number), entry->amount() > 0 ? quantity(entry->amount()) : dash, QString());
        if (entry->lockTime().isValid()) {
            SummaryCard::Row lock(entries.size() > 1 ? tr("Locked until (%1)").arg(number) : tr("Locked until"), GUIUtil::isoDateTime(entry->lockTime()), QString(), false);
            lock.mono = true;
            rows << lock;
        }
    }
    if (!token.isEmpty()) {
        SummaryCard::Row left(tr("Left of the token"), quantity(available - total), QString());
        left.alert = total > available;
        rows << left;
    }
    SummaryCard::Row feeRow(tr("Fee (estimated)"), GUIUtil::withThousands(AokChainUnits::format(unit, fee, false, AokChainUnits::separatorNever)), unitName);
    feeRow.toolTip = tr("The fee is paid in AOK. The fee that is really paid is shown for confirmation before the transaction is sent.");
    rows << feeRow;
    SummaryCard::Row aok(tr("AOK balance"), GUIUtil::withThousands(AokChainUnits::format(unit, balance, false, AokChainUnits::separatorNever)), unitName);
    aok.alert = balance < fee;
    rows << aok;
    summary->setTotal(tr("Quantity to send"), quantity(total), token);
    summary->setRows(rows);

    const bool ready = complete && !token.isEmpty() && total > 0 && total <= available && balance >= fee;
    ui->sendButton->setEnabled(ready);
    ui->sendButton->setProperty("primary", ready);
    ui->sendButton->style()->unpolish(ui->sendButton);
    ui->sendButton->style()->polish(ui->sendButton);
    if (ready) {
        // "Send 12.5 DZTB"
        QString shortQuantity = quantity(total);
        if (shortQuantity.contains('.')) {
            while (shortQuantity.endsWith('0'))
                shortQuantity.chop(1);
            if (shortQuantity.endsWith('.'))
                shortQuantity.chop(1);
        }
        ui->sendButton->setText(tr("Send %1").arg(shortQuantity + " " + token));
        ui->sendButton->setToolTip(QString());
    } else {
        ui->sendButton->setText(tr("Send"));
        ui->sendButton->setToolTip(total > available && !token.isEmpty() ? tr("The quantity exceeds what the wallet holds of this token.")
                                                                        : tr("Choose a token and enter a valid address and a quantity for every recipient."));
    }
}

void TokensDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void TokensDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(_model);
            }
        }

        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(tokenControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(tokenControlFeatureChanged(bool)));

        // Custom Fee Control
        connect(_model->getOptionsModel(), SIGNAL(customFeeFeaturesChanged(bool)), this, SLOT(customFeeFeatureChanged(bool)));


        heldTokens = HeldTokenIndex::forModel(_model->getTokenTableModel());
        picker->setIndex(heldTokens);
        refreshTokens();
        ui->frameTokenControl->setVisible(false);
        ui->frameTokenControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        ui->frameFee->setVisible(_model->getOptionsModel()->getCustomFeeFeatures());
        tokenControlUpdateLabels();

        // fee section
        for (const int &n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(tokenControlUpdateLabels()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(tokenControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(tokenControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(tokenControlUpdateLabels()));
//        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(updateSmartFeeLabel()));
//        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(tokenControlUpdateLabels()));
        ui->customFee->setSingleStep(GetRequiredFee(1000));
        updateFeeSectionControls();
        updateMinFeeLabel();
        updateSmartFeeLabel();

        // set default rbf checkbox state
//        ui->optInRBF->setCheckState(model->getDefaultWalletRbf() ? Qt::Checked : Qt::Unchecked);
        ui->optInRBF->hide();

        // set the smartfee-sliders default value (wallets default conf.target or last stored value)
        QSettings settings;
        if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
            // migrate nSmartFeeSliderPosition to nConfTarget
            // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
            int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
            settings.setValue("nConfTarget", nConfirmTarget);
            settings.remove("nSmartFeeSliderPosition");
        }
        if (settings.value("nConfTarget").toInt() == 0)
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->getDefaultConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));
    }
}

TokensDialog::~TokensDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void TokensDialog::setupTokenControlFrame(const PlatformStyle *platformStyle)
{
    /** Update the tokencontrol frame */
    ui->frameTokenControl->setStyleSheet(QString(".QFrame { background-color: %1; border: none; border-radius: 8px; padding: 10px 8px; }").arg(GUIUtil::theme().surfaceAlt.name()));
    ui->widgetTokenControl->setStyleSheet(".QWidget {background-color: transparent;}");
    /** Create the shadow effects on the frames */

    ui->labelTokenControlFeatures->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlFeatures->setFont(GUIUtil::getTopLabelFont());

    ui->labelTokenControlQuantityText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlQuantityText->setFont(GUIUtil::getSubLabelFont());

    ui->labelTokenControlAmountText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlAmountText->setFont(GUIUtil::getSubLabelFont());

    ui->labelTokenControlFeeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlFeeText->setFont(GUIUtil::getSubLabelFont());

    ui->labelTokenControlAfterFeeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlAfterFeeText->setFont(GUIUtil::getSubLabelFont());

    ui->labelTokenControlBytesText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlBytesText->setFont(GUIUtil::getSubLabelFont());

    ui->labelTokenControlLowOutputText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlLowOutputText->setFont(GUIUtil::getSubLabelFont());

    ui->labelTokenControlChangeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenControlChangeText->setFont(GUIUtil::getSubLabelFont());

    // Align the other labels next to the input buttons to the text in the same height
    ui->labelTokenControlAutomaticallySelected->setStyleSheet(STRING_LABEL_COLOR);

    // Align the Custom change address checkbox
    ui->checkBoxTokenControlChange->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->labelTokenControlQuantity->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlAmount->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlAfterFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlBytes->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlLowOutput->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->checkBoxTokenControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->lineEditTokenControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlInsuffFunds->setFont(GUIUtil::getSubLabelFont());
    ui->labelTokenControlAutomaticallySelected->setFont(GUIUtil::getSubLabelFont());

}

void TokensDialog::setupScrollView(const PlatformStyle *platformStyle)
{
    /** Update the scrollview*/
    ui->scrollArea->setStyleSheet(".QScrollArea { background: transparent; border: none; }");

    ui->entries->setContentsMargins(0,0,6,0);
    ui->entries->setSpacing(18);
    ui->scrollAreaWidgetContents->setStyleSheet(".QWidget { background: transparent; }");
}

void TokensDialog::setupFeeControl(const PlatformStyle *platformStyle)
{
    /** Update the fee frame */
    ui->frameFee->setStyleSheet(QString(".QFrame#frameFee { background-color: %1; border: none; border-radius: 8px; padding: 6px 8px; }").arg(GUIUtil::theme().surfaceAlt.name()));
    /** Create the shadow effects on the frames */

    ui->labelFeeHeadline->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelFeeHeadline->setFont(GUIUtil::getSubLabelFont());

    ui->labelSmartFee3->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCustomPerKilobyte->setStyleSheet(QString(".QLabel{ %1; }").arg(STRING_LABEL_COLOR));
    ui->radioSmartFee->setStyleSheet(STRING_LABEL_COLOR);
    ui->radioCustomFee->setStyleSheet(STRING_LABEL_COLOR);
    ui->checkBoxMinimumFee->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->buttonChooseFee->setFont(GUIUtil::getSubLabelFont());
    ui->fallbackFeeWarningLabel->setFont(GUIUtil::getSubLabelFont());
    ui->buttonMinimizeFee->setFont(GUIUtil::getSubLabelFont());
    ui->radioSmartFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelSmartFee2->setFont(GUIUtil::getSubLabelFont());
    ui->labelSmartFee3->setFont(GUIUtil::getSubLabelFont());
    ui->confTargetSelector->setFont(GUIUtil::getSubLabelFont());
    ui->radioCustomFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelCustomPerKilobyte->setFont(GUIUtil::getSubLabelFont());
    ui->customFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelMinFeeWarning->setFont(GUIUtil::getSubLabelFont());
    ui->optInRBF->setFont(GUIUtil::getSubLabelFont());
    ui->sendButton->setFont(GUIUtil::getSubLabelFont());
    ui->clearButton->setFont(GUIUtil::getSubLabelFont());
    ui->addButton->setFont(GUIUtil::getSubLabelFont());
    ui->labelSmartFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelFeeEstimation->setFont(GUIUtil::getSubLabelFont());
    ui->labelFeeMinimized->setFont(GUIUtil::getSubLabelFont());

}

void TokensDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendTokensRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate())
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    fNewRecipientAllowed = false;
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return;
    }

    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    // ToDo: transaction message
    std::string txMessage;

    for (auto recipient : recipients) {
        std::string recipientAddress = recipient.address.toStdString();

        if (recipientAddress == "") {
            QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
            msgParams.second = CClientUIInterface::MSG_WARNING;
            msgParams.first = tr("There is no address associated with username you specified. Please recheck.");

            Q_EMIT message(tr("Send Coins"), msgParams.first, msgParams.second);
            return;
        }

        vTransfers.emplace_back(std::make_pair(CTokenTransfer(recipient.tokenName.toStdString(), recipient.amount, recipient.tokenLockTime), recipientAddress));
    }

    // Always use a CCoinControl instance, use the TokenControlDialog instance if CoinControl has been enabled
    CCoinControl ctrl;
    if (model->getOptionsModel()->getCoinControlFeatures())
        ctrl = *TokenControlDialog::tokenControl;

    updateTokenControlState(ctrl);

    CWalletTx tx;
    CReserveKey reservekey(model->getWallet());
    std::pair<int, std::string> error;
    CAmount nFeeRequired;

    if (!CreateTransferTokenTransaction(model->getWallet(), ctrl, vTransfers, "", error, tx, reservekey, nFeeRequired, txMessage)) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString(error.second));
        msgBox.exec();
        return;
    }

    // What is about to be sent, for the confirmation: the token first, so that it cannot be
    // mistaken for the coin
    const int unit = model->getOptionsModel()->getDisplayUnit();
    SendConfirmData confirm;
    confirm.token = true;
    confirm.asset = recipients.first().tokenName;
    confirm.feeUnit = AokChainUnits::name(unit).trimmed();
    HeldToken held;
    HeldTokenIndex *heldTokens = HeldTokenIndex::forModel(model->getTokenTableModel());
    const bool haveBalance = heldTokens && heldTokens->find(confirm.asset, &held) && held.held;
    const int tokenUnits = haveBalance ? held.units : 8;
    auto number = [tokenUnits](const CAmount &amount) { return QString::fromStdString(ValueFromAmountString(amount, tokenUnits)); };
    CAmount totalTokens = 0;
    for (SendTokensRecipient &rcp : recipients)
    {
        SendConfirmData::Recipient to;
        to.address = rcp.address;
        to.label = rcp.label.isEmpty() ? model->getAddressTableModel()->labelForAddress(rcp.address) : rcp.label;
        to.amount = number(rcp.amount);
        to.known = model->isKnownAddress(rcp.address);
        if (rcp.tokenName != confirm.asset)
            to.note = rcp.tokenName; // a card with another token (owner tokens of the advanced view)
        if (rcp.tokenLockTime > 0)
            to.note = tr("Locked until %1").arg(QDateTime::fromTime_t(rcp.tokenLockTime).toString("yyyy.MM.dd HH:mm"));
        if (rcp.tokenName == confirm.asset)
            totalTokens += rcp.amount;
        confirm.recipients.append(to);
    }
    confirm.total = number(totalTokens);
    confirm.fee = AokChainUnits::format(unit, nFeeRequired, false, AokChainUnits::separatorNever);
    confirm.assetDeducted = confirm.total;
    if (haveBalance) {
        confirm.assetAfter = number(std::max<CAmount>(0, held.quantity - totalTokens));
        confirm.wholeBalance = totalTokens >= held.quantity;
    }
    confirm.coinAfter = AokChainUnits::format(unit, std::max<CAmount>(0, model->getBalance() - nFeeRequired), false, AokChainUnits::separatorNever);

    if (!SendConfirmDialog::confirm(confirm, this))
    {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendTokens(tx, recipients, reservekey);
    // process sendStatus and on error generate message shown to user
    processSendCoinsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        TokenControlDialog::tokenControl->UnSelectAll();
        tokenControlUpdateLabels();
        accept();
        // (after the page has tidied up: widgets it let go of are deleted when the event loop is back)
        const QString txid = QString::fromStdString(tx.GetHash().GetHex());
        QTimer::singleShot(0, this, [this, confirm, txid] { SendConfirmDialog::showSent(confirm, txid, this); });
    }
    fNewRecipientAllowed = true;
}

void TokensDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void TokensDialog::reject()
{
    clear();
}

void TokensDialog::accept()
{
    clear();
}

SendTokensEntry *TokensDialog::addEntry()
{
    // An entry is not given the names of all tokens: there can be tens of thousands.
    // It gets the token that is chosen for the page.
    QStringList list;
    bool fIsOwner = false;
    bool fIsTokenControl = false;
    if (TokenControlDialog::tokenControl->HasTokenSelected()) {
        list << QString::fromStdString(TokenControlDialog::tokenControl->strTokenSelected);
        fIsOwner = IsTokenNameAnOwner(TokenControlDialog::tokenControl->strTokenSelected);
        fIsTokenControl = true;
    }

    SendTokensEntry *entry = new SendTokensEntry(platformStyle, list, this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendTokensEntry*)), this, SLOT(removeEntry(SendTokensEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(tokenControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(tokenControlUpdateLabels()));
    connect(entry, SIGNAL(recipientChanged()), this, SLOT(updateSummary()));
    for (QPushButton *button : findChildren<QPushButton*>())
        button->setAutoDefault(false);

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocusTokenListBox();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    entry->IsTokenControl(fIsTokenControl, fIsOwner);

    if (list.size() == 1)
        entry->setCurrentIndex(1);

    // the token is chosen for the page
    if (!picker->current().isEmpty())
        entry->selectToken(picker->current());
    renumberEntries();
    updateTabsAndLabels();
    updateSummary();

    return entry;
}

void TokensDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    tokenControlUpdateLabels();
}

void TokensDialog::removeEntry(SendTokensEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    renumberEntries();
    updateTabsAndLabels();
    updateSummary();
}

QWidget *TokensDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return ui->addButton;
}

void TokensDialog::setAddress(const QString &address)
{
    SendTokensEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendTokensEntry *first = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void TokensDialog::pasteEntry(const SendTokensRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendTokensEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendTokensEntry *first = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool TokensDialog::handlePaymentRequest(const SendTokensRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void TokensDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                                 const CAmount& watchBalance, const CAmount& watchUnconfirmedBalance, const CAmount& watchImmatureBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchBalance);
    Q_UNUSED(watchUnconfirmedBalance);
    Q_UNUSED(watchImmatureBalance);

    ui->labelBalance->setFont(GUIUtil::monoFont(13, QFont::Medium));
    ui->label->setFont(GUIUtil::getSubLabelFont());

    if(model && model->getOptionsModel())
    {
        ui->labelBalance->setText(AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
        updateSummary();
    }
}

void TokensDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0);
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void TokensDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendCoinsDialog usage of WalletModel::SendCoinsReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendCoins()
    // all others are used only in WalletModel::prepareTransaction()
    switch(sendCoinsReturn.status)
    {
        case WalletModel::InvalidAddress:
            msgParams.first = tr("The recipient address is not valid. Please recheck.");
            break;
        case WalletModel::InvalidUsername:
            msgParams.first = tr("There is no address associated with username you specified. Please recheck.");
            break;
        case WalletModel::InvalidAmount:
            msgParams.first = tr("The amount to pay must be larger than 0.");
            break;
        case WalletModel::AmountExceedsBalance:
            msgParams.first = tr("The amount exceeds your balance.");
            break;
        case WalletModel::AmountWithFeeExceedsBalance:
            msgParams.first = tr("The total exceeds your balance when the %1 transaction fee is included.").arg(msgArg);
            break;
        case WalletModel::DuplicateAddress:
            msgParams.first = tr("Duplicate address found: addresses should only be used once each.");
            break;
        case WalletModel::TransactionCreationFailed:
            msgParams.first = tr("Transaction creation failed!");
            msgParams.second = CClientUIInterface::MSG_ERROR;
            break;
        case WalletModel::TransactionCommitFailed:
            msgParams.first = tr("The transaction was rejected with the following reason: %1").arg(sendCoinsReturn.reasonCommitFailed);
            msgParams.second = CClientUIInterface::MSG_ERROR;
            break;
        case WalletModel::AbsurdFee:
            msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee.").arg(AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), maxTxFee));
            break;
        case WalletModel::PaymentRequestExpired:
            msgParams.first = tr("Payment request expired.");
            msgParams.second = CClientUIInterface::MSG_ERROR;
            break;
            // included to prevent a compiler warning.
        case WalletModel::OK:
        default:
            return;
    }

    Q_EMIT message(tr("Send Coins"), msgParams.first, msgParams.second);
}

void TokensDialog::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void TokensDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void TokensDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void TokensDialog::setMinimumFee()
{
    ui->customFee->setValue(GetRequiredFee(1000));
}

void TokensDialog::updateFeeSectionControls()
{
    ui->confTargetSelector      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee           ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->checkBoxMinimumFee      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelMinFeeWarning      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelCustomPerKilobyte  ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
    ui->customFee               ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
}

void TokensDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void TokensDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
                AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), GetRequiredFee(1000)) + "/kB")
        );
}

void TokensDialog::updateTokenControlState(CCoinControl& ctrl)
{
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
//    ctrl.signalRbf = ui->optInRBF->isChecked();
}

void TokensDialog::updateSmartFeeLabel()
{
    if(!model || !model->getOptionsModel())
        return;
    CCoinControl coin_control;
    updateTokenControlState(coin_control);
    coin_control.m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
    FeeCalculation feeCalc;
    CFeeRate feeRate = CFeeRate(GetMinimumFee(1000, coin_control, ::mempool, ::feeEstimator, &feeCalc));

    ui->labelSmartFee->setText(AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK()) + "/kB");

    if (feeCalc.reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
        int lightness = ui->fallbackFeeWarningLabel->palette().color(QPalette::WindowText).lightness();
        QColor warning_colour(255 - (lightness / 5), 176 - (lightness / 3), 48 - (lightness / 14));
        ui->fallbackFeeWarningLabel->setStyleSheet("QLabel { color: " + warning_colour.name() + "; }");
        ui->fallbackFeeWarningLabel->setIndent(QFontMetrics(ui->fallbackFeeWarningLabel->font()).width("x"));
    }
    else
    {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(tr("Estimated to begin confirmation within %n block(s).", "", feeCalc.returnedTarget));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

// Coin Control: copy label "Quantity" to clipboard
void TokensDialog::tokenControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelTokenControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void TokensDialog::tokenControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelTokenControlAmount->text().left(ui->labelTokenControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void TokensDialog::tokenControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelTokenControlFee->text().left(ui->labelTokenControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void TokensDialog::tokenControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelTokenControlAfterFee->text().left(ui->labelTokenControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void TokensDialog::tokenControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelTokenControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void TokensDialog::tokenControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelTokenControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void TokensDialog::tokenControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelTokenControlChange->text().left(ui->labelTokenControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void TokensDialog::tokenControlFeatureChanged(bool checked)
{
    ui->frameTokenControl->setVisible(checked);

    if (!checked && model) // coin control features disabled
        TokenControlDialog::tokenControl->SetNull();

    tokenControlUpdateLabels();
}

void TokensDialog::customFeeFeatureChanged(bool checked)
{
    ui->frameFee->setVisible(checked);
}

// Coin Control: button inputs -> show actual coin control dialog
void TokensDialog::tokenControlButtonClicked()
{
    TokenControlDialog dlg(platformStyle);
    dlg.setModel(model);
    dlg.exec();
    tokenControlUpdateLabels();
    tokenControlUpdateSendCoinsDialog();
}

// Coin Control: checkbox custom change address
void TokensDialog::tokenControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        TokenControlDialog::tokenControl->destChange = CNoDestination();
        ui->labelTokenControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        tokenControlChangeEdited(ui->lineEditTokenControlChange->text());

    ui->lineEditTokenControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void TokensDialog::tokenControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        TokenControlDialog::tokenControl->destChange = CNoDestination();
        ui->labelTokenControlChangeLabel->setStyleSheet("QLabel{color:red;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelTokenControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelTokenControlChangeLabel->setText(tr("Warning: Invalid AokChain address"));
        }
        else // Valid address
        {
            if (!model->IsSpendable(dest)) {
                ui->labelTokenControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                                                                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    TokenControlDialog::tokenControl->destChange = dest;
                else
                {
                    ui->lineEditTokenControlChange->setText("");
                    ui->labelTokenControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                    ui->labelTokenControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelTokenControlChangeLabel->setStyleSheet("QLabel{color:black;}");

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelTokenControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelTokenControlChangeLabel->setText(tr("(no label)"));

                TokenControlDialog::tokenControl->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void TokensDialog::tokenControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateTokenControlState(*TokenControlDialog::tokenControl);

    // set pay amounts
    TokenControlDialog::payAmounts.clear();
    TokenControlDialog::fSubtractFeeFromAmount = false;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendTokensRecipient rcp = entry->getValue();
            TokenControlDialog::payAmounts.append(rcp.amount);
//            if (rcp.fSubtractFeeFromAmount)
//                TokenControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (TokenControlDialog::tokenControl->HasTokenSelected())
    {
        // actual coin control calculation
        TokenControlDialog::updateLabels(model, this);

        // show coin control stats
        ui->labelTokenControlAutomaticallySelected->hide();
        ui->widgetTokenControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelTokenControlAutomaticallySelected->show();
        ui->widgetTokenControl->hide();
        ui->labelTokenControlInsuffFunds->hide();
    }
}

/** TOKENS START */
void TokensDialog::tokenControlUpdateSendCoinsDialog()
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            removeEntry(entry);
        }
    }

    addEntry();

}

void TokensDialog::processNewTransaction()
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->refreshTokenList();
        }
    }
    // refreshing an entry resets its choice: choose again for the page
    refreshTokens();
}

void TokensDialog::focusToken(const QModelIndex &idx)
{
    clear();

    QString name = idx.data(TokenTableModel::TokenNameRole).toString();
    // the name of a locked balance as the table model shows it
    if (name.endsWith(" (LOCKED)"))
        name.chop(9);
    selectToken(name);

    SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(0)->widget());
    if(entry)
        entry->setFocus();
}

void TokensDialog::focusTokenListBox()
{
    SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(0)->widget());
    if (entry)
        entry->setFocus();
}

void TokensDialog::handleFirstSelection()
{
    SendTokensEntry *entry = qobject_cast<SendTokensEntry*>(ui->entries->itemAt(0)->widget());
    if (entry) {
        entry->refreshTokenList();
    }
    refreshTokens();
}
/** TOKENS END */
