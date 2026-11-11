// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendcoinsdialog.h"
#include "ui_sendcoinsdialog.h"

#include "addresstablemodel.h"
#include "aokchainunits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "sendcoinsentry.h"
#include "walletmodel.h"
#include "guiconstants.h"

#include "base58.h"
#include "chainparams.h"
#include "wallet/coincontrol.h"
#include "validation.h" // mempool and minRelayTxFee
#include "ui_interface.h"
#include "txmempool.h"
#include "policy/fees.h"
#include "wallet/fees.h"

#include <QButtonGroup>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>

SendCoinsDialog::SendCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendCoinsDialog),
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
    ui->clearButton->setIcon(QIcon());
    ui->sendButton->setIcon(QIcon());

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(coinControlChangeEdited(const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

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

    // Setup the coin control visuals and labels
    setupCoinControl(platformStyle);
    setupScrollView(platformStyle);
    setupFeeControl(platformStyle);
}

static QLabel *summaryCaption(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "secondary");
    return label;
}

static QLabel *summaryValue(QWidget *parent)
{
    QLabel *label = new QLabel(parent);
    label->setFont(GUIUtil::monoFont(14, QFont::DemiBold));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

/**
 * Arrange the widgets of the form in two columns: a card with the recipients,
 * the fee and the coin control on the left and a card with the summary and
 * the send button on the right.
 */
void SendCoinsDialog::buildLayout()
{
    // available balance for the page header
    headerBox = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(headerBox);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    headerLayout->addWidget(summaryCaption(tr("Available"), headerBox));
    availableLabel = new QLabel(headerBox);
    availableLabel->setFont(GUIUtil::monoFont(13, QFont::DemiBold));
    headerLayout->addWidget(availableLabel);
    ui->label->hide();
    ui->labelBalance->hide();

    // the layouts of the form go; the widgets stay
    delete layout();
    QHBoxLayout *columns = new QHBoxLayout(this);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(20);

    // left: recipients, fee, coin control
    QFrame *formCard = new QFrame(this);
    formCard->setProperty("role", "card");
    QVBoxLayout *form = new QVBoxLayout(formCard);
    form->setContentsMargins(26, 24, 20, 20);
    form->setSpacing(12);
    form->addWidget(ui->scrollArea, 1);

    QLabel *feeLabel = new QLabel(tr("Fee"), formCard);
    feeLabel->setProperty("role", "fieldLabel");
    form->addWidget(feeLabel);
    QHBoxLayout *feeRow = new QHBoxLayout();
    feeRow->setSpacing(8);
    feeRow->setContentsMargins(0, 0, 6, 0);
    QButtonGroup *feeGroup = new QButtonGroup(this);
    feeTargets[0] = 24;
    feeTargets[1] = 6;
    const QString feeTexts[3] = {
        tr("Slow") + "\n" + tr("about %1 blocks").arg(feeTargets[0]),
        tr("Normal") + "\n" + tr("about %1 blocks").arg(feeTargets[1]),
        tr("Custom") + "\n" + "AOK/kB",
    };
    for (int i = 0; i < 3; ++i) {
        feeButtons[i] = new QPushButton(feeTexts[i], formCard);
        feeButtons[i]->setProperty("look", "fee");
        feeButtons[i]->setCheckable(true);
        feeButtons[i]->setFixedHeight(56);
        feeButtons[i]->setCursor(Qt::PointingHandCursor);
        feeGroup->addButton(feeButtons[i], i);
        feeRow->addWidget(feeButtons[i], 1);
    }
    connect(feeGroup, SIGNAL(buttonClicked(int)), this, SLOT(feeButtonClicked(int)));
    form->addLayout(feeRow);
    QHBoxLayout *feeInfoRow = new QHBoxLayout();
    feeInfoRow->setContentsMargins(0, 0, 6, 0);
    feeInfo = new QLabel(formCard);
    feeInfo->setProperty("role", "caption");
    feeDetailsButton = new QPushButton(tr("Fee settings"), formCard);
    feeDetailsButton->setProperty("look", "link");
    feeDetailsButton->setCursor(Qt::PointingHandCursor);
    connect(feeDetailsButton, SIGNAL(clicked()), this, SLOT(toggleFeeDetails()));
    feeInfoRow->addWidget(feeInfo, 1);
    feeInfoRow->addWidget(feeDetailsButton);
    form->addLayout(feeInfoRow);
    form->addWidget(ui->frameFee);
    form->addWidget(ui->frameCoinControl);

    ui->addButton->setProperty("look", "dashed");
    ui->addButton->setText(tr("Add recipient"));
    ui->addButton->setFixedHeight(44);
    ui->addButton->setCursor(Qt::PointingHandCursor);
    form->addWidget(ui->addButton, 0, Qt::AlignLeft);
    columns->addWidget(formCard, 3);

    // right: summary
    QFrame *summaryCard = new QFrame(this);
    summaryCard->setProperty("role", "card");
    QVBoxLayout *summary = new QVBoxLayout(summaryCard);
    summary->setContentsMargins(26, 24, 26, 24);
    summary->setSpacing(14);
    QLabel *summaryTitle = new QLabel(tr("Summary"), summaryCard);
    summaryTitle->setProperty("role", "title");
    summaryTitle->setFont(GUIUtil::appFont(16, QFont::Bold));
    summary->addWidget(summaryTitle);
    summary->addSpacing(2);

    auto addRow = [&](const QString &caption, QLabel *&value) {
        QHBoxLayout *row = new QHBoxLayout();
        row->setSpacing(12);
        row->addWidget(summaryCaption(caption, summaryCard));
        value = summaryValue(summaryCard);
        row->addWidget(value, 1);
        summary->addLayout(row);
    };
    addRow(tr("Recipient"), summaryRecipient);
    addRow(tr("Amount to send"), summaryAmount);
    addRow(tr("Fee (estimated)"), summaryFee);
    summaryFee->setToolTip(tr("Estimate for a transaction with one input. The fee that is really paid is shown for confirmation before the transaction is sent."));
    QFrame *separator = new QFrame(summaryCard);
    separator->setProperty("role", "hline");
    separator->setFixedHeight(1);
    summary->addWidget(separator);
    addRow(tr("Total"), summaryTotal);
    summaryTotal->setTextFormat(Qt::RichText);
    summaryTotal->setFont(GUIUtil::monoFont(20, QFont::Bold));
    addRow(tr("Balance after sending"), summaryAfter);
    summaryAfter->setFont(GUIUtil::monoFont(13));
    summary->addStretch(1);

    QLabel *warning = new QLabel(tr("A transaction that has been sent cannot be cancelled. Check the address once more."), summaryCard);
    warning->setProperty("role", "warning");
    warning->setWordWrap(true);
    summary->addWidget(warning);
    ui->sendButton->setProperty("look", "send");
    ui->sendButton->setFixedHeight(52);
    ui->sendButton->setCursor(Qt::PointingHandCursor);
    summary->addWidget(ui->sendButton);
    ui->clearButton->setProperty("look", "ghost");
    ui->clearButton->setText(tr("Clear all"));
    ui->clearButton->setFixedHeight(44);
    ui->clearButton->setCursor(Qt::PointingHandCursor);
    summary->addWidget(ui->clearButton);
    columns->addWidget(summaryCard, 2);
}

void SendCoinsDialog::feeButtonClicked(int id)
{
    if (id == 2) {
        ui->radioCustomFee->setChecked(true);
        // the custom rate is entered in the fee settings
        minimizeFeeSection(false);
    } else {
        ui->radioSmartFee->setChecked(true);
        ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(feeTargets[id]));
    }
    updateFeeSectionControls();
    updateSmartFeeLabel();
    coinControlUpdateLabels();
}

