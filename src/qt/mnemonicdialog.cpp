// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "mnemonicdialog.h"

#include "aokchainunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "walletmodel.h"

#include "base58.h"
#include "bip39.h"
#include "support/allocators/secure.h"
#include "support/cleanse.h"
#include "validation.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

/** An amount with its unit, the thousands with commas as on the pages */
static QString amountWithUnit(int unit, const CAmount &amount)
{
    return GUIUtil::withThousands(AokChainUnits::format(unit, amount, false, AokChainUnits::separatorNever)) + " " + AokChainUnits::name(unit).trimmed();
}

/** Overwrite a string that held a secret before letting go of it. */
static void wipe(QString &text)
{
    text.fill(QChar(' '));
    text.clear();
}

static void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty())
        memory_cleanse(bytes.data(), bytes.size());
    bytes.clear();
}

void MnemonicWorker::search()
{
    std::string error;
    const bool ok = mnemonicrestore::BuildPlan(master, haveKnown ? &known : nullptr, cancel, plan, error);
    Q_EMIT searchDone(ok, QString::fromStdString(error));
}

void MnemonicWorker::rescan()
{
    std::string error;
    const bool ok = mnemonicrestore::Rescan(wallet, cancel, [this](int percent) { Q_EMIT rescanProgress(percent); }, error);
    Q_EMIT rescanDone(ok, QString::fromStdString(error));
}

static QLabel *wrapped(const QString &text, const char *role, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setWordWrap(true);
    if (role)
        label->setProperty("role", role);
    return label;
}

static QPushButton *dialogButton(const QString &text, bool primary, QWidget *parent)
{
    QPushButton *button = new QPushButton(text, parent);
    button->setFixedHeight(40);
    button->setMinimumWidth(110);
    button->setAutoDefault(false);
    if (primary)
        button->setProperty("primary", true);
    return button;
}

MnemonicDialog::MnemonicDialog(WalletModel *_model, QWidget *parent) :
    QDialog(parent),
    model(_model),
    worker(new MnemonicWorker()),
    busy(false),
    closeWhenIdle(false),
    imported(0)
{
    setWindowTitle(tr("Restore from recovery words"));
    setMinimumSize(640, 560);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 20);
    layout->setSpacing(14);
    QLabel *title = new QLabel(windowTitle(), this);
    title->setProperty("role", "title");
    layout->addWidget(title);
    stack = new QStackedWidget(this);
    layout->addWidget(stack, 1);
    stack->addWidget(enterPage());
    stack->addWidget(workingPage());
    stack->addWidget(resultPage());
    stack->addWidget(rescanPage());
    stack->addWidget(donePage());

    // a node without the blocks of the past cannot find the earlier transactions of the keys
    {
        QWidget *page = new QWidget(this);
        QVBoxLayout *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        QString reason;
        const bool available = model->ensurePastBlocksAvailable(&reason);
        pageLayout->addWidget(wrapped(reason, "warning", page));
        pageLayout->addStretch();
        QHBoxLayout *buttons = new QHBoxLayout();
        buttons->addStretch();
        QPushButton *close = dialogButton(tr("Close"), false, page);
        connect(close, SIGNAL(clicked()), this, SLOT(reject()));
        buttons->addWidget(close);
        pageLayout->addLayout(buttons);
        stack->addWidget(page);
        stack->setCurrentIndex(available ? Enter : Unavailable);
    }

    worker->wallet = model->getWallet();
    worker->moveToThread(&thread);
    connect(&thread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(this, SIGNAL(startSearch()), worker, SLOT(search()));
    connect(this, SIGNAL(startRescan()), worker, SLOT(rescan()));
    connect(worker, SIGNAL(searchDone(bool,QString)), this, SLOT(searchDone(bool,QString)));
    connect(worker, SIGNAL(rescanProgress(int)), this, SLOT(rescanProgress(int)));
    connect(worker, SIGNAL(rescanDone(bool,QString)), this, SLOT(rescanDone(bool,QString)));
    thread.start();
}

MnemonicDialog::~MnemonicDialog()
{
    worker->cancel = true;
    thread.quit();
    thread.wait();
    wipeInputs();
    model->setProgressQuiet(false);
    // (the worker, with the master key and the keys of the plan, is deleted with the thread;
    // CKey keeps its secret in locked memory and wipes it)
}

QWidget *MnemonicDialog::enterPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(wrapped(tr("Enter the recovery words of the other wallet (for example a mobile wallet). Its keys are added to this wallet, so the coins can be used here."), "secondary", page));
    layout->addWidget(wrapped(tr("Anyone who sees these words can take the coins. Type them only on a computer you trust. The words are not saved; only the keys made from them go into this wallet."), "warning", page));
    layout->addSpacing(4);

    QLabel *wordsLabel = new QLabel(tr("Recovery words (12 to 24, separated by spaces; English or Korean)"), page);
    wordsLabel->setProperty("role", "fieldLabel");
    layout->addWidget(wordsLabel);
    words = new QPlainTextEdit(page);
    words->setFont(GUIUtil::monoFont(14));
    words->setFixedHeight(84);
    words->setUndoRedoEnabled(false);
    words->setTabChangesFocus(true);
    words->setPlaceholderText(tr("word1 word2 word3 ..."));
    layout->addWidget(words);
    wordsStatus = new QLabel(page);
    wordsStatus->setProperty("role", "caption");
    layout->addWidget(wordsStatus);
    layout->addSpacing(4);

    QLabel *passLabel = new QLabel(tr("Passphrase of the recovery words (optional)"), page);
    passLabel->setProperty("role", "fieldLabel");
    layout->addWidget(passLabel);
    passphrase = new QLineEdit(page);
    passphrase->setEchoMode(QLineEdit::Password);
    passphrase->setMinimumHeight(40);
    passphrase->setPlaceholderText(tr("Leave empty unless that wallet was set up with one"));
    layout->addWidget(passphrase);
    layout->addSpacing(4);

    QLabel *addressLabel = new QLabel(tr("An address of that wallet (optional)"), page);
    addressLabel->setProperty("role", "fieldLabel");
    layout->addWidget(addressLabel);
    address = new QLineEdit(page);
    address->setFont(GUIUtil::monoFont(13));
    address->setMinimumHeight(40);
    address->setPlaceholderText(tr("Only if no coins are found without it"));
    address->setToolTip(tr("With an address the wallet can find an unusual key path. The search then takes up to a minute."));
    layout->addWidget(address);

    enterError = wrapped(QString(), "resultError", page);
    enterError->hide();
    layout->addWidget(enterError);
    layout->addStretch();

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = dialogButton(tr("Cancel"), false, page);
    findButton = dialogButton(tr("Find addresses"), true, page);
    findButton->setEnabled(false);
    buttons->addWidget(cancel);
    buttons->addWidget(findButton);
    layout->addLayout(buttons);

    validateTimer = new QTimer(this);
    validateTimer->setSingleShot(true);
    validateTimer->setInterval(150);
    connect(words, SIGNAL(textChanged()), validateTimer, SLOT(start()));
    connect(validateTimer, SIGNAL(timeout()), this, SLOT(validateWords()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(findButton, SIGNAL(clicked()), this, SLOT(findAddresses()));
    validateWords();
    return page;
}

QWidget *MnemonicDialog::workingPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addStretch();
    workingText = wrapped(QString(), nullptr, page);
    workingText->setAlignment(Qt::AlignCenter);
    layout->addWidget(workingText);
    QProgressBar *bar = new QProgressBar(page);
    bar->setRange(0, 0);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    layout->addWidget(bar);
    layout->addStretch();
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = dialogButton(tr("Cancel"), false, page);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    return page;
}