void SendCoinsDialog::toggleFeeDetails()
{
    minimizeFeeSection(!fFeeMinimized);
}

/** Show which of the three fee choices the fee settings amount to. */
void SendCoinsDialog::updateFeeButtons()
{
    int checked = -1;
    if (ui->radioCustomFee->isChecked())
        checked = 2;
    else if (getConfTargetForIndex(ui->confTargetSelector->currentIndex()) == feeTargets[1])
        checked = 1;
    else if (getConfTargetForIndex(ui->confTargetSelector->currentIndex()) == feeTargets[0])
        checked = 0;
    QButtonGroup *group = feeButtons[0]->group();
    group->setExclusive(false);
    for (int i = 0; i < 3; ++i)
        feeButtons[i]->setChecked(i == checked);
    group->setExclusive(true);

    if (!model || !model->getOptionsModel())
        return;
    if (ui->radioCustomFee->isChecked()) {
        feeInfo->setText(ui->checkBoxMinimumFee->isChecked() ? ui->checkBoxMinimumFee->text()
            : AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    } else {
        QString text = ui->labelSmartFee->text();
        if (!ui->labelFeeEstimation->text().isEmpty())
            text += QString::fromUtf8(" · ") + ui->labelFeeEstimation->text();
        feeInfo->setText(text);
    }
}

/** Use everything that can be spent for this recipient; the fee comes out of the amount. */
void SendCoinsDialog::useAvailableBalance(SendCoinsEntry *entry)
{
    if (!model || !model->getOptionsModel())
        return;
    // Calculate available amount to send.
    CAmount amount = model->getOptionsModel()->getCoinControlFeatures() && CoinControlDialog::coinControl->HasSelected()
        ? model->getBalance(CoinControlDialog::coinControl) : model->getBalance();
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *e = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if (e && !e->isHidden() && e != entry)
            amount -= e->amount();
    }
    if (amount > 0) {
        entry->checkSubtractFeeFromAmount();
        entry->setAmount(amount);
    } else {
        entry->setAmount(0);
    }
}