QWidget *MnemonicDialog::resultPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    resultSummary = wrapped(QString(), nullptr, page);
    layout->addWidget(resultSummary);
    resultTable = new QTableWidget(0, 3, page);
    resultTable->setHorizontalHeaderLabels({tr("Address"), tr("Key path"), tr("Amount")});
    resultTable->verticalHeader()->hide();
    resultTable->setShowGrid(false);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable->setSelectionMode(QAbstractItemView::NoSelection);
    resultTable->setFocusPolicy(Qt::NoFocus);
    resultTable->horizontalHeader()->setHighlightSections(false);
    resultTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    resultTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    resultTable->verticalHeader()->setDefaultSectionSize(36);
    layout->addWidget(resultTable, 1);
    resultNote = wrapped(QString(), "warning", page);
    layout->addWidget(resultNote);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = dialogButton(tr("Cancel"), false, page);
    QPushButton *import = dialogButton(tr("Add the keys and scan"), true, page);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(import, SIGNAL(clicked()), this, SLOT(importKeys()));
    buttons->addWidget(cancel);
    buttons->addWidget(import);
    layout->addLayout(buttons);
    return page;
}

QWidget *MnemonicDialog::rescanPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addStretch();
    rescanText = wrapped(QString(), nullptr, page);
    rescanText->setAlignment(Qt::AlignCenter);
    layout->addWidget(rescanText);
    rescanBar = new QProgressBar(page);
    rescanBar->setRange(0, 100);
    rescanBar->setFixedHeight(22);
    layout->addWidget(rescanBar);
    layout->addWidget(wrapped(tr("The wallet reads the blocks of the past to find the transactions of the new keys. The rest of the program can be used meanwhile."), "caption", page));
    layout->addStretch();
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    rescanStop = dialogButton(tr("Stop"), false, page);
    rescanStop->setToolTip(tr("The keys stay in the wallet. The scan can be done again later by starting the program with -rescan."));
    connect(rescanStop, SIGNAL(clicked()), this, SLOT(reject()));
    buttons->addWidget(rescanStop);
    layout->addLayout(buttons);
    return page;
}

QWidget *MnemonicDialog::donePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    doneText = wrapped(QString(), "resultOk", page);
    layout->addWidget(doneText);
    doneNote = wrapped(tr("The new keys are in this wallet only. They are not covered by earlier backups: make a new backup of the wallet file now."), "warning", page);
    layout->addWidget(doneNote);
    layout->addStretch();
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *close = dialogButton(tr("Close"), false, page);
    backupButton = dialogButton(tr("Back up now"), true, page);
    connect(close, SIGNAL(clicked()), this, SLOT(accept()));
    connect(backupButton, &QPushButton::clicked, this, [this] {
        accept();
        Q_EMIT backupRequested();
    });
    buttons->addWidget(close);
    buttons->addWidget(backupButton);
    layout->addLayout(buttons);
    return page;
}

void MnemonicDialog::wipeInputs()
{
    for (QLineEdit *edit : {passphrase}) {
        edit->setText(QString(" ").repeated(edit->text().size()));
        edit->clear();
    }
    const bool blocked = words->blockSignals(true);
    words->setPlainText(QString(" ").repeated(words->toPlainText().size()));
    words->clear();
    words->blockSignals(blocked);
}

void MnemonicDialog::validateWords()
{
    QString text = words->toPlainText();
    QByteArray utf8 = text.toUtf8();
    wipe(text);
    std::string raw(utf8.constData(), utf8.size());
    wipe(utf8);
    std::string normalized, language, error;
    bool ok = false;
    QString status;
    QString role = "caption";
    if (!bip39::Normalize(raw, true, normalized)) {
        status = tr("Only English and Korean words can be used here.");
        role = "error";
    } else {
        int count = 0;
        if (!normalized.empty())
            count = 1 + std::count(normalized.begin(), normalized.end(), ' ');
        if (count == 0) {
            status = tr("12, 15, 18, 21 or 24 words.");
        } else if (bip39::CheckMnemonic(normalized, language, error)) {
            ok = true;
            status = tr("%n words, correct.", "", count) + " " + (language == "korean" ? tr("Korean word list.") : tr("English word list."));
            role = "ok";
        } else if (count != 12 && count != 15 && count != 18 && count != 21 && count != 24) {
            status = tr("%n words so far. 12, 15, 18, 21 or 24 are needed.", "", count);
        } else {
            QRegExp unknown("word (\\d+) ");
            if (unknown.indexIn(QString::fromStdString(error)) >= 0)
                status = tr("Word %1 is not in the word list. Check its spelling.").arg(unknown.cap(1));
            else
                status = tr("The words do not fit together (checksum). A word is mistyped, missing or out of order.");
            role = "error";
        }
    }
    if (!raw.empty())
        memory_cleanse(&raw[0], raw.size());
    if (!normalized.empty())
        memory_cleanse(&normalized[0], normalized.size());
    wordsStatus->setText(status);
    wordsStatus->setProperty("role", role);
    wordsStatus->style()->unpolish(wordsStatus);
    wordsStatus->style()->polish(wordsStatus);
    const bool synced = !IsInitialBlockDownload();
    if (!synced) {
        enterError->setText(tr("The wallet is still synchronizing. Coins in blocks that are not here yet would not be found: restore when the synchronization has finished."));
        enterError->show();
    }
    findButton->setEnabled(ok && synced);
}