static QString plainAmount(int unit, const CAmount &amount)
{
    return GUIUtil::withThousands(AokChainUnits::format(unit, amount, false, AokChainUnits::separatorNever));
}

void SendCoinsDialog::updateSummary()
{
    if (!model || !model->getOptionsModel())
        return;
    const int unit = model->getOptionsModel()->getDisplayUnit();
    const QString unitName = AokChainUnits::name(unit);
    const QString dash = QString::fromUtf8("–");

    int count = 0;
    CAmount total = 0;
    bool subtractFee = false;
    QString address, label;
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if (!entry || entry->isHidden())
            continue;
        ++count;
        total += entry->amount();
        subtractFee = subtractFee || entry->subtractsFee();
        address = entry->addressText();
        label = entry->labelText();
    }

    // recipient: the label, or the address in its short form with the full one in a tool tip
    if (count > 1) {
        summaryRecipient->setFont(GUIUtil::appFont(14, QFont::DemiBold));
        summaryRecipient->setText(tr("%n recipient(s)", "", count));
        summaryRecipient->setToolTip(QString());
    } else if (address.isEmpty()) {
        summaryRecipient->setText(dash);
        summaryRecipient->setToolTip(QString());
    } else {
        const bool valid = model->validateAddress(address);
        summaryRecipient->setFont(valid ? GUIUtil::monoFont(14, QFont::DemiBold) : GUIUtil::appFont(14, QFont::DemiBold));
        const QString shown = valid ? GUIUtil::shortAddress(address) : address;
        summaryRecipient->setText(QFontMetrics(summaryRecipient->font()).elidedText(shown, Qt::ElideMiddle, 220));
        summaryRecipient->setToolTip(GUIUtil::monoToolTip(address, label));
    }

    // fee for a typical transaction of this many outputs at the chosen rate
    CCoinControl coin_control;
    updateCoinControlState(coin_control);
    const unsigned int bytes = 10 + 4 + 148 + 34 * (qMax(1, count) + 1);
    const CAmount fee = GetMinimumFee(bytes, coin_control, ::mempool, ::feeEstimator, nullptr);

    const CAmount balance = model->getBalance();
    if (total > 0) {
        summaryAmount->setText(plainAmount(unit, total) + " " + unitName);
        summaryFee->setText(QString::fromUtf8("≈ ") + plainAmount(unit, fee) + " " + unitName);
        const CAmount spent = subtractFee ? total : total + fee;
        summaryTotal->setText(QString("%1 <span style=\"font-family:'Noto Sans'; font-size:14px; color:%2;\">%3</span>")
            .arg(plainAmount(unit, spent), GUIUtil::theme().accent.name(), unitName));
        summaryAfter->setText(plainAmount(unit, balance - spent) + " " + unitName);
        summaryAfter->setStyleSheet(balance - spent < 0 ? QString("QLabel { color: %1; }").arg(GUIUtil::theme().error.name()) : QString());

        // "Send 25 AOK"
        QString shortAmount = AokChainUnits::format(unit, total, false, AokChainUnits::separatorNever);
        if (shortAmount.contains('.')) {
            while (shortAmount.endsWith('0'))
                shortAmount.chop(1);
            if (shortAmount.endsWith('.'))
                shortAmount.chop(1);
        }
        ui->sendButton->setText(tr("Send %1").arg(GUIUtil::withThousands(shortAmount) + " " + unitName));
    } else {
        summaryAmount->setText(dash);
        summaryFee->setText(dash);
        summaryTotal->setText(dash);
        summaryAfter->setText(plainAmount(unit, balance) + " " + unitName);
        summaryAfter->setStyleSheet(QString());
        ui->sendButton->setText(tr("Send"));
    }
}

void SendCoinsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void SendCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(_model);
            }
        }

        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(), _model->getStake(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance(), _model->getWatchStake());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        ui->frameCoinControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        coinControlUpdateLabels();

        // "normal" is the default confirmation target of the wallet, "slow" two steps later
        const int normalIndex = getIndexForConfTarget(model->getDefaultConfirmTarget());
        feeTargets[1] = getConfTargetForIndex(normalIndex);
        feeTargets[0] = getConfTargetForIndex(std::min(normalIndex + 2, (int)confTargets.size() - 1));
        feeButtons[0]->setText(tr("Slow") + "\n" + tr("about %1 blocks").arg(feeTargets[0]));
        feeButtons[1]->setText(tr("Normal") + "\n" + tr("about %1 blocks").arg(feeTargets[1]));

        // fee section
        for (const int &n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
//        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(updateSmartFeeLabel()));
//        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
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

SendCoinsDialog::~SendCoinsDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void SendCoinsDialog::setupCoinControl(const PlatformStyle *platformStyle)
{
    /** Update the coincontrol frame */
    ui->frameCoinControl->setStyleSheet(QString(".QFrame { background-color: %1; border: none; border-radius: 12px; padding: 10px 8px; }").arg(GUIUtil::theme().surfaceAlt.name()));
    ui->widgetCoinControl->setStyleSheet(".QWidget {background-color: transparent;}");
    /** Create the shadow effects on the frames */

    ui->labelCoinControlFeatures->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlFeatures->setFont(GUIUtil::getTopLabelFont());

    ui->labelCoinControlQuantityText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlQuantityText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlAmountText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlAmountText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlFeeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlFeeText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlAfterFeeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlAfterFeeText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlBytesText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlBytesText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlLowOutputText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlLowOutputText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlChangeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlChangeText->setFont(GUIUtil::getSubLabelFont());

    // Align the other labels next to the input buttons to the text in the same height
    ui->labelCoinControlAutomaticallySelected->setStyleSheet(STRING_LABEL_COLOR);

    // Align the Custom change address checkbox
    ui->checkBoxCoinControlChange->setStyleSheet(STRING_LABEL_COLOR);

    ui->labelCoinControlQuantity->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlAmount->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlAfterFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlBytes->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlLowOutput->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->checkBoxCoinControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->lineEditCoinControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlInsuffFunds->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlAutomaticallySelected->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlChangeLabel->setFont(GUIUtil::getSubLabelFontBolded());
}

void SendCoinsDialog::setupScrollView(const PlatformStyle *platformStyle)
{
    /** Update the scrollview*/
    ui->scrollArea->setStyleSheet(".QScrollArea { background: transparent; border: none; }");

    ui->entries->setContentsMargins(0,0,6,0);
    ui->entries->setSpacing(18);
    ui->scrollAreaWidgetContents->setStyleSheet(".QWidget { background: transparent; }");
}

void SendCoinsDialog::setupFeeControl(const PlatformStyle *platformStyle)
{
    /** Update the fee frame */
    ui->frameFee->setStyleSheet(QString(".QFrame#frameFee { background-color: %1; border: none; border-radius: 12px; padding: 6px 8px; }").arg(GUIUtil::theme().surfaceAlt.name()));
    /** Create the shadow effects on the frames */

    ui->labelFeeHeadline->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelFeeHeadline->setFont(GUIUtil::getSubLabelFont());

    ui->labelSmartFee3->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCustomPerKilobyte->setStyleSheet(STRING_LABEL_COLOR);
    ui->radioSmartFee->setStyleSheet(STRING_LABEL_COLOR);
    ui->radioCustomFee->setStyleSheet(STRING_LABEL_COLOR);
    ui->checkBoxMinimumFee->setStyleSheet(STRING_LABEL_COLOR);


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

void SendCoinsDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
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

    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;

    // Always use a CCoinControl instance, use the CoinControlDialog instance if CoinControl has been enabled
    CCoinControl ctrl;
    if (model->getOptionsModel()->getCoinControlFeatures())
        ctrl = *CoinControlDialog::coinControl;

    updateCoinControlState(ctrl);

    prepareStatus = model->prepareTransaction(currentTransaction, ctrl);

    // process prepareStatus and on error generate message shown to user
    processSendCoinsReturn(prepareStatus,
        AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

    if(prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();

    // Format confirmation message
    QStringList formatted;
    for (const SendCoinsRecipient &rcp : currentTransaction.getRecipients())
    {
        // generate bold amount string
        QString amount = "<b>" + AokChainUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
        amount.append("</b>");
        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        if (!rcp.paymentRequest.IsInitialized()) // normal payment
        {
            if(rcp.label.length() > 0) // label with address
            {
                recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
                recipientElement.append(QString(" (%1)").arg(address));
            }
            else // just address
            {
                recipientElement = tr("%1 to %2").arg(amount, address);
            }
        }
        else if(!rcp.authenticatedMerchant.isEmpty()) // authenticated payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.authenticatedMerchant));
        }
        else // unauthenticated payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }

        if (rcp.username.length() > 0) {
            recipientElement.append(QString(" (%1)").arg(rcp.username));
        }

        if (rcp.coinLockTime > 0) {
            recipientElement.append(tr(" locked till %1").arg(QDateTime::fromTime_t(rcp.coinLockTime).toString("yyyy.MM.dd HH:mm")));
        }

        formatted.append(recipientElement);
    }

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if(txFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(AokChainUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));

        // append transaction size
        questionString.append(" (" + QString::number((double)currentTransaction.getTransactionSize() / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    questionString.append(tr("Total Amount %1")
        .arg(AokChainUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));

    SendConfirmationDialog confirmationDialog(tr("Confirm send coins"),
        questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction);
    // process sendStatus and on error generate message shown to user
    processSendCoinsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        accept();
        CoinControlDialog::coinControl->UnSelectAll();
        coinControlUpdateLabels();
    }
    fNewRecipientAllowed = true;
}

void SendCoinsDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void SendCoinsDialog::reject()
{
    clear();
}

void SendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry *SendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(recipientChanged()), this, SLOT(updateSummary()));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

void SendCoinsDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

void SendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendCoinsDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
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

void SendCoinsDialog::setAddress(const QString &address)
{
    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
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

void SendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
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

bool SendCoinsDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void SendCoinsDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& stake,
                                 const CAmount& watchBalance, const CAmount& watchUnconfirmedBalance, const CAmount& watchImmatureBalance, const CAmount& watchStake)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchBalance);
    Q_UNUSED(stake);
    Q_UNUSED(watchUnconfirmedBalance);
    Q_UNUSED(watchImmatureBalance);
    Q_UNUSED(watchStake);

    ui->labelBalance->setFont(GUIUtil::getSubLabelFont());
    ui->label->setFont(GUIUtil::getSubLabelFont());

    if(model && model->getOptionsModel())
    {
        const int unit = model->getOptionsModel()->getDisplayUnit();
        availableLabel->setText(GUIUtil::withThousands(AokChainUnits::format(unit, balance, false, AokChainUnits::separatorNever)) + " " + AokChainUnits::name(unit));
        updateSummary();
        ui->labelBalance->setText(AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
    }
}

void SendCoinsDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0, 0, 0);
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void SendCoinsDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg)
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