QString MnemonicDialog::errorText(const QString &error) const
{
    if (error == "Cancelled")
        return tr("It was stopped.");
    if (error.startsWith("No coins were found"))
        return tr("No coins were found on the usual key paths of these words. Check the passphrase, or enter an address of that wallet so that its key path can be found.");
    if (error.startsWith("The unspent outputs"))
        return tr("The coins of the chain could not be read.");
    if (error.startsWith("The wallet is locked") || error.startsWith("The wallet is unlocked for staking"))
        return tr("The wallet is locked.");
    if (error.startsWith("Some blocks could not be read"))
        return tr("Some blocks could not be read. Transactions in those blocks are missing from the wallet.");
    if (error.startsWith("The recovery words contain") || error.startsWith("The passphrase contains"))
        return tr("Only English and Korean words, and a passphrase of ASCII or Korean characters, can be used here.");
    return error;
}

void MnemonicDialog::findAddresses()
{
    if (busy)
        return;
    enterError->hide();
    // the optional address
    worker->haveKnown = false;
    const QString known = address->text().trimmed();
    if (!known.isEmpty()) {
        const CTxDestination dest = DecodeDestination(known.toStdString());
        const CKeyID *keyid = boost::get<CKeyID>(&dest);
        if (!keyid) {
            enterError->setText(tr("The address is not a valid AokChain address."));
            enterError->show();
            return;
        }
        worker->known = *keyid;
        worker->haveKnown = true;
    }

    // the words and the passphrase become the master key; the fields are wiped at once
    std::string language, error;
    bool ok;
    {
        QString wordsText = words->toPlainText();
        QString passText = passphrase->text();
        QByteArray wordsUtf8 = wordsText.toUtf8();
        QByteArray passUtf8 = passText.toUtf8();
        wipe(wordsText);
        wipe(passText);
        SecureString secureWords(wordsUtf8.constData(), wordsUtf8.size());
        SecureString securePass(passUtf8.constData(), passUtf8.size());
        wipe(wordsUtf8);
        wipe(passUtf8);
        ok = mnemonicrestore::MasterFromMnemonic(secureWords, securePass, worker->master, language, error);
    }
    if (!ok) {
        enterError->setText(errorText(QString::fromStdString(error)));
        enterError->show();
        return;
    }
    wipeInputs();
    validateWords();

    workingText->setText(worker->haveKnown
        ? tr("Looking for the key path of the address and for addresses that hold coins. This can take a minute.")
        : tr("Looking for addresses that hold coins..."));
    stack->setCurrentIndex(Working);
    busy = true;
    worker->cancel = false;
    Q_EMIT startSearch();
}

void MnemonicDialog::searchDone(bool ok, const QString &error)
{
    busy = false;
    // the master key has done its work
    worker->master = CExtKey();
    if (closeWhenIdle) {
        QDialog::reject();
        return;
    }
    const mnemonicrestore::Plan &plan = worker->plan;
    if (ok && worker->haveKnown && plan.knownPath.empty() && plan.found.empty())
        ok = false;
    if (!ok) {
        worker->plan.Clear();
        enterError->setText(worker->haveKnown && error.isEmpty()
            ? tr("The address does not belong to these recovery words.")
            : errorText(error));
        enterError->show();
        stack->setCurrentIndex(Enter);
        return;
    }
    const int unit = model->getOptionsModel() ? model->getOptionsModel()->getDisplayUnit() : AokChainUnits::AOK;
    CAmount total = 0;
    resultTable->setRowCount(plan.found.size());
    int row = 0;
    for (const mnemonicrestore::FoundAddress &entry : plan.found) {
        total += entry.nAmount;
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(EncodeDestination(entry.keyid)));
        addressItem->setFont(GUIUtil::monoFont(13));
        QTableWidgetItem *pathItem = new QTableWidgetItem(QString::fromStdString(entry.path));
        pathItem->setFont(GUIUtil::monoFont(12));
        QString amount = amountWithUnit(unit, entry.nAmount);
        if (entry.fTokens)
            amount += " + " + tr("tokens");
        QTableWidgetItem *amountItem = new QTableWidgetItem(amount);
        amountItem->setFont(GUIUtil::monoFont(13));
        amountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        resultTable->setItem(row, 0, addressItem);
        resultTable->setItem(row, 1, pathItem);
        resultTable->setItem(row, 2, amountItem);
        ++row;
    }
    if (plan.found.empty())
        resultSummary->setText(tr("The address belongs to these words (key path %1). It holds no coins now; its keys can still be added.").arg(QString::fromStdString(plan.knownPath)));
    else
        resultSummary->setText(tr("Addresses of these words that hold coins now: %n. Together:", "", plan.found.size()) + " <b>" + amountWithUnit(unit, total) + "</b>");
    resultTable->setVisible(!plan.found.empty());
    resultNote->setText(tr("Keys that will be added to this wallet: %n (the addresses above and the next ones on the same key paths). They are separate from the keys this wallet makes itself, so make a new backup afterwards. Then the wallet scans the blocks of the past for their transactions; that can take a while.", "", plan.keys.size()));
    stack->setCurrentIndex(Result);
}

void MnemonicDialog::importKeys()
{
    if (busy)
        return;
    std::string error;
    {
        // an encrypted wallet has to be open to store the keys (they are stored encrypted)
        WalletModel::UnlockContext context(model->requestUnlock());
        if (!context.isValid())
            return;
        imported = mnemonicrestore::ImportKeys(model->getWallet(), worker->plan, tr("Restored").toStdString(), error);
    }
    worker->plan.Clear();
    if (imported < 0) {
        QMessageBox::critical(this, windowTitle(), errorText(QString::fromStdString(error)));
        stack->setCurrentIndex(Enter);
        return;
    }
    rescanText->setText(tr("Keys added: %n. Scanning the blocks...", "", imported));
    rescanBar->setValue(0);
    stack->setCurrentIndex(Rescan);
    busy = true;
    worker->cancel = false;
    model->setProgressQuiet(true);
    Q_EMIT startRescan();
}

void MnemonicDialog::rescanProgress(int percent)
{
    rescanBar->setValue(percent);
}

void MnemonicDialog::rescanDone(bool ok, const QString &error)
{
    busy = false;
    model->setProgressQuiet(false);
    if (closeWhenIdle) {
        QDialog::reject();
        return;
    }
    const int unit = model->getOptionsModel() ? model->getOptionsModel()->getDisplayUnit() : AokChainUnits::AOK;
    if (ok) {
        doneText->setText(tr("Done. Keys added: %n. The blocks were scanned.", "", imported) + " "
            + tr("Balance of the wallet now: %1").arg(amountWithUnit(unit, model->getBalance())));
    } else {
        doneText->setProperty("role", "resultError");
        doneText->setText(tr("The keys were added, but the scan did not finish: %1").arg(errorText(error)) + " "
            + tr("Start the program with -rescan to scan again."));
    }
    stack->setCurrentIndex(Done);
}

void MnemonicDialog::reject()
{
    if (busy) {
        // the worker is asked to stop; the window closes when it has
        worker->cancel = true;
        closeWhenIdle = stack->currentIndex() != Rescan;
        if (stack->currentIndex() == Rescan) {
            rescanStop->setEnabled(false);
            rescanText->setText(tr("Stopping..."));
        }
        return;
    }
    QDialog::reject();
}