void SendCoinsDialog::minimizeFeeSection(bool fMinimize)
{
    // The three fee buttons take the place of the minimized fee section; the
    // complete controls are shown or hidden as a whole
    ui->labelFeeHeadline ->setVisible(false);
    ui->labelFeeMinimized->setVisible(false);
    ui->buttonChooseFee  ->setVisible(false);
    ui->buttonMinimizeFee->setVisible(false);
    ui->frameFeeSelection->setVisible(true);
    ui->frameFee->setVisible(!fMinimize);
    feeDetailsButton->setText(fMinimize ? tr("Fee settings") : tr("Hide fee settings"));
    fFeeMinimized = fMinimize;
}

void SendCoinsDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void SendCoinsDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void SendCoinsDialog::setMinimumFee()
{
    ui->customFee->setValue(GetRequiredFee(1000));
}

void SendCoinsDialog::updateFeeSectionControls()
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
    updateFeeButtons();
}

void SendCoinsDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void SendCoinsDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
            AokChainUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), GetRequiredFee(1000)) + "/kB")
        );
}

void SendCoinsDialog::updateCoinControlState(CCoinControl& ctrl)
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

void SendCoinsDialog::updateSmartFeeLabel()
{
    if(!model || !model->getOptionsModel())
        return;
    CCoinControl coin_control;
    updateCoinControlState(coin_control);
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
    updateFeeButtons();
}

// Coin Control: copy label "Quantity" to clipboard
void SendCoinsDialog::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendCoinsDialog::coinControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void SendCoinsDialog::coinControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void SendCoinsDialog::coinControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void SendCoinsDialog::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void SendCoinsDialog::coinControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void SendCoinsDialog::coinControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void SendCoinsDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) // coin control features disabled
        CoinControlDialog::coinControl->SetNull();

    coinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked()
{
    CoinControlDialog dlg(platformStyle);
    dlg.setModel(model);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void SendCoinsDialog::coinControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        CoinControlDialog::coinControl->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void SendCoinsDialog::coinControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        CoinControlDialog::coinControl->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid AokChain address"));
        }
        else // Valid address
        {
            if (!model->IsSpendable(dest)) {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    CoinControlDialog::coinControl->destChange = dest;
                else
                {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                    ui->labelCoinControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelCoinControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));

                CoinControlDialog::coinControl->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void SendCoinsDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*CoinControlDialog::coinControl);
    updateFeeButtons();
    updateSummary();

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendCoinsRecipient rcp = entry->getValue();
            CoinControlDialog::payAmounts.append(rcp.amount);
            if (rcp.fSubtractFeeFromAmount)
                CoinControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (CoinControlDialog::coinControl->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(model, this);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

SendConfirmationDialog::SendConfirmationDialog(const QString &title, const QString &text, int _secDelay,
    QWidget *parent) :
    QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent), secDelay(_secDelay)
{
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int SendConfirmationDialog::exec()
{
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void SendConfirmationDialog::countDown()
{
    secDelay--;
    updateYesButton();

    if(secDelay <= 0)
    {
        countDownTimer.stop();
    }
}

void SendConfirmationDialog::updateYesButton()
{
    if(secDelay > 0)
    {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    }
    else
    {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}

QWidget *SendCoinsDialog::headerWidget() const
{
    return headerBox;
}
