// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "signhubpage.h"

#include "addresstablemodel.h"
#include "sendconfirmdialog.h"

#include "addressbookpage.h"
#include "aokchainamountfield.h"
#include "aokchainunits.h"
#include "guiutil.h"
#include "pagewidgets.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "walletmodel.h"

#include "ui_interface.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum Page { PageHub, PageProve, PageVerify, PageCreate, PageJoin, PageWallet, PageRequest };

QString amountText(CAmount amount)
{
    return GUIUtil::withThousands(AokChainUnits::format(AokChainUnits::AOK, amount, false, AokChainUnits::separatorNever));
}

QLabel *caption(const QString& text, QWidget *parent = nullptr)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "caption");
    label->setWordWrap(true);
    return label;
}

QLabel *heading(const QString& text, QWidget *parent = nullptr)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "title");
    return label;
}

QLabel *fieldLabel(const QString& text, QWidget *parent = nullptr)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "fieldLabel");
    return label;
}

QFrame *card(QWidget *parent, QVBoxLayout **body, int spacing = 12)
{
    QFrame *frame = new QFrame(parent);
    frame->setProperty("role", "card");
    QVBoxLayout *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(spacing);
    *body = layout;
    return frame;
}

/** A small rounded label: "ok" (green), "pend" (amber) or "plain". */
QLabel *badge(const QString& text, const char *kind, QWidget *parent = nullptr)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QColor back = t.chip, front = t.chipText;
    if (QString(kind) == "ok") {
        back = t.okSoft;
        front = t.ok;
    } else if (QString(kind) == "pend") {
        back = t.warnBg;
        front = t.warnText;
    } else if (QString(kind) == "bad") {
        back = t.errorSoft;
        front = t.error;
    }
    QLabel *label = new QLabel(text, parent);
    label->setStyleSheet(QString("QLabel { background-color: %1; color: %2; border-radius: 12px; padding: 4px 11px; font-size: 12px; font-weight: 600; }")
                             .arg(back.name(), front.name()));
    label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return label;
}

/** The round mark of a participant or of a step. */
QLabel *avatar(const QString& text, int size, bool filled, QWidget *parent = nullptr)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QLabel *label = new QLabel(text, parent);
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QString("QLabel { background-color: %1; color: %2; border-radius: %3px; font-size: %4px; font-weight: 600; }")
                             .arg((filled ? t.fill : t.accentSoft).name(), (filled ? t.onFill : t.accent).name())
                             .arg(size / 2).arg(size >= 36 ? 14 : 12));
    return label;
}

QLabel *iconTile(const QString& icon, int size, QWidget *parent = nullptr)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QLabel *label = new QLabel(parent);
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(GUIUtil::lineIcon(icon, size / 2, t.accent, 2.0));
    label->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 12px; }").arg(t.accentSoft.name()));
    return label;
}

QPushButton *linkButton(const QString& text, QWidget *parent = nullptr)
{
    QPushButton *button = new QPushButton(text, parent);
    button->setProperty("look", "link");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton *button(const QString& text, bool primary, QWidget *parent = nullptr, int height = 40)
{
    QPushButton *b = new QPushButton(text, parent);
    if (primary)
        b->setProperty("primary", true);
    b->setFixedHeight(height);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoDefault(false);
    return b;
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}

QFrame *rule(QWidget *parent = nullptr)
{
    QFrame *line = new QFrame(parent);
    line->setProperty("role", "hline");
    line->setFixedHeight(1);
    return line;
}

/** A box that says something that matters: "notice" (tinted), "warning" (amber) or "error" (red). */
QLabel *noteBox(const QString& text, const char *kind, QWidget *parent = nullptr)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", QString(kind) == "error" ? "resultError" : kind);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QScrollArea *scrollOf(QWidget *body)
{
    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->setObjectName("hubScrollBody");
    body->setStyleSheet("QWidget#hubScrollBody { background: transparent; }");
    scroll->setWidget(body);
    scroll->viewport()->setAutoFillBackground(false);
    return scroll;
}

QString ruleText(int required, int total)
{
    return SignHubPage::tr("%1-of-%2 · %3 of %2 sign").arg(required).arg(total).arg(required);
}

} // namespace

SignHubPage::SignHubPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent), platformStyle(_platformStyle), walletModel(nullptr), model(nullptr), myNameEdit(nullptr), draftCreated(false)
{
    setObjectName("pageShell");
    setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(36, 26, 36, 28);
    stack = new QStackedWidget(this);
    layout->addWidget(stack);
    stack->addWidget(buildHub());
    stack->addWidget(buildProve());
    stack->addWidget(buildVerify());
    stack->addWidget(buildCreate());
    stack->addWidget(buildJoin());
    stack->addWidget(buildWallet());
    stack->addWidget(buildRequest());
}

void SignHubPage::setModel(WalletModel *_walletModel)
{
    walletModel = _walletModel;
    if (!walletModel)
        return;
    model = new MultisigModel(walletModel, this);
    connect(model, SIGNAL(changed()), this, SLOT(modelChanged()));
    connect(model, SIGNAL(walletAdded(QString,QString)), this, SLOT(walletAdded(QString,QString)));
    fillHub();
    model->refresh();
}

void SignHubPage::modelChanged()
{
    fillHub();
    if (stack->currentIndex() == PageWallet)
        fillWalletPage();
    else if (stack->currentIndex() == PageRequest)
        fillRequestPage();
}

QLayout *SignHubPage::subHeader(const QString& title, QLabel **titleLabel, QWidget *right)
{
    QHBoxLayout *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(14);
    QPushButton *back = new QPushButton(QString::fromUtf8("‹ ") + tr("Sign"));
    back->setProperty("look", "link");
    back->setCursor(Qt::PointingHandCursor);
    back->setToolTip(tr("Back to the first page of Sign"));
    connect(back, SIGNAL(clicked()), this, SLOT(showHub()));
    header->addWidget(back);
    QLabel *label = new QLabel(title);
    label->setProperty("role", "pageTitle");
    label->setFont(GUIUtil::appFont(24, QFont::Bold));
    label->setMinimumHeight(40);
    header->addWidget(label);
    if (titleLabel)
        *titleLabel = label;
    header->addStretch();
    if (right)
        header->addWidget(right, 0, Qt::AlignVCenter);
    return header;
}

// ------------------------------------------------------------------ first page

QWidget *SignHubPage::buildHub()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    QHBoxLayout *header = new QHBoxLayout();
    header->setSpacing(14);
    QLabel *title = new QLabel(tr("Sign"), page);
    title->setProperty("role", "pageTitle");
    title->setFont(GUIUtil::appFont(24, QFont::Bold));
    title->setMinimumHeight(40);
    header->addWidget(title);
    QLabel *lead = caption(tr("Choose what you want to do and the wallet guides you step by step"), page);
    lead->setWordWrap(false);
    header->addWidget(lead, 0, Qt::AlignBottom);
    header->addStretch();
    layout->addLayout(header);

    // the three tasks
    QHBoxLayout *tasks = new QHBoxLayout();
    tasks->setSpacing(20);
    struct Task { QString icon, title, chip, text, action; const char *slot; bool main; };
    const Task list[] = {
        {"sign", tr("Prove my address"), QString(),
         tr("Proves that you are the owner of an address by signing a text with it. Used when an exchange or a partner asks you to confirm that an address is yours. No coins move."),
         tr("Choose an address and sign"), SLOT(showProve()), false},
        {"shield", tr("Check a signature"), QString(),
         tr("Paste the address, the text and the signature that someone sent, and the wallet tells you whether they are genuine. Pasted in one piece, they are split automatically."),
         tr("Paste a signature"), SLOT(showVerify()), false},
        {"people", tr("Multisig"), tr("multi-signature"),
         tr("A multisig (multi-signature) address lets coins go out only when a set number of several people sign. Example: 2 of 3 (2-of-3). The mistake of one person, or a theft from one person, does not move the coins."),
         tr("Make a multisig wallet"), SLOT(showCreate()), true},
    };
    for (const Task& task : list) {
        QVBoxLayout *body = nullptr;
        QFrame *frame = card(page, &body, 10);
        frame->setMinimumHeight(250);
        if (task.main)
            frame->setStyleSheet(QString("QFrame[role=\"card\"] { border-color: %1; }").arg(GUIUtil::theme().accent.name()));
        body->addWidget(iconTile(task.icon, 44, frame));
        body->addSpacing(4);
        QHBoxLayout *titleRow = new QHBoxLayout();
        titleRow->setSpacing(8);
        QLabel *name = new QLabel(task.title, frame);
        name->setFont(GUIUtil::appFont(17, QFont::DemiBold));
        titleRow->addWidget(name);
        if (!task.chip.isEmpty()) {
            QLabel *chip = new QLabel(task.chip, frame);
            chip->setProperty("role", "tag");
            titleRow->addWidget(chip);
        }
        titleRow->addStretch();
        body->addLayout(titleRow);
        body->addWidget(caption(task.text, frame));
        body->addStretch(1);
        if (task.main) {
            QPushButton *what = linkButton(tr("What is multisig?"), frame);
            connect(what, SIGNAL(clicked()), this, SLOT(showHelp()));
            body->addWidget(what, 0, Qt::AlignLeft);
        }
        QPushButton *go = button(task.action, task.main, frame);
        connect(go, SIGNAL(clicked()), this, task.slot);
        body->addWidget(go);
        tasks->addWidget(frame, 1);
    }
    layout->addLayout(tasks);

    // the multisig wallets of this wallet
    QVBoxLayout *body = nullptr;
    QFrame *wallets = card(page, &body, 6);
    QHBoxLayout *top = new QHBoxLayout();
    top->setSpacing(18);
    top->addWidget(heading(tr("My multisig wallets"), wallets));
    top->addStretch();
    QPushButton *myKey = linkButton(tr("Copy my public key"), wallets);
    myKey->setToolTip(tr("The key to give to the person who makes a multisig wallet with you"));
    QPushButton *openRequest = linkButton(tr("Open a signing request"), wallets);
    openRequest->setToolTip(tr("A signing request that another participant sent you as a file or as a code"));
    QPushButton *join = linkButton(tr("Add a multisig wallet I was invited to"), wallets);
    top->addWidget(myKey);
    top->addWidget(openRequest);
    top->addWidget(join);
    connect(myKey, SIGNAL(clicked()), this, SLOT(copyMyKey()));
    connect(openRequest, SIGNAL(clicked()), this, SLOT(openReceivedRequest()));
    connect(join, SIGNAL(clicked()), this, SLOT(showJoin()));
    body->addLayout(top);
    QWidget *listBody = new QWidget();
    walletList = new QVBoxLayout(listBody);
    walletList->setContentsMargins(0, 0, 6, 0);
    walletList->setSpacing(0);
    body->addWidget(scrollOf(listBody), 1);
    body->addWidget(rule(wallets));
    QHBoxLayout *foot = new QHBoxLayout();
    QLabel *footText = caption(tr("Coins at a multisig address cannot be sent by one person alone. You make a send request, and when the set number of participants have signed it in their own wallets, it is sent."), wallets);
    foot->addWidget(footText, 1);
    QPushButton *more = linkButton(tr("More about multisig"), wallets);
    connect(more, SIGNAL(clicked()), this, SLOT(showHelp()));
    foot->addWidget(more);
    body->addLayout(foot);
    layout->addWidget(wallets, 1);
    return page;
}

void SignHubPage::fillHub()
{
    clearLayout(walletList);
    if (!model)
        return;
    const QList<MultisigWallet> wallets = model->wallets();
    if (wallets.isEmpty()) {
        QLabel *empty = caption(tr("No multisig wallet yet. Make one with \"Make a multisig wallet\", or add one that someone else made with \"Add a multisig wallet I was invited to\"."));
        empty->setContentsMargins(0, 18, 0, 18);
        walletList->addWidget(empty);
        walletList->addStretch(1);
        return;
    }
    for (const MultisigWallet& wallet : wallets) {
        walletList->addWidget(rule());
        QWidget *row = new QWidget();
        QHBoxLayout *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 12, 0, 12);
        line->setSpacing(16);
        line->addWidget(iconTile("people", 40, row));
        QVBoxLayout *names = new QVBoxLayout();
        names->setSpacing(2);
        QHBoxLayout *nameRow = new QHBoxLayout();
        nameRow->setSpacing(8);
        QLabel *name = new QLabel(wallet.name.isEmpty() ? tr("(no name)") : wallet.name, row);
        name->setFont(GUIUtil::appFont(14, QFont::DemiBold));
        nameRow->addWidget(name);
        QLabel *chip = new QLabel(ruleText(wallet.required, wallet.keys.size()), row);
        chip->setProperty("role", "tag");
        nameRow->addWidget(chip);
        if (!wallet.hasMyKey())
            nameRow->addWidget(badge(tr("watch only"), "plain", row));
        nameRow->addStretch();
        names->addLayout(nameRow);
        QLabel *address = new QLabel(GUIUtil::shortAddress(wallet.address), row);
        address->setFont(GUIUtil::monoFont(12));
        address->setProperty("role", "secondary");
        address->setToolTip(wallet.address);
        names->addWidget(address);
        line->addLayout(names, 1);
        AmountLabel *balance = new AmountLabel(15, QFont::Medium, row);
        balance->setAmount(amountText(wallet.balance), "AOK");
        balance->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        line->addWidget(balance);
        const QString walletAddress = wallet.address;
        if (wallet.needMySignature > 0 || wallet.waiting > 0) {
            QPushButton *pending = new QPushButton(wallet.needMySignature > 0 ? tr("My signature needed: %1").arg(wallet.needMySignature)
                                                                               : tr("Waiting for signatures: %1").arg(wallet.waiting), row);
            pending->setProperty("look", wallet.needMySignature > 0 ? "pend" : "wait");
            pending->setCursor(Qt::PointingHandCursor);
            connect(pending, &QPushButton::clicked, this, [this, walletAddress] {
                // one open request: straight to it
                QStringList open;
                for (const MultisigRequest& request : model->requests(walletAddress)) {
                    if (request.txid.isEmpty() && !model->analyze(request).spent)
                        open << request.id;
                }
                if (open.size() == 1)
                    showRequest(open.first());
                else
                    showWallet(walletAddress);
            });
            line->addWidget(pending);
        } else {
            line->addWidget(badge(tr("No open request"), "plain", row));
        }
        QPushButton *send = button(tr("Send"), wallet.needMySignature == 0 && wallet.hasMyKey(), row, 36);
        send->setFixedWidth(96);
        connect(send, &QPushButton::clicked, this, [this, walletAddress] { showWallet(walletAddress); });
        line->addWidget(send);
        walletList->addWidget(row);
    }
    walletList->addStretch(1);
}

void SignHubPage::showHub()
{
    stack->setCurrentIndex(PageHub);
    if (model)
        model->refresh();
}

void SignHubPage::copyMyKey()
{
    if (!model)
        return;
    QString pubkey, error;
    if (!model->myKey(pubkey, error)) {
        QMessageBox::warning(this, tr("My public key"), error);
        return;
    }
    const QString code = MultisigModel::keyCode(pubkey);
    GUIUtil::setClipboard(code);
    QMessageBox box(QMessageBox::Information, tr("My public key"),
                    tr("Your public key was copied. Send it to the person who makes the multisig wallet, by messenger or mail."), QMessageBox::Ok, this);
    box.setInformativeText(QString("<span style=\"font-family:'Noto Sans Mono'; font-size:12px\">%1</span><br><br>%2")
                               .arg(code.left(44) + "<br>" + code.mid(44),
                                    tr("A public key only says \"this person is a signer\". It cannot take coins out, so it is safe to share. Never send your recovery words or a private key to anyone.")));
    box.exec();
}

// ------------------------------------------------------------------ prove an address

QWidget *SignHubPage::buildProve()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);
    layout->addLayout(subHeader(tr("Prove my address")));

    QVBoxLayout *body = nullptr;
    QFrame *frame = card(page, &body, 8);
    frame->setMaximumWidth(900);
    body->addWidget(noteBox(tr("You sign a text with one of your addresses. Whoever gets the address, the text and the signature can check that the owner of the address wrote it. No coins move, and nothing is sent to the network."), "notice", frame));
    body->addSpacing(6);
    body->addWidget(fieldLabel(tr("1. My address"), frame));
    QHBoxLayout *addressRow = new QHBoxLayout();
    addressRow->setSpacing(8);
    proveAddress = new QLineEdit(frame);
    proveAddress->setProperty("look", "field");
    proveAddress->setFont(GUIUtil::monoFont(13));
    proveAddress->setFixedHeight(44);
    proveAddress->setPlaceholderText(tr("Choose one of the addresses of this wallet"));
    addressRow->addWidget(proveAddress, 1);
    QPushButton *choose = button(tr("Choose my address"), false, frame, 44);
    choose->setIcon(QIcon(GUIUtil::lineIcon("addressbook", 16, GUIUtil::theme().text, 1.8)));
    connect(choose, SIGNAL(clicked()), this, SLOT(chooseProveAddress()));
    addressRow->addWidget(choose);
    body->addLayout(addressRow);
    body->addSpacing(6);
    body->addWidget(fieldLabel(tr("2. The text to sign"), frame));
    proveText = new QPlainTextEdit(frame);
    proveText->setPlaceholderText(tr("For example the sentence that the exchange asked you to sign"));
    proveText->setMinimumHeight(70);
    proveText->setMaximumHeight(110);
    body->addWidget(proveText);
    QHBoxLayout *signRow = new QHBoxLayout();
    QPushButton *sign = button(tr("Sign"), true, frame, 42);
    sign->setMinimumWidth(150);
    connect(sign, SIGNAL(clicked()), this, SLOT(signText()));
    signRow->addWidget(sign);
    proveStatus = new QLabel(frame);
    proveStatus->hide();
    signRow->addWidget(proveStatus);
    signRow->addStretch();
    body->addLayout(signRow);
    body->addSpacing(6);
    body->addWidget(fieldLabel(tr("3. Send this to the other side, as it is"), frame));
    proveResult = new QPlainTextEdit(frame);
    proveResult->setReadOnly(true);
    proveResult->setFont(GUIUtil::monoFont(12));
    proveResult->setPlaceholderText(tr("The address, the text and the signature appear here as one block"));
    proveResult->setMinimumHeight(120);
    body->addWidget(proveResult, 1);
    QHBoxLayout *copyRow = new QHBoxLayout();
    proveCopy = button(tr("Copy all"), false, frame);
    proveCopy->setIcon(QIcon(GUIUtil::lineIcon("copy", 15, GUIUtil::theme().text, 2.0)));
    proveCopy->setEnabled(false);
    connect(proveCopy, &QPushButton::clicked, this, [this] {
        GUIUtil::setClipboard(proveResult->toPlainText());
        proveCopy->setText(tr("Copied"));
        QTimer::singleShot(1500, proveCopy, [this] { proveCopy->setText(tr("Copy all")); });
    });
    copyRow->addWidget(proveCopy);
    copyRow->addStretch();
    body->addLayout(copyRow);
    connect(proveAddress, &QLineEdit::textEdited, this, [this] { proveStatus->hide(); });
    layout->addWidget(frame, 1);
    return page;
}

void SignHubPage::showProve(const QString& address)
{
    if (!address.isEmpty())
        proveAddress->setText(address);
    stack->setCurrentIndex(PageProve);
}

void SignHubPage::chooseProveAddress()
{
    if (!walletModel || !walletModel->getAddressTableModel())
        return;
    AddressBookPage dialog(platformStyle, AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
    dialog.setModel(walletModel->getAddressTableModel());
    if (dialog.exec()) {
        proveAddress->setText(dialog.getReturnValue());
        proveText->setFocus();
    }
}

static void showStatusLabel(QLabel *label, bool fine, const QString& text)
{
    label->setProperty("role", fine ? "resultOk" : "resultError");
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->setText(text);
    label->show();
}

void SignHubPage::signText()
{
    if (!model)
        return;
    proveResult->clear();
    proveCopy->setEnabled(false);
    if (proveAddress->text().trimmed().isEmpty()) {
        showStatusLabel(proveStatus, false, tr("Choose the address first."));
        return;
    }
    if (proveText->toPlainText().isEmpty()) {
        showStatusLabel(proveStatus, false, tr("Enter the text to sign."));
        return;
    }
    WalletModel::UnlockContext context(walletModel->requestUnlock());
    if (!context.isValid()) {
        showStatusLabel(proveStatus, false, tr("The wallet was not unlocked."));
        return;
    }
    QString signature, error;
    if (!model->signMessage(proveAddress->text(), proveText->toPlainText(), signature, error)) {
        showStatusLabel(proveStatus, false, error);
        return;
    }
    proveResult->setPlainText(MultisigModel::messageBlock(proveAddress->text().trimmed(), proveText->toPlainText(), signature));
    proveCopy->setEnabled(true);
    showStatusLabel(proveStatus, true, tr("Signed. Copy the block below and send it."));
}

// ------------------------------------------------------------------ check a signature

QWidget *SignHubPage::buildVerify()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);
    layout->addLayout(subHeader(tr("Check a signature")));

    QHBoxLayout *columns = new QHBoxLayout();
    columns->setSpacing(20);
    QVBoxLayout *body = nullptr;
    QFrame *frame = card(page, &body, 8);
    body->addWidget(heading(tr("Paste what you received"), frame));
    body->addWidget(caption(tr("Paste the whole block that the other side sent. The address, the text and the signature are taken apart for you."), frame));
    verifyPaste = new QPlainTextEdit(frame);
    verifyPaste->setFont(GUIUtil::monoFont(12));
    verifyPaste->setPlaceholderText("-----BEGIN AOKCHAIN SIGNED MESSAGE-----\n...");
    body->addWidget(verifyPaste, 1);
    QHBoxLayout *pasteRow = new QHBoxLayout();
    QPushButton *paste = button(tr("Paste"), false, frame);
    paste->setIcon(QIcon(GUIUtil::lineIcon("paste", 15, GUIUtil::theme().text, 2.0)));
    connect(paste, &QPushButton::clicked, this, [this] { verifyPaste->setPlainText(QApplication::clipboard()->text()); });
    QPushButton *check = button(tr("Check"), true, frame);
    check->setMinimumWidth(130);
    connect(check, SIGNAL(clicked()), this, SLOT(verifyFields()));
    QPushButton *clear = button(tr("Clear"), false, frame);
    connect(clear, &QPushButton::clicked, this, [this] {
        verifyPaste->clear(); verifyAddress->clear(); verifyText->clear(); verifySignature->clear();
        showVerifyResult(false, false, QString());
    });
    pasteRow->addWidget(paste);
    pasteRow->addStretch();
    pasteRow->addWidget(clear);
    pasteRow->addWidget(check);
    body->addLayout(pasteRow);
    columns->addWidget(frame, 1);

    QVBoxLayout *right = nullptr;
    QFrame *fields = card(page, &right, 6);
    right->addWidget(heading(tr("Or enter the three parts"), fields));
    right->addWidget(fieldLabel(tr("Address"), fields));
    verifyAddress = new QLineEdit(fields);
    verifyAddress->setFont(GUIUtil::monoFont(13));
    verifyAddress->setFixedHeight(40);
    right->addWidget(verifyAddress);
    right->addWidget(fieldLabel(tr("Text"), fields));
    verifyText = new QPlainTextEdit(fields);
    verifyText->setMinimumHeight(60);
    right->addWidget(verifyText, 1);
    right->addWidget(fieldLabel(tr("Signature"), fields));
    verifySignature = new QLineEdit(fields);
    verifySignature->setFont(GUIUtil::monoFont(12));
    verifySignature->setFixedHeight(40);
    right->addWidget(verifySignature);
    right->addSpacing(8);
    // the result, large
    verifyBanner = new QWidget(fields);
    verifyBanner->setObjectName("verifyBanner");
    verifyBanner->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *banner = new QVBoxLayout(verifyBanner);
    banner->setContentsMargins(18, 14, 18, 14);
    banner->setSpacing(4);
    verifyResult = new QLabel(verifyBanner);
    verifyResult->setFont(GUIUtil::appFont(20, QFont::Bold));
    verifyReason = new QLabel(verifyBanner);
    verifyReason->setWordWrap(true);
    banner->addWidget(verifyResult);
    banner->addWidget(verifyReason);
    right->addWidget(verifyBanner);
    columns->addWidget(fields, 1);
    layout->addLayout(columns, 1);
    showVerifyResult(false, false, QString());
    connect(verifyPaste, SIGNAL(textChanged()), this, SLOT(verifyPasted()));
    return page;
}

void SignHubPage::showVerify(const QString& address)
{
    if (!address.isEmpty())
        verifyAddress->setText(address);
    stack->setCurrentIndex(PageVerify);
}

void SignHubPage::showVerifyResult(bool checked, bool valid, const QString& reason)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    const QColor back = !checked ? t.surfaceAlt : valid ? t.okSoft : t.errorSoft;
    const QColor front = !checked ? t.textSecondary : valid ? t.ok : t.error;
    verifyBanner->setStyleSheet(QString("QWidget#verifyBanner { background-color: %1; border-radius: 12px; } QLabel { color: %2; background: transparent; }")
                                    .arg(back.name(), front.name()));
    verifyResult->setText(!checked ? tr("Not checked yet") : valid ? tr("Valid") : tr("Not valid"));
    verifyReason->setText(!checked ? tr("The result appears here.") : reason);
}

void SignHubPage::verifyPasted()
{
    QString address, text, signature;
    if (!MultisigModel::parseMessageBlock(verifyPaste->toPlainText(), address, text, signature))
        return;
    verifyAddress->setText(address);
    verifyText->setPlainText(text);
    verifySignature->setText(signature);
    verifyFields();
}

void SignHubPage::verifyFields()
{
    if (!model)
        return;
    if (verifyAddress->text().trimmed().isEmpty() || verifySignature->text().trimmed().isEmpty()) {
        showVerifyResult(true, false, verifyPaste->toPlainText().trimmed().isEmpty()
            ? tr("Paste the block, or fill in the address, the text and the signature.")
            : tr("The pasted text is not a complete signed block. Copy it again from the first line to the last, or fill in the three parts."));
        return;
    }
    QString reason;
    const bool valid = model->verifyMessage(verifyAddress->text(), verifyText->toPlainText(), verifySignature->text(), reason);
    showVerifyResult(true, valid, valid ? tr("The owner of the address %1 signed exactly this text.").arg(verifyAddress->text().trimmed()) : reason);
}

// ------------------------------------------------------------------ what multisig is

void SignHubPage::showHelp()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("What is multisig?"));
    dialog.resize(720, 680);
    QVBoxLayout *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    QWidget *body = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(body);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(10);
    auto section = [&](const QString& title, const QStringList& paragraphs) {
        QLabel *head = heading(title, body);
        head->setContentsMargins(0, 8, 0, 0);
        layout->addWidget(head);
        for (const QString& paragraph : paragraphs) {
            QLabel *text = new QLabel(paragraph, body);
            text->setWordWrap(true);
            text->setProperty("role", "secondary");
            text->setTextFormat(Qt::RichText);
            layout->addWidget(text);
        }
    };
    QLabel *title = new QLabel(tr("Multisig (multi-signature)"), body);
    title->setFont(GUIUtil::appFont(22, QFont::Bold));
    layout->addWidget(title);
    section(tr("What it is"), {
        tr("A multisig address is an address from which coins go out only when <b>M of the N people you decided on</b> sign. Receiving works like with any address: anyone can send coins to it."),
    });
    section(tr("Why use it"), {
        tr("The mistake of one person, or a theft from one person, does not move the coins. It is used for company funds, for money that several people manage together, and by one person as a two-step vault (two devices, both must sign)."),
    });
    section(tr("What M-of-N means"), {
        tr("<b>2-of-2</b>: two people, both must sign. Nobody can send alone; if one of them loses the wallet, the coins are stuck."),
        tr("<b>2-of-3</b>: three people, any two sign. One person can be away or lose the wallet, and the other two can still send."),
    });
    section(tr("Public key and private key"), {
        tr("Every participant gives one <b>public key</b>. A public key only says \"this person is a signer\". It cannot take coins out, so it is <b>safe to share</b> by messenger or mail."),
        tr("The <b>private key</b> and the <b>recovery words</b> stay in each participant's own wallet. <b>Never send them to anyone</b>, also not to the other participants."),
    });
    // how a spend works, as three steps
    QLabel *how = heading(tr("How coins are sent"), body);
    how->setContentsMargins(0, 8, 0, 0);
    layout->addWidget(how);
    QHBoxLayout *steps = new QHBoxLayout();
    steps->setSpacing(10);
    const QString stepTitles[3] = {tr("Make a request"), tr("Each one signs"), tr("Broadcast")};
    const QString stepTexts[3] = {
        tr("One participant enters the recipient and the amount. The wallet makes a \"signing request\"."),
        tr("The request is passed on as a file or a code. Each participant opens it in their own wallet, checks it and signs."),
        tr("When M signatures are there, anyone who has the request sends it to the network."),
    };
    for (int i = 0; i < 3; ++i) {
        QFrame *box = new QFrame(body);
        box->setProperty("role", "tile");
        QVBoxLayout *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(14, 12, 14, 12);
        boxLayout->setSpacing(6);
        QHBoxLayout *top = new QHBoxLayout();
        top->setSpacing(8);
        top->addWidget(avatar(QString::number(i + 1), 26, true, box));
        QLabel *name = new QLabel(stepTitles[i], box);
        name->setFont(GUIUtil::appFont(14, QFont::DemiBold));
        top->addWidget(name, 1);
        boxLayout->addLayout(top);
        boxLayout->addWidget(caption(stepTexts[i], box));
        boxLayout->addStretch();
        steps->addWidget(box, 1);
        if (i < 2) {
            QLabel *arrow = new QLabel(box);
            arrow->setPixmap(GUIUtil::lineIcon("chevron-right", 18, GUIUtil::theme().textSecondary, 2.0));
            steps->addWidget(arrow);
        }
    }
    layout->addLayout(steps);
    section(tr("What to back up"), {
        tr("Every participant backs up <b>their own wallet</b> (Settings → Wallet management → Backup), and keeps the <b>multisig information file</b> that is saved when the multisig wallet is made. With a wallet backup and that file, the multisig wallet can be set up again on another computer."),
    });
    layout->addWidget(noteBox(tr("The risk: if so many participants lose their keys that fewer than M are left, nobody can take the coins out any more. Not the developers either. Decide M and N with that in mind."), "warning", body));
    layout->addStretch(1);
    outer->addWidget(scrollOf(body), 1);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->setContentsMargins(28, 0, 28, 20);
    buttons->addStretch();
    QPushButton *close = button(tr("Close"), true, &dialog);
    close->setMinimumWidth(110);
    connect(close, SIGNAL(clicked()), &dialog, SLOT(accept()));
    buttons->addWidget(close);
    outer->addLayout(buttons);
    dialog.exec();
}

// ------------------------------------------------------------------ make a multisig wallet

QWidget *SignHubPage::buildCreate()
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    // the three steps, on the right of the title
    QWidget *stepper = new QWidget(page);
    QHBoxLayout *stepLayout = new QHBoxLayout(stepper);
    stepLayout->setContentsMargins(0, 0, 0, 0);
    stepLayout->setSpacing(10);
    const QString names[3] = {tr("Set the rule"), tr("Gather the participants"), tr("Check and finish")};
    for (int i = 0; i < 3; ++i) {
        QLabel *label = new QLabel(names[i], stepper);
        stepLabels.append(label);
        stepLayout->addWidget(label);
        if (i < 2) {
            QFrame *dash = new QFrame(stepper);
            dash->setFixedSize(28, 1);
            dash->setStyleSheet(QString("background-color: %1;").arg(t.borderStrong.name()));
            stepLayout->addWidget(dash);
        }
    }
    layout->addLayout(subHeader(tr("Make a multisig wallet"), nullptr, stepper));

    createStack = new QStackedWidget(page);
    layout->addWidget(createStack, 1);

    // --- step 1: name and rule
    {
        QWidget *step = new QWidget(createStack);
        QHBoxLayout *columns = new QHBoxLayout(step);
        columns->setContentsMargins(0, 0, 0, 0);
        columns->setSpacing(20);
        QVBoxLayout *body = nullptr;
        QFrame *frame = card(step, &body, 10);
        body->addWidget(heading(tr("Name and rule"), frame));
        body->addWidget(fieldLabel(tr("Name of the multisig wallet"), frame));
        createName = new QLineEdit(frame);
        createName->setProperty("look", "field");
        createName->setFixedHeight(44);
        createName->setMaxLength(40);
        createName->setPlaceholderText(tr("For example: Operating funds"));
        body->addWidget(createName);
        body->addSpacing(6);
        body->addWidget(fieldLabel(tr("How many people, and how many of them must sign"), frame));
        QHBoxLayout *presetRow = new QHBoxLayout();
        presetRow->setSpacing(10);
        presets = new QButtonGroup(this);
        const QString presetNames[4] = {"2-of-2", "2-of-3", "3-of-5", tr("Other")};
        const QString presetNotes[4] = {tr("both of two"), tr("any two of three"), tr("any three of five"), tr("set it yourself")};
        for (int i = 0; i < 4; ++i) {
            ChoiceButton *choice = new ChoiceButton(presetNames[i] + "\n" + presetNotes[i], ChoiceButton::Box, frame);
            choice->setFixedHeight(58);
            presets->addButton(choice, i);
            presetRow->addWidget(choice, 1);
        }
        body->addLayout(presetRow);
        QHBoxLayout *numbers = new QHBoxLayout();
        numbers->setSpacing(10);
        createN = new QSpinBox(frame);
        createN->setRange(2, MultisigModel::MAX_KEYS);
        createN->setFixedSize(84, 40);
        createM = new QSpinBox(frame);
        createM->setRange(1, MultisigModel::MAX_KEYS);
        createM->setFixedSize(84, 40);
        numbers->addWidget(new QLabel(tr("Participants"), frame));
        numbers->addWidget(createN);
        numbers->addSpacing(16);
        numbers->addWidget(new QLabel(tr("Signatures needed"), frame));
        numbers->addWidget(createM);
        numbers->addStretch();
        body->addLayout(numbers);
        ruleEffects = new QLabel(frame);
        ruleEffects->setProperty("role", "notice");
        ruleEffects->setWordWrap(true);
        ruleEffects->setTextFormat(Qt::RichText);
        body->addWidget(ruleEffects);
        body->addStretch(1);
        QHBoxLayout *buttons = new QHBoxLayout();
        buttons->addStretch();
        QPushButton *next = button(tr("Next: gather the participants"), true, frame, 44);
        next->setMinimumWidth(240);
        connect(next, &QPushButton::clicked, this, [this] { createStep(1); });
        buttons->addWidget(next);
        body->addLayout(buttons);
        columns->addWidget(frame, 3);

        QVBoxLayout *side = nullptr;
        QFrame *aside = card(step, &side, 8);
        side->addWidget(heading(tr("How to choose"), aside));
        side->addWidget(caption(tr("<b>2-of-2</b> suits two people who always decide together. If one of them loses the wallet, the coins are stuck."), aside));
        side->addWidget(caption(tr("<b>2-of-3</b> is the usual choice: it still works when one person is away or has lost the wallet."), aside));
        side->addWidget(caption(tr("<b>3-of-5</b> suits a larger group, such as a board."), aside));
        side->addWidget(caption(tr("The rule cannot be changed later. To change it, make a new multisig wallet and move the coins."), aside));
        side->addStretch(1);
        QPushButton *what = linkButton(tr("What is multisig?"), aside);
        connect(what, SIGNAL(clicked()), this, SLOT(showHelp()));
        side->addWidget(what, 0, Qt::AlignLeft);
        for (QLabel *label : aside->findChildren<QLabel*>())
            label->setTextFormat(Qt::RichText);
        columns->addWidget(aside, 2);
        createStack->addWidget(step);

        createN->setValue(3);
        createM->setValue(2);
        presets->button(1)->setChecked(true);
        connect(presets, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), this, [this](int id) {
            const int rules[3][2] = {{2, 2}, {2, 3}, {3, 5}};
            if (id < 3) {
                createN->setValue(rules[id][1]);
                createM->setValue(rules[id][0]);
            }
        });
        connect(createN, SIGNAL(valueChanged(int)), this, SLOT(ruleChanged()));
        connect(createM, SIGNAL(valueChanged(int)), this, SLOT(ruleChanged()));
    }

    // --- step 2: the participants
    {
        QWidget *step = new QWidget(createStack);
        QHBoxLayout *columns = new QHBoxLayout(step);
        columns->setContentsMargins(0, 0, 0, 0);
        columns->setSpacing(20);
        QVBoxLayout *body = nullptr;
        QFrame *frame = card(step, &body, 8);
        QHBoxLayout *top = new QHBoxLayout();
        participantsTitle = heading(QString(), frame);
        top->addWidget(participantsTitle);
        top->addStretch();
        participantsRule = caption(QString(), frame);
        participantsRule->setWordWrap(false);
        top->addWidget(participantsRule);
        body->addLayout(top);
        QWidget *rows = new QWidget();
        participantRows = new QVBoxLayout(rows);
        participantRows->setContentsMargins(0, 0, 6, 0);
        participantRows->setSpacing(0);
        body->addWidget(scrollOf(rows), 1);
        QHBoxLayout *buttons = new QHBoxLayout();
        QPushButton *previous = button(tr("Back"), false, frame, 44);
        connect(previous, &QPushButton::clicked, this, [this] { createStep(0); });
        buttons->addWidget(previous);
        buttons->addStretch();
        participantsNext = button(QString(), true, frame, 44);
        participantsNext->setMinimumWidth(260);
        connect(participantsNext, &QPushButton::clicked, this, [this] { createStep(2); });
        buttons->addWidget(participantsNext);
        body->addLayout(buttons);
        columns->addWidget(frame, 3);

        QVBoxLayout *asideLayout = new QVBoxLayout();
        asideLayout->setSpacing(16);
        QVBoxLayout *side = nullptr;
        QFrame *safe = card(step, &side, 8);
        side->addWidget(heading(tr("A public key is safe to share"), safe));
        side->addWidget(caption(tr("A public key only says \"this person is a signer\". It is not a key that can take coins out. You may send it by messenger or mail."), safe));
        QLabel *never = caption(tr("But <b>never send your recovery words or a private key to anyone.</b>"), safe);
        never->setTextFormat(Qt::RichText);
        side->addWidget(never);
        asideLayout->addWidget(safe);
        QFrame *how = card(step, &side, 8);
        side->addWidget(heading(tr("This is how multisig works"), how));
        const QString texts[3] = {
            tr("<b>Everyone's public key is gathered.</b> This is the step you are at."),
            tr("<b>The multisig address is made.</b> Send the finished file to the other participants, and the same multisig wallet appears in their wallets."),
            tr("<b>Coins are paid to the multisig address.</b> To take them out, the set number of participants must sign."),
        };
        for (int i = 0; i < 3; ++i) {
            QHBoxLayout *line = new QHBoxLayout();
            line->setSpacing(10);
            line->addWidget(avatar(QString::number(i + 1), 24, false, how), 0, Qt::AlignTop);
            QLabel *text = caption(texts[i], how);
            text->setTextFormat(Qt::RichText);
            line->addWidget(text, 1);
            side->addLayout(line);
        }
        stepHelpNeeded = noteBox(QString(), "warning", how);
        side->addWidget(stepHelpNeeded);
        side->addStretch(1);
        asideLayout->addWidget(how, 1);
        columns->addLayout(asideLayout, 2);
        createStack->addWidget(step);
    }

    // --- step 3: check and finish
    {
        QWidget *step = new QWidget(createStack);
        QVBoxLayout *outer = new QVBoxLayout(step);
        outer->setContentsMargins(0, 0, 0, 0);
        QVBoxLayout *body = nullptr;
        QFrame *frame = card(step, &body, 8);
        QWidget *inner = new QWidget();
        confirmBody = new QVBoxLayout(inner);
        confirmBody->setContentsMargins(0, 0, 6, 0);
        confirmBody->setSpacing(10);
        body->addWidget(scrollOf(inner), 1);
        outer->addWidget(frame);
        createStack->addWidget(step);
    }
    ruleChanged();
    return page;
}

void SignHubPage::showCreate()
{
    if (draftCreated) {
        // the last one is finished: start a new one
        draftCreated = false;
        createName->clear();
        myPubkey.clear();
        otherNames.clear();
        otherKeys.clear();
        otherStatus.clear();
        clearLayout(participantRows);
    }
    stack->setCurrentIndex(PageCreate);
    createStep(0);
}

void SignHubPage::ruleChanged()
{
    const int n = createN->value();
    createM->setMaximum(n);
    const int m = createM->value();
    // the preset that fits, if any
    const int rules[3][2] = {{2, 2}, {2, 3}, {3, 5}};
    int preset = 3;
    for (int i = 0; i < 3; ++i) {
        if (rules[i][0] == m && rules[i][1] == n)
            preset = i;
    }
    presets->button(preset)->setChecked(true);
    QStringList effects;
    effects << tr("To send coins, <b>%1 of the %2 participants</b> must sign.").arg(m).arg(n);
    if (m == 1)
        effects << tr("Any one participant can send the coins alone. If several people are to approve, set 2 or more.");
    if (m == n)
        effects << tr("If even one participant loses the wallet, the coins cannot be taken out. Everyone's backup is essential.");
    else
        effects << tr("If %1 lose their wallets, the others can still take the coins out. If %2 lose them, nobody can.").arg(n - m).arg(n - m + 1);
    ruleEffects->setText(effects.join("<br>"));
}

void SignHubPage::createStep(int step)
{
    if (!model)
        return;
    if (step == 1) {
        if (createName->text().trimmed().isEmpty()) {
            createName->setFocus();
            QMessageBox::information(this, tr("Make a multisig wallet"), tr("Give the multisig wallet a name, for example \"Operating funds\"."));
            return;
        }
        if (myPubkey.isEmpty()) {
            QString error;
            if (!model->myKey(myPubkey, error)) {
                QMessageBox::warning(this, tr("Make a multisig wallet"), error);
                return;
            }
        }
        buildParticipantRows();
        participantsChanged();
    } else if (step == 2) {
        QString error;
        if (!collectDraft(error)) {
            QMessageBox::warning(this, tr("Make a multisig wallet"), error);
            return;
        }
        fillConfirm();
    }
    createStack->setCurrentIndex(step);
    // the stepper
    const GUIUtil::Theme& t = GUIUtil::theme();
    for (int i = 0; i < stepLabels.size(); ++i) {
        const bool done = i < step, now = i == step;
        const QString mark = done ? QString::fromUtf8("✓") : QString::number(i + 1);
        const QString name = stepLabels[i]->property("stepName").isValid() ? stepLabels[i]->property("stepName").toString() : stepLabels[i]->text();
        stepLabels[i]->setProperty("stepName", name);
        stepLabels[i]->setTextFormat(Qt::RichText);
        stepLabels[i]->setText(QString("<span style=\"color:%1; font-weight:%2\">%3&nbsp;&nbsp;%4</span>")
                                   .arg((now ? t.accent : done ? t.ok : t.textSecondary).name(), now ? "700" : "500", mark, name.toHtmlEscaped()));
    }
}

void SignHubPage::buildParticipantRows()
{
    const int others = createN->value() - 1;
    // keep what was entered when the number of participants did not change
    if (otherKeys.size() == others && participantRows->count() > 0)
        return;
    QStringList keptNames, keptKeys;
    for (int i = 0; i < otherKeys.size(); ++i) {
        keptNames << otherNames[i]->text();
        keptKeys << otherKeys[i]->text();
    }
    const QString keptMine = participantRows->count() > 0 && myNameEdit ? myNameEdit->text() : QString();
    clearLayout(participantRows);
    otherNames.clear();
    otherKeys.clear();
    otherStatus.clear();

    // me
    QWidget *mine = new QWidget();
    QHBoxLayout *mineRow = new QHBoxLayout(mine);
    mineRow->setContentsMargins(0, 12, 0, 12);
    mineRow->setSpacing(12);
    mineRow->addWidget(avatar(tr("Me"), 36, true, mine));
    QVBoxLayout *mineText = new QVBoxLayout();
    mineText->setSpacing(4);
    QHBoxLayout *mineTop = new QHBoxLayout();
    mineTop->setSpacing(8);
    QLabel *me = new QLabel(tr("Me (this wallet)"), mine);
    me->setFont(GUIUtil::appFont(14, QFont::DemiBold));
    mineTop->addWidget(me);
    myNameEdit = new QLineEdit(mine);
    myNameEdit->setPlaceholderText(tr("My name, as the others will see it"));
    myNameEdit->setMaxLength(40);
    myNameEdit->setFixedHeight(34);
    myNameEdit->setMaximumWidth(260);
    myNameEdit->setText(keptMine);
    mineTop->addWidget(myNameEdit, 1);
    mineTop->addStretch();
    mineText->addLayout(mineTop);
    myKeyLabel = new QLabel(tr("Public key %1").arg(MultisigModel::keyCode(myPubkey).left(18) + QString::fromUtf8("…") + myPubkey.right(4)), mine);
    myKeyLabel->setFont(GUIUtil::monoFont(12));
    myKeyLabel->setProperty("role", "secondary");
    myKeyLabel->setToolTip(MultisigModel::keyCode(myPubkey));
    mineText->addWidget(myKeyLabel);
    mineRow->addLayout(mineText, 1);
    mineRow->addWidget(badge(tr("Ready"), "ok", mine));
    QPushButton *copy = button(tr("Copy my public key"), false, mine, 36);
    copy->setIcon(QIcon(GUIUtil::lineIcon("copy", 14, GUIUtil::theme().text, 2.0)));
    copy->setToolTip(tr("Only needed when another participant makes the multisig wallet instead of you"));
    connect(copy, &QPushButton::clicked, this, [this, copy] {
        GUIUtil::setClipboard(MultisigModel::keyCode(myPubkey));
        copy->setText(tr("Copied"));
        QTimer::singleShot(1500, copy, [copy] { copy->setText(tr("Copy my public key")); });
    });
    mineRow->addWidget(copy);
    participantRows->addWidget(mine);

    for (int i = 0; i < others; ++i) {
        participantRows->addWidget(rule());
        QWidget *row = new QWidget();
        QHBoxLayout *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 12, 0, 12);
        line->setSpacing(12);
        line->addWidget(avatar(QString::number(i + 2), 36, false, row), 0, Qt::AlignTop);
        QVBoxLayout *text = new QVBoxLayout();
        text->setSpacing(6);
        QHBoxLayout *top = new QHBoxLayout();
        top->addWidget(fieldLabel(tr("Name and public key of participant %1").arg(i + 2), row));
        top->addStretch();
        QLabel *status = badge(tr("Waiting for the key"), "plain", row);
        top->addWidget(status);
        text->addLayout(top);
        QHBoxLayout *inputs = new QHBoxLayout();
        inputs->setSpacing(8);
        QLineEdit *name = new QLineEdit(row);
        name->setPlaceholderText(tr("Name (e.g. CEO)"));
        name->setMaxLength(40);
        name->setFixedHeight(40);
        name->setFixedWidth(150);
        QLineEdit *key = new QLineEdit(row);
        key->setFont(GUIUtil::monoFont(12));
        key->setPlaceholderText(tr("Paste the public key that the participant sent"));
        key->setFixedHeight(40);
        QPushButton *paste = button(tr("Paste"), false, row, 40);
        connect(paste, &QPushButton::clicked, this, [key] { key->setText(QApplication::clipboard()->text().trimmed()); });
        inputs->addWidget(name);
        inputs->addWidget(key, 1);
        inputs->addWidget(paste);
        text->addLayout(inputs);
        if (i == 0) {
            QLabel *hint = caption(tr("The participant gets the code in their own wallet at <b>Sign → My multisig wallets → Copy my public key</b>."), row);
            hint->setTextFormat(Qt::RichText);
            text->addWidget(hint);
        }
        line->addLayout(text, 1);
        participantRows->addWidget(row);
        if (i < keptKeys.size()) {
            name->setText(keptNames[i]);
            key->setText(keptKeys[i]);
        }
        otherNames.append(name);
        otherKeys.append(key);
        otherStatus.append(status);
        connect(key, SIGNAL(textChanged(QString)), this, SLOT(participantsChanged()));
    }
    participantRows->addStretch(1);
}

void SignHubPage::participantsChanged()
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    const int n = createN->value(), m = createM->value();
    int ready = 1;
    QStringList seen;
    seen << myPubkey;
    for (int i = 0; i < otherKeys.size(); ++i) {
        QString hex, error;
        QString kind = "plain", text = tr("Waiting for the key");
        if (!otherKeys[i]->text().trimmed().isEmpty()) {
            if (!model->parseKeyCode(otherKeys[i]->text(), hex, error)) {
                kind = "bad";
                text = tr("Not a public key");
            } else if (seen.contains(hex)) {
                kind = "bad";
                text = hex == myPubkey ? tr("This is your own key") : tr("Same key twice");
                error = tr("Every participant needs a key of their own.");
            } else {
                kind = "ok";
                text = tr("Key received");
                seen << hex;
                ++ready;
            }
        }
        QColor back = t.chip, front = t.chipText;
        if (kind == "ok") { back = t.okSoft; front = t.ok; }
        if (kind == "bad") { back = t.errorSoft; front = t.error; }
        otherStatus[i]->setText(text);
        otherStatus[i]->setToolTip(error);
        otherStatus[i]->setStyleSheet(QString("QLabel { background-color: %1; color: %2; border-radius: 12px; padding: 4px 11px; font-size: 12px; font-weight: 600; }").arg(back.name(), front.name()));
    }
    participantsTitle->setText(tr("%1 of %2 participants ready").arg(ready).arg(n));
    participantsRule->setText(tr("Name: %1 · %2").arg(createName->text().trimmed(), ruleText(m, n)));
    participantsNext->setEnabled(ready == n);
    participantsNext->setText(ready == n ? tr("Next: check and finish") : tr("Next, when all participants are here"));
    stepHelpNeeded->setText(m == n
        ? tr("If one of the %1 participants loses the wallet, the coins cannot be taken out. Everyone must back up their wallet.").arg(n)
        : tr("If %1 of the %2 participants lose their wallets, the coins cannot be taken out. Everyone must back up their wallet.").arg(n - m + 1).arg(n));
}

bool SignHubPage::collectDraft(QString& error)
{
    QList<MultisigKey> keys;
    MultisigKey mine;
    mine.name = myNameEdit->text().trimmed().isEmpty() ? tr("Participant %1").arg(1) : myNameEdit->text().trimmed();
    mine.pubkey = myPubkey;
    keys.append(mine);
    for (int i = 0; i < otherKeys.size(); ++i) {
        MultisigKey key;
        key.name = otherNames[i]->text().trimmed().isEmpty() ? tr("Participant %1").arg(i + 2) : otherNames[i]->text().trimmed();
        key.pubkey = otherKeys[i]->text();
        keys.append(key);
    }
    if (!model->makeWallet(createName->text(), createM->value(), keys, draft, error))
        return false;
    MultisigWallet existing;
    if (model->wallet(draft.address, existing)) {
        error = tr("This wallet already has a multisig wallet with exactly these participants and this rule: \"%1\".").arg(existing.name);
        return false;
    }
    return true;
}

/** The lines that describe a multisig wallet: rule, participants, address. */
static void describeWallet(QVBoxLayout *layout, const MultisigWallet& wallet, QWidget *parent)
{
    QHBoxLayout *ruleRow = new QHBoxLayout();
    ruleRow->setSpacing(10);
    QLabel *name = new QLabel(wallet.name.isEmpty() ? SignHubPage::tr("(no name)") : wallet.name, parent);
    name->setFont(GUIUtil::appFont(18, QFont::DemiBold));
    ruleRow->addWidget(name);
    QLabel *chip = new QLabel(ruleText(wallet.required, wallet.keys.size()), parent);
    chip->setProperty("role", "tag");
    ruleRow->addWidget(chip);
    ruleRow->addStretch();
    layout->addLayout(ruleRow);
    layout->addWidget(fieldLabel(SignHubPage::tr("Multisig address"), parent));
    QLabel *address = new QLabel(wallet.address, parent);
    address->setFont(GUIUtil::monoFont(15, QFont::Medium));
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(address);
    layout->addWidget(fieldLabel(SignHubPage::tr("Participants"), parent));
    int number = 1;
    for (const MultisigKey& key : wallet.keys) {
        QHBoxLayout *line = new QHBoxLayout();
        line->setSpacing(10);
        line->addWidget(avatar(key.mine ? SignHubPage::tr("Me") : QString::number(number), 30, key.mine, parent));
        QLabel *who = new QLabel(key.mine ? SignHubPage::tr("%1 (this wallet)").arg(key.name) : key.name, parent);
        who->setMinimumWidth(170);
        line->addWidget(who);
        QLabel *pub = new QLabel(key.pubkey.left(10) + QString::fromUtf8("…") + key.pubkey.right(6), parent);
        pub->setFont(GUIUtil::monoFont(12));
        pub->setProperty("role", "secondary");
        pub->setToolTip(key.pubkey);
        line->addWidget(pub);
        line->addStretch();
        layout->addLayout(line);
        ++number;
    }
}

void SignHubPage::fillConfirm()
{
    clearLayout(confirmBody);
    QWidget *parent = confirmBody->parentWidget();
    confirmBody->addWidget(heading(draftCreated ? tr("The multisig wallet is ready") : tr("Check and finish"), parent));
    describeWallet(confirmBody, draft, parent);
    confirmBody->addWidget(caption(tr("The public keys are put in a fixed order (sorted), so every participant gets this same address, whatever the order in which the keys were entered."), parent));
    if (!draftCreated) {
        confirmBody->addWidget(noteBox(tr("Compare the participants with what you agreed on. The rule and the participants cannot be changed after this."), "notice", parent));
        confirmBody->addStretch(1);
        QHBoxLayout *buttons = new QHBoxLayout();
        QPushButton *previous = button(tr("Back"), false, parent, 44);
        connect(previous, &QPushButton::clicked, this, [this] { createStep(1); });
        buttons->addWidget(previous);
        buttons->addStretch();
        QPushButton *finish = button(tr("Make the multisig wallet"), true, parent, 44);
        finish->setMinimumWidth(240);
        connect(finish, SIGNAL(clicked()), this, SLOT(finishCreate()));
        buttons->addWidget(finish);
        confirmBody->addLayout(buttons);
        return;
    }
    confirmBody->addWidget(noteBox(tr("Next: send the multisig information to the other participants. They add it in their wallets at Sign → \"Add a multisig wallet I was invited to\". Keep the file yourself too: together with a wallet backup it brings this multisig wallet back."), "notice", parent));
    confirmBody->addWidget(caption(tr("The information holds the name, the rule, the public keys and the address. It holds no private key."), parent));
    confirmBody->addStretch(1);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    QPushButton *file = button(tr("Save the multisig information file"), true, parent, 44);
    QPushButton *code = button(tr("Copy the information code"), false, parent, 44);
    QPushButton *address = button(tr("Copy the address"), false, parent, 44);
    QPushButton *done = button(tr("Done"), false, parent, 44);
    done->setMinimumWidth(110);
    const MultisigWallet wallet = draft;
    connect(file, &QPushButton::clicked, this, [this, wallet] { shareWallet(wallet, true); });
    connect(code, &QPushButton::clicked, this, [this, wallet, code] {
        shareWallet(wallet, false);
        code->setText(tr("Copied"));
    });
    connect(address, &QPushButton::clicked, this, [wallet] { GUIUtil::setClipboard(wallet.address); });
    connect(done, SIGNAL(clicked()), this, SLOT(showHub()));
    buttons->addWidget(file);
    buttons->addWidget(code);
    buttons->addWidget(address);
    buttons->addStretch();
    buttons->addWidget(done);
    confirmBody->addLayout(buttons);
}

void SignHubPage::finishCreate()
{
    // a new address has no history: no rescan
    setEnabled(false);
    model->addWallet(draft, false);
}

void SignHubPage::walletAdded(const QString& address, const QString& error)
{
    setEnabled(true);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Multisig wallet"), tr("The multisig wallet could not be added: %1").arg(error));
        return;
    }
    if (stack->currentIndex() == PageCreate && address == draft.address) {
        draftCreated = true;
        model->wallet(address, draft);
        fillConfirm();
        return;
    }
    // a wallet that was added for a received request: open the request now
    if (!pendingRequestCode.isEmpty()) {
        const QString code = pendingRequestCode;
        pendingRequestCode.clear();
        MultisigRequest request;
        QString id, problem;
        if (model->parseRequestCode(code, request, problem) && model->importRequest(request, id, problem)) {
            showRequest(id);
            return;
        }
    }
    showHub();
}

void SignHubPage::saveCode(const QString& title, const QString& fileName, const QString& code)
{
    const QString path = GUIUtil::getSaveFileName(this, title, fileName, tr("AokChain multisig file (*.aokms)"), nullptr);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(code.toUtf8() + "\n") < 0)
        QMessageBox::critical(this, title, tr("The file could not be written: %1").arg(path));
}

void SignHubPage::shareWallet(const MultisigWallet& wallet, bool toFile)
{
    const QString code = MultisigModel::walletCode(wallet);
    if (toFile)
        saveCode(tr("Save the multisig information file"), QString("multisig-%1.aokms").arg(wallet.address.left(8)), code);
    else
        GUIUtil::setClipboard(code);
}

// ------------------------------------------------------------------ add a multisig wallet that someone else made

QWidget *SignHubPage::buildJoin()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);
    layout->addLayout(subHeader(tr("Add a multisig wallet I was invited to")));
    QHBoxLayout *columns = new QHBoxLayout();
    columns->setSpacing(20);
    QVBoxLayout *body = nullptr;
    QFrame *frame = card(page, &body, 8);
    body->addWidget(heading(tr("1. Multisig information"), frame));
    body->addWidget(caption(tr("Paste the information code that the maker of the multisig wallet sent you, or open the file. It starts with aokms-wallet1:"), frame));
    joinText = new QPlainTextEdit(frame);
    joinText->setFont(GUIUtil::monoFont(12));
    joinText->setPlaceholderText("aokms-wallet1:...");
    body->addWidget(joinText, 1);
    QHBoxLayout *row = new QHBoxLayout();
    QPushButton *paste = button(tr("Paste"), false, frame);
    QPushButton *open = button(tr("Open a file..."), false, frame);
    connect(paste, &QPushButton::clicked, this, [this] { joinText->setPlainText(QApplication::clipboard()->text()); });
    connect(open, &QPushButton::clicked, this, [this] {
        const QString path = GUIUtil::getOpenFileName(this, tr("Open a multisig information file"), QString(), tr("AokChain multisig file (*.aokms);;All files (*)"), nullptr);
        QFile file(path);
        if (!path.isEmpty() && file.open(QIODevice::ReadOnly) && file.size() < 1000000)
            joinText->setPlainText(QString::fromUtf8(file.readAll()));
    });
    row->addWidget(paste);
    row->addWidget(open);
    row->addStretch();
    body->addLayout(row);
    columns->addWidget(frame, 1);

    QVBoxLayout *right = nullptr;
    QFrame *preview = card(page, &right, 8);
    right->addWidget(heading(tr("2. Check and add"), preview));
    QWidget *inner = new QWidget();
    QVBoxLayout *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(0, 0, 6, 0);
    joinPreview = caption(QString(), inner);
    innerLayout->addWidget(joinPreview);
    innerLayout->addStretch(1);
    right->addWidget(scrollOf(inner), 1);
    right->addWidget(fieldLabel(tr("Name in this wallet"), preview));
    joinName = new QLineEdit(preview);
    joinName->setFixedHeight(40);
    joinName->setMaxLength(40);
    right->addWidget(joinName);
    joinRescan = new QCheckBox(tr("Look for earlier transactions of this address (rescan; can take a while)"), preview);
    joinRescan->setToolTip(tr("Needed when coins were already paid to the multisig address. Without it the balance shows only what arrives from now on."));
    joinRescan->setChecked(true);
    right->addWidget(joinRescan);
    QHBoxLayout *addRow = new QHBoxLayout();
    addRow->addStretch();
    joinButton = button(tr("Add the multisig wallet"), true, preview, 44);
    joinButton->setMinimumWidth(220);
    joinButton->setEnabled(false);
    connect(joinButton, SIGNAL(clicked()), this, SLOT(finishJoin()));
    addRow->addWidget(joinButton);
    right->addLayout(addRow);
    columns->addWidget(preview, 1);
    layout->addLayout(columns, 1);
    connect(joinText, SIGNAL(textChanged()), this, SLOT(joinTextChanged()));
    return page;
}

void SignHubPage::showJoin()
{
    joinText->clear();
    pendingRequestCode.clear();
    stack->setCurrentIndex(PageJoin);
    joinTextChanged();
}

void SignHubPage::joinTextChanged()
{
    QVBoxLayout *layout = static_cast<QVBoxLayout*>(joinPreview->parentWidget()->layout());
    // drop the description of the last one (everything after the first label)
    while (layout->count() > 1) {
        QLayoutItem *item = layout->takeAt(1);
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
    joinButton->setEnabled(false);
    joinWallet = MultisigWallet();
    QString error;
    const QString text = joinText->toPlainText().trimmed();
    if (text.isEmpty() || !model) {
        joinPreview->setText(tr("The rule, the participants and the address appear here."));
        layout->addStretch(1);
        return;
    }
    if (!model->parseWalletCode(text, joinWallet, error)) {
        joinPreview->setText(error);
        layout->addStretch(1);
        return;
    }
    MultisigWallet existing;
    if (model->wallet(joinWallet.address, existing)) {
        joinPreview->setText(tr("This multisig wallet is in your wallet already: \"%1\".").arg(existing.name));
        layout->addStretch(1);
        return;
    }
    joinPreview->setText(tr("Compare this with what the maker of the multisig wallet told you."));
    describeWallet(layout, joinWallet, joinPreview->parentWidget());
    layout->addWidget(joinWallet.hasMyKey()
        ? noteBox(tr("One of the public keys belongs to this wallet: you are a signer."), "notice", joinPreview->parentWidget())
        : noteBox(tr("None of the public keys belongs to this wallet. You can add it to watch the balance and the requests, but this wallet cannot sign for it. If you expected to be a signer, you sent the public key from another wallet: open that wallet instead."), "warning", joinPreview->parentWidget()));
    layout->addStretch(1);
    if (joinName->text().trimmed().isEmpty() || !joinName->isModified())
        joinName->setText(joinWallet.name);
    joinButton->setEnabled(true);
}

void SignHubPage::finishJoin()
{
    if (joinWallet.address.isEmpty())
        return;
    if (!joinName->text().trimmed().isEmpty())
        joinWallet.name = joinName->text().trimmed();
    setEnabled(false);
    model->addWallet(joinWallet, joinRescan->isChecked());
}

// ------------------------------------------------------------------ send from a multisig wallet

QWidget *SignHubPage::buildWallet()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    QWidget *right = new QWidget(page);
    QHBoxLayout *rightLayout = new QHBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    QLabel *balanceCaption = caption(tr("Multisig balance"), right);
    balanceCaption->setWordWrap(false);
    rightLayout->addWidget(balanceCaption);
    sendBalance = new AmountLabel(15, QFont::DemiBold, right);
    rightLayout->addWidget(sendBalance);
    QHBoxLayout *header = static_cast<QHBoxLayout*>(subHeader(QString(), &sendTitle, right));
    sendRule = new QLabel(page);
    sendRule->setProperty("role", "tag");
    header->insertWidget(2, sendRule);
    layout->addLayout(header);

    QHBoxLayout *columns = new QHBoxLayout();
    columns->setSpacing(20);
    QVBoxLayout *body = nullptr;
    QFrame *form = card(page, &body, 6);
    body->addWidget(heading(tr("New send request"), form));
    body->addWidget(fieldLabel(tr("Recipient address"), form));
    QHBoxLayout *toRow = new QHBoxLayout();
    toRow->setSpacing(8);
    sendTo = new QLineEdit(form);
    sendTo->setProperty("look", "field");
    sendTo->setFont(GUIUtil::monoFont(13));
    sendTo->setFixedHeight(44);
    sendTo->setPlaceholderText(tr("AokChain address"));
    toRow->addWidget(sendTo, 1);
    const std::pair<const char*, QString> squares[] = {{"addressbook", tr("Choose from the address book")}, {"paste", tr("Paste the address")}};
    for (const std::pair<const char*, QString>& square : squares) {
        QPushButton *b = new QPushButton(form);
        b->setIcon(QIcon(GUIUtil::lineIcon(square.first, 18, GUIUtil::theme().label, 1.8)));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(44, 44);
        b->setStyleSheet("padding: 0;");
        b->setToolTip(square.second);
        b->setCursor(Qt::PointingHandCursor);
        toRow->addWidget(b);
        if (QString(square.first) == "paste") {
            connect(b, &QPushButton::clicked, this, [this] { sendTo->setText(QApplication::clipboard()->text().trimmed()); });
        } else {
            connect(b, &QPushButton::clicked, this, [this] {
                if (!walletModel || !walletModel->getAddressTableModel())
                    return;
                AddressBookPage dialog(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
                dialog.setModel(walletModel->getAddressTableModel());
                if (dialog.exec())
                    sendTo->setText(dialog.getReturnValue());
            });
        }
    }
    body->addLayout(toRow);
    body->addSpacing(4);
    body->addWidget(fieldLabel(tr("Amount"), form));
    QHBoxLayout *amountRow = new QHBoxLayout();
    amountRow->setSpacing(8);
    sendAmount = new AokChainAmountField(form);
    sendAmount->setMinimumHeight(44);
    for (QAbstractSpinBox *spin : sendAmount->findChildren<QAbstractSpinBox*>()) {
        spin->setMaximumWidth(QWIDGETSIZE_MAX);
        spin->setMinimumWidth(260);
        spin->setMinimumHeight(44);
        spin->setFont(GUIUtil::monoFont(15, QFont::Medium));
    }
    amountRow->addWidget(sendAmount, 1);
    sendAllButton = new QPushButton(tr("All"), form);
    sendAllButton->setCheckable(true);
    sendAllButton->setProperty("look", "soft");
    sendAllButton->setFixedHeight(40);
    sendAllButton->setToolTip(tr("Send everything that is at the multisig address; the fee is taken from it"));
    amountRow->addWidget(sendAllButton);
    body->addLayout(amountRow);
    body->addSpacing(4);
    body->addWidget(fieldLabel(tr("Memo for the other participants (optional)"), form));
    sendMemo = new QLineEdit(form);
    sendMemo->setFixedHeight(40);
    sendMemo->setMaxLength(120);
    sendMemo->setPlaceholderText(tr("For example: server cost of November. Travels with the request, is not written to the block chain."));
    body->addWidget(sendMemo);
    body->addSpacing(4);
    body->addWidget(fieldLabel(tr("Fee"), form));
    QHBoxLayout *feeRow = new QHBoxLayout();
    feeRow->setSpacing(10);
    sendFee = new QButtonGroup(this);
    const QString feeNames[2] = {tr("Normal"), tr("Faster") + "\n" + tr("twice the fee")};
    for (int i = 0; i < 2; ++i) {
        ChoiceButton *choice = new ChoiceButton(feeNames[i], ChoiceButton::Box, form);
        choice->setFixedHeight(46);
        sendFee->addButton(choice, i + 1);
        feeRow->addWidget(choice, 1);
    }
    sendFee->button(1)->setChecked(true);
    feeRow->addStretch(2);
    body->addLayout(feeRow);
    sendFeeLine = caption(QString(), form);
    body->addWidget(sendFeeLine);
    sendError = noteBox(QString(), "error", form);
    sendError->hide();
    body->addWidget(sendError);
    body->addStretch(1);
    body->addWidget(caption(tr("Only AOK can be sent from a multisig address for now. Sending tokens from multisig is in preparation."), form));
    sendButton = button(tr("Make the send request"), true, form, 46);
    connect(sendButton, SIGNAL(clicked()), this, SLOT(makeRequest()));
    body->addWidget(sendButton);
    columns->addWidget(form, 3);

    QVBoxLayout *side = nullptr;
    QFrame *aside = card(page, &side, 8);
    QWidget *inner = new QWidget();
    sendSide = new QVBoxLayout(inner);
    sendSide->setContentsMargins(0, 0, 6, 0);
    sendSide->setSpacing(8);
    side->addWidget(scrollOf(inner), 1);
    columns->addWidget(aside, 2);
    layout->addLayout(columns, 1);

    previewTimer = new QTimer(this);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(250);
    connect(previewTimer, SIGNAL(timeout()), this, SLOT(updateSendPreview()));
    connect(sendAmount, SIGNAL(valueChanged()), previewTimer, SLOT(start()));
    connect(sendFee, SIGNAL(buttonClicked(int)), previewTimer, SLOT(start()));
    connect(sendAllButton, &QPushButton::toggled, this, [this](bool on) {
        sendAmount->setEnabled(!on);
        updateSendPreview();
    });
    return page;
}

void SignHubPage::showWallet(const QString& address)
{
    if (!model)
        return;
    if (currentWallet != address) {
        sendTo->clear();
        sendAmount->clear();
        sendMemo->clear();
        sendAllButton->setChecked(false);
    }
    currentWallet = address;
    sendError->hide();
    fillWalletPage();
    stack->setCurrentIndex(PageWallet);
    updateSendPreview();
}

void SignHubPage::fillWalletPage()
{
    MultisigWallet wallet;
    if (!model || !model->wallet(currentWallet, wallet)) {
        showHub();
        return;
    }
    sendTitle->setText(tr("Send from %1").arg(wallet.name.isEmpty() ? GUIUtil::shortAddress(wallet.address) : wallet.name));
    sendRule->setText(ruleText(wallet.required, wallet.keys.size()));
    sendBalance->setAmount(amountText(wallet.balance), "AOK");

    clearLayout(sendSide);
    QWidget *parent = sendSide->parentWidget();
    // open requests of this wallet
    sendSide->addWidget(heading(tr("Requests in progress"), parent));
    int shown = 0;
    for (const MultisigRequest& request : model->requests(currentWallet)) {
        const MultisigRequestView view = model->analyze(request);
        const QString id = request.id;
        QFrame *box = new QFrame(parent);
        box->setProperty("role", "tile");
        QVBoxLayout *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(14, 10, 14, 10);
        boxLayout->setSpacing(4);
        QHBoxLayout *top = new QHBoxLayout();
        AmountLabel *amount = new AmountLabel(15, QFont::Medium, box);
        amount->setAmount(amountText(view.amount), "AOK");
        top->addWidget(amount);
        top->addStretch();
        if (!request.txid.isEmpty())
            top->addWidget(badge(tr("Sent"), "ok", box));
        else if (view.spent)
            top->addWidget(badge(tr("No longer usable"), "plain", box));
        else if (view.canSign)
            top->addWidget(badge(tr("My signature needed"), "pend", box));
        else if (view.complete)
            top->addWidget(badge(tr("Ready to send"), "ok", box));
        else
            top->addWidget(badge(tr("Signatures %1 / %2").arg(view.signatures).arg(view.required), "plain", box));
        boxLayout->addLayout(top);
        QString to;
        for (const MultisigRequestView::Output& output : view.outputs) {
            if (!output.change)
                to = output.address;
        }
        QLabel *line = caption(request.memo.isEmpty() ? GUIUtil::shortAddress(to) : QString("%1 · %2").arg(GUIUtil::shortAddress(to), request.memo), box);
        line->setToolTip(to);
        boxLayout->addWidget(line);
        QPushButton *open = linkButton(tr("Open"), box);
        connect(open, &QPushButton::clicked, this, [this, id] { showRequest(id); });
        boxLayout->addWidget(open, 0, Qt::AlignLeft);
        sendSide->addWidget(box);
        ++shown;
    }
    if (shown == 0)
        sendSide->addWidget(caption(tr("None. A request that you make or receive appears here until it is sent."), parent));
    QPushButton *openRequest = button(tr("Open a signing request"), false, parent, 38);
    connect(openRequest, SIGNAL(clicked()), this, SLOT(openReceivedRequest()));
    sendSide->addWidget(openRequest);

    // the wallet itself
    sendSide->addSpacing(8);
    sendSide->addWidget(rule(parent));
    sendSide->addWidget(heading(tr("This multisig wallet"), parent));
    QLabel *address = new QLabel(wallet.address, parent);
    address->setFont(GUIUtil::monoFont(12));
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    address->setWordWrap(true);
    sendSide->addWidget(address);
    QStringList names;
    for (const MultisigKey& key : wallet.keys)
        names << (key.mine ? tr("%1 (me)").arg(key.name) : key.name);
    sendSide->addWidget(caption(tr("Participants: %1").arg(names.join(", ")), parent));
    sendSide->addWidget(caption(tr("To receive, give this address to the payer like any other address."), parent));
    QHBoxLayout *actions = new QHBoxLayout();
    actions->setSpacing(14);
    QPushButton *copyAddress = linkButton(tr("Copy the address"), parent);
    QPushButton *copyCode = linkButton(tr("Copy the information code"), parent);
    QPushButton *saveFile = linkButton(tr("Save the information file"), parent);
    connect(copyAddress, &QPushButton::clicked, this, [wallet] { GUIUtil::setClipboard(wallet.address); });
    connect(copyCode, &QPushButton::clicked, this, [this, wallet] { shareWallet(wallet, false); });
    connect(saveFile, &QPushButton::clicked, this, [this, wallet] { shareWallet(wallet, true); });
    actions->addWidget(copyAddress);
    actions->addWidget(copyCode);
    actions->addWidget(saveFile);
    actions->addStretch();
    sendSide->addLayout(actions);
    QPushButton *remove = linkButton(tr("Remove from this list"), parent);
    connect(remove, &QPushButton::clicked, this, [this, wallet] {
        if (QMessageBox::question(this, tr("Remove the multisig wallet"),
                tr("Remove \"%1\" from the list of multisig wallets? Its open requests are dropped too. The coins stay at the address, and you can add the multisig wallet again with its information file.").arg(wallet.name),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
            model->removeWallet(wallet.address);
            showHub();
        }
    });
    sendSide->addWidget(remove, 0, Qt::AlignLeft);
    sendSide->addStretch(1);

    if (!wallet.hasMyKey()) {
        sendButton->setEnabled(true);
        sendButton->setToolTip(tr("This wallet only watches the multisig wallet. It can make a request, but cannot sign it."));
    }
}

void SignHubPage::updateSendPreview()
{
    if (!model || currentWallet.isEmpty() || stack->currentIndex() != PageWallet)
        return;
    const bool all = sendAllButton->isChecked();
    CAmount fee = 0, sent = 0;
    QString error;
    const CAmount amount = all ? 0 : sendAmount->value();
    if (!all && amount <= 0) {
        sendFeeLine->setText(tr("The fee is worked out from the size of the transaction with all its signatures."));
        return;
    }
    MultisigWallet wallet;
    model->wallet(currentWallet, wallet);
    if (!model->previewSpend(currentWallet, amount, all, sendFee->checkedId(), fee, sent, error)) {
        sendFeeLine->setText(error);
        return;
    }
    if (all)
        sendAmount->setValue(sent);
    sendFeeLine->setText(tr("Fee (estimate) %1 AOK · multisig balance after sending %2 AOK")
                             .arg(amountText(fee), amountText(wallet.balance - sent - fee)));
}

void SignHubPage::makeRequest()
{
    if (!model)
        return;
    sendError->hide();
    QString id, error;
    const bool all = sendAllButton->isChecked();
    if (!walletModel->validateAddress(sendTo->text().trimmed())) {
        sendError->setText(tr("The recipient address is not valid."));
        sendError->show();
        return;
    }
    {
        // the same question as before any other send
        MultisigWallet wallet;
        model->wallet(currentWallet, wallet);
        CAmount fee = 0, sent = 0;
        if (!model->previewSpend(currentWallet, all ? 0 : sendAmount->value(), all, sendFee->checkedId(), fee, sent, error)) {
            sendError->setText(error);
            sendError->show();
            return;
        }
        SendConfirmData confirm;
        confirm.asset = "AOK";
        confirm.feeUnit = "AOK";
        confirm.source = tr("From the multisig wallet \"%1\" (%2)").arg(wallet.name, wallet.rule());
        SendConfirmData::Recipient to;
        to.address = sendTo->text().trimmed();
        to.label = walletModel->getAddressTableModel()->labelForAddress(to.address);
        to.amount = amountText(sent);
        to.known = walletModel->isKnownAddress(to.address);
        confirm.recipients.append(to);
        confirm.total = amountText(sent);
        confirm.fee = amountText(fee);
        confirm.feeNote = tr("estimate");
        confirm.assetDeducted = amountText(sent + fee);
        confirm.assetAfter = amountText(std::max<CAmount>(0, wallet.balance - sent - fee));
        confirm.wholeBalance = all;
        confirm.sendText = tr("Make the request");
        confirm.extraWarnings << tr("Nothing is sent yet: the request has to be signed by %1 participants and is sent after that.").arg(wallet.required);
        if (!SendConfirmDialog::confirm(confirm, this))
            return;
    }
    if (!model->createRequest(currentWallet, sendTo->text(), all ? 0 : sendAmount->value(), all, sendMemo->text(), sendFee->checkedId(), id, error)) {
        sendError->setText(error);
        sendError->show();
        return;
    }
    // the maker signs first, if this wallet has a key
    MultisigRequest request;
    if (model->request(id, request) && model->analyze(request).canSign) {
        WalletModel::UnlockContext context(walletModel->requestUnlock());
        if (context.isValid()) {
            QString signError;
            if (!model->signRequest(id, signError))
                QMessageBox::warning(this, tr("Send request"), signError);
        }
    }
    sendTo->clear();
    sendAmount->clear();
    sendMemo->clear();
    sendAllButton->setChecked(false);
    showRequest(id);
}

// ------------------------------------------------------------------ a request

QWidget *SignHubPage::buildRequest()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget *inner = new QWidget();
    requestBody = new QVBoxLayout(inner);
    requestBody->setContentsMargins(0, 0, 0, 0);
    requestBody->setSpacing(16);
    layout->addWidget(scrollOf(inner));
    return page;
}

void SignHubPage::showRequest(const QString& id)
{
    currentRequest = id;
    fillRequestPage();
    stack->setCurrentIndex(PageRequest);
}

void SignHubPage::fillRequestPage()
{
    clearLayout(requestBody);
    QWidget *parent = requestBody->parentWidget();
    MultisigRequest request;
    if (!model || !model->request(currentRequest, request)) {
        showHub();
        return;
    }
    const MultisigRequestView view = model->analyze(request);
    MultisigWallet wallet;
    const bool known = model->wallet(request.address, wallet);
    const bool sent = !request.txid.isEmpty();
    const GUIUtil::Theme& t = GUIUtil::theme();

    // header
    QWidget *right = new QWidget(parent);
    QHBoxLayout *rightLayout = new QHBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    QLabel *balanceCaption = caption(tr("Multisig balance"), right);
    balanceCaption->setWordWrap(false);
    rightLayout->addWidget(balanceCaption);
    AmountLabel *balance = new AmountLabel(15, QFont::DemiBold, right);
    balance->setAmount(amountText(wallet.balance), "AOK");
    rightLayout->addWidget(balance);
    QHBoxLayout *header = static_cast<QHBoxLayout*>(subHeader(tr("Send from %1").arg(known ? wallet.name : request.walletName), nullptr, right));
    QLabel *chip = new QLabel(ruleText(view.required, known ? wallet.keys.size() : view.signedBy.size()), parent);
    chip->setProperty("role", "tag");
    header->insertWidget(2, chip);
    requestBody->addLayout(header);

    // progress: request made -> signatures k / M -> send
    const int missing = qMax(0, view.required - view.signatures);
    QVBoxLayout *stripBody = nullptr;
    QFrame *strip = card(parent, &stripBody, 0);
    stripBody->setContentsMargins(22, 12, 22, 12);
    QHBoxLayout *progress = new QHBoxLayout();
    progress->setSpacing(12);
    struct Stage { QString mark, text; int state; };   // 0 to do, 1 now, 2 done
    QList<Stage> stages;
    stages << Stage{QString::fromUtf8("✓"), tr("Request made"), 2};
    stages << Stage{view.signatures > 0 ? QString::fromUtf8("✓") : "1", tr("Signatures %1 / %2").arg(view.signatures).arg(view.required), view.signatures > 0 ? 2 : 1};
    if (missing > 0 && view.signatures > 0)
        stages << Stage{"2", missing == 1 ? tr("One more signature needed") : tr("%1 more signatures needed").arg(missing), 1};
    stages << Stage{sent ? QString::fromUtf8("✓") : "3", sent ? tr("Sent") : tr("Broadcast"), sent ? 2 : view.complete ? 1 : 0};
    for (int i = 0; i < stages.size(); ++i) {
        const Stage& stage = stages[i];
        QLabel *dot = new QLabel(stage.mark, strip);
        dot->setFixedSize(26, 26);
        dot->setAlignment(Qt::AlignCenter);
        dot->setStyleSheet(QString("QLabel { background-color: %1; color: %2; border-radius: 13px; font-size: 12px; font-weight: 700; }")
                               .arg((stage.state == 2 ? t.okSoft : stage.state == 1 ? t.fill : t.chip).name(),
                                    (stage.state == 2 ? t.ok : stage.state == 1 ? t.onFill : t.chipText).name()));
        progress->addWidget(dot);
        QLabel *text = new QLabel(stage.text, strip);
        text->setFont(GUIUtil::appFont(13, stage.state == 1 ? QFont::DemiBold : QFont::Normal));
        if (stage.state == 0)
            text->setProperty("role", "secondary");
        progress->addWidget(text);
        if (i + 1 < stages.size()) {
            QFrame *dash = new QFrame(strip);
            dash->setFixedHeight(1);
            dash->setMinimumWidth(24);
            dash->setStyleSheet(QString("background-color: %1;").arg(t.borderStrong.name()));
            progress->addWidget(dash, 1);
        }
    }
    stripBody->addLayout(progress);
    requestBody->addWidget(strip);

    QHBoxLayout *columns = new QHBoxLayout();
    columns->setSpacing(20);

    // --- what the request does, read from its transaction
    QVBoxLayout *body = nullptr;
    QFrame *what = card(parent, &body, 8);
    body->addWidget(heading(tr("Send request"), what));
    AmountLabel *amount = new AmountLabel(30, QFont::Medium, what);
    amount->setAmount(amountText(view.amount), "AOK");
    body->addWidget(amount);
    auto row = [&](const QString& name, QWidget *value) {
        QHBoxLayout *line = new QHBoxLayout();
        QLabel *left = caption(name, what);
        left->setWordWrap(false);
        line->addWidget(left);
        line->addStretch();
        line->addWidget(value);
        body->addLayout(line);
        body->addWidget(rule(what));
    };
    for (const MultisigRequestView::Output& output : view.outputs) {
        if (output.change)
            continue;
        QLabel *to = new QLabel(output.address.isEmpty() ? tr("(not an address)") : output.address, what);
        to->setFont(GUIUtil::monoFont(13, QFont::Medium));
        to->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row(tr("Recipient address"), to);
        if (view.outputs.size() > 2 || view.amount != output.amount) {
            AmountLabel *part = new AmountLabel(13, QFont::Medium, what);
            part->setAmount(amountText(output.amount), "AOK");
            row(tr("Amount to this address"), part);
        }
    }
    if (!request.memo.isEmpty()) {
        QLabel *memo = new QLabel(request.memo, what);
        memo->setToolTip(tr("The memo comes with the request. It is not part of the transaction."));
        row(tr("Memo"), memo);
    }
    if (view.inputsFound) {
        AmountLabel *fee = new AmountLabel(13, QFont::Medium, what);
        fee->setAmount(amountText(view.fee), "AOK");
        row(tr("Fee"), fee);
        AmountLabel *change = new AmountLabel(13, QFont::Medium, what);
        change->setAmount(amountText(view.change), "AOK");
        row(tr("Back to the multisig address (change)"), change);
        if (known && !sent) {
            AmountLabel *after = new AmountLabel(13, QFont::Medium, what);
            after->setAmount(amountText(wallet.balance - view.amount - view.fee), "AOK");
            row(tr("Multisig balance after sending"), after);
        }
    }
    QLabel *source = caption(tr("These figures are read from the transaction itself and from the block chain of this wallet, not from what the request says about itself."), what);
    body->addWidget(source);
    for (const QString& problem : sent ? QStringList() : view.problems)
        body->addWidget(noteBox(problem, "error", what));
    for (const QString& warning : sent ? QStringList() : view.warnings)
        body->addWidget(noteBox(QString::fromUtf8("⚠ ") + warning, "warning", what));

    // what to do next, in one sentence
    QString nextTitle, nextText;
    if (sent) {
        nextTitle = tr("Done: the transaction was sent");
        nextText = tr("It is confirmed with the next blocks. The other participants need not do anything.");
    } else if (view.spent) {
        nextTitle = tr("Nothing to do: this request can no longer be used");
        nextText = tr("Its coins were spent, most likely because another participant sent it already. You can remove it.");
    } else if (!view.problems.isEmpty()) {
        nextTitle = known ? tr("Do not sign: something is wrong with this request") : tr("Next: add the multisig wallet of this request");
        nextText = known ? tr("Read the red notes above and ask the person who sent the request.") : tr("The request is for a multisig wallet that is not in your wallet yet.");
    } else if (view.canSign) {
        nextTitle = tr("Next: check the request and sign");
        nextText = tr("Compare the recipient and the amount with what you agreed on. If they are right, press \"Sign\".");
    } else if (view.complete) {
        nextTitle = tr("Next: send it");
        nextText = tr("All the signatures that are needed are here. Press \"Send\" to hand the transaction to the network.");
    } else {
        nextTitle = tr("Next: send the signing request to another participant");
        nextText = tr("Take the signing request out as a file or a code with the buttons below and send it by messenger or mail. The receiver puts it into their wallet at <b>Sign → Open a signing request</b> and presses \"Sign\".");
    }
    QFrame *next = new QFrame(what);
    next->setObjectName("nextBox");
    const bool alarm = !sent && !view.spent && !view.problems.isEmpty() && known;
    next->setStyleSheet(QString("QFrame#nextBox { background-color: %1; border-radius: 12px; }").arg((alarm ? t.errorSoft : t.accentSoft).name()));
    QVBoxLayout *nextLayout = new QVBoxLayout(next);
    nextLayout->setContentsMargins(16, 12, 16, 12);
    nextLayout->setSpacing(4);
    QLabel *nextHead = new QLabel(nextTitle, next);
    nextHead->setFont(GUIUtil::appFont(14, QFont::DemiBold));
    nextHead->setWordWrap(true);
    nextLayout->addWidget(nextHead);
    QLabel *nextBody = caption(nextText, next);
    nextBody->setTextFormat(Qt::RichText);
    nextLayout->addWidget(nextBody);
    body->addSpacing(4);
    body->addWidget(next);
    if (sent) {
        QLabel *txid = new QLabel(request.txid, what);
        txid->setFont(GUIUtil::monoFont(12));
        txid->setTextInteractionFlags(Qt::TextSelectableByMouse);
        txid->setWordWrap(true);
        body->addWidget(fieldLabel(tr("Transaction ID"), what));
        body->addWidget(txid);
        QPushButton *explore = button(tr("View in the explorer"), false, what);
        const QString id = request.txid;
        connect(explore, &QPushButton::clicked, this, [this, id] { Q_EMIT explorerRequested(id); });
        body->addWidget(explore, 0, Qt::AlignLeft);
    } else if (!view.spent && view.decoded) {
        QHBoxLayout *exports = new QHBoxLayout();
        exports->setSpacing(8);
        QPushButton *file = button(tr("Save the signing request file"), false, what);
        QPushButton *code = button(tr("Copy the request code"), false, what);
        QPushButton *qr = button(tr("Show as QR"), false, what);
        connect(file, SIGNAL(clicked()), this, SLOT(exportRequestFile()));
        connect(code, &QPushButton::clicked, this, [this, code] {
            copyRequestCode();
            code->setText(tr("Copied"));
        });
        connect(qr, SIGNAL(clicked()), this, SLOT(showRequestQr()));
        const int length = MultisigModel::requestCode(request).size();
#ifdef USE_QRCODE
        if (length > MultisigModel::QR_MAX_CHARS) {
            qr->setEnabled(false);
            qr->setToolTip(tr("This request is too large for one QR code (%1 characters; a QR code is offered up to %2). Use the file or the code.").arg(length).arg(MultisigModel::QR_MAX_CHARS));
        }
#else
        qr->setEnabled(false);
        qr->setToolTip(tr("This build has no QR code support."));
#endif
        exports->addWidget(file);
        exports->addWidget(code);
        exports->addWidget(qr);
        exports->addStretch();
        body->addLayout(exports);
    }
    body->addStretch(1);
    columns->addWidget(what, 3);

    // --- who signed
    QVBoxLayout *side = nullptr;
    QFrame *signers = card(parent, &side, 6);
    side->addWidget(heading(tr("Signatures"), signers));
    for (int i = 0; i < view.signedBy.size(); ++i) {
        const bool mine = known && i < wallet.keys.size() && wallet.keys[i].mine;
        const QString name = known && i < wallet.keys.size() ? wallet.keys[i].name : tr("Participant %1").arg(i + 1);
        QHBoxLayout *line = new QHBoxLayout();
        line->setContentsMargins(0, 8, 0, 8);
        line->setSpacing(12);
        line->addWidget(avatar(mine ? tr("Me") : QString::number(i + 1), 36, mine, signers));
        QVBoxLayout *text = new QVBoxLayout();
        text->setSpacing(1);
        QLabel *who = new QLabel(mine ? tr("%1 (this wallet)").arg(name) : name, signers);
        who->setFont(GUIUtil::appFont(14, QFont::Medium));
        text->addWidget(who);
        text->addWidget(caption(view.signedBy[i] ? tr("Signed") : tr("Waiting for the signature"), signers));
        line->addLayout(text, 1);
        line->addWidget(badge(view.signedBy[i] ? tr("Signed") : tr("Waiting"), view.signedBy[i] ? "ok" : "plain", signers));
        side->addLayout(line);
        side->addWidget(rule(signers));
    }
    if (!sent && !view.spent) {
        side->addWidget(caption(view.complete ? tr("Enough signatures are here.")
                                : missing == 1 ? tr("One more participant has to sign.")
                                : tr("%1 more participants have to sign.").arg(missing), signers));
        side->addWidget(caption(tr("Who signed is read from the signatures in the transaction."), signers));
    }
    side->addStretch(1);
    if (!sent && !view.spent) {
        if (!known) {
            QPushButton *add = button(tr("Add this multisig wallet..."), true, signers, 44);
            const MultisigRequest copy = request;
            connect(add, &QPushButton::clicked, this, [this, copy] { addWalletFromRequest(copy); });
            side->addWidget(add);
        }
        if (view.canSign || (known && wallet.hasMyKey() && !view.mineSigned && !view.complete)) {
            QPushButton *sign = button(tr("Sign"), true, signers, 46);
            sign->setEnabled(view.canSign);
            if (!view.canSign)
                sign->setToolTip(tr("Signing is not offered because something is wrong with this request. See the red notes."));
            connect(sign, SIGNAL(clicked()), this, SLOT(signCurrentRequest()));
            side->addWidget(sign);
        }
        QPushButton *merge = button(tr("Load signatures I received (file · paste)"), false, signers);
        connect(merge, SIGNAL(clicked()), this, SLOT(mergeSignatures()));
        side->addWidget(merge);
        QPushButton *send = button(view.complete ? tr("Broadcast") : tr("Can be sent when the signatures are here"), view.complete && !view.canSign, signers, 46);
        send->setEnabled(view.complete && view.problems.isEmpty());
        connect(send, SIGNAL(clicked()), this, SLOT(broadcastCurrentRequest()));
        side->addWidget(send);
    }
    QPushButton *cancel = linkButton(sent || view.spent ? tr("Remove from the list") : tr("Cancel this request"), signers);
    connect(cancel, SIGNAL(clicked()), this, SLOT(cancelCurrentRequest()));
    side->addWidget(cancel, 0, Qt::AlignHCenter);
    columns->addWidget(signers, 2);
    requestBody->addLayout(columns, 1);
}

void SignHubPage::exportRequestFile()
{
    MultisigRequest request;
    if (model && model->request(currentRequest, request))
        saveCode(tr("Save the signing request file"), QString("sign-request-%1.aokms").arg(request.id.left(8)), MultisigModel::requestCode(request));
}

void SignHubPage::copyRequestCode()
{
    MultisigRequest request;
    if (model && model->request(currentRequest, request))
        GUIUtil::setClipboard(MultisigModel::requestCode(request));
}

void SignHubPage::showRequestQr()
{
    MultisigRequest request;
    if (!model || !model->request(currentRequest, request))
        return;
    const QImage image = ReceiveRequestDialog::qrCodeImage(MultisigModel::requestCode(request), 560, MultisigModel::QR_MAX_CHARS);
    if (image.isNull()) {
        QMessageBox::information(this, tr("Signing request"), tr("The QR code could not be made. Use the file or the code."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Signing request as QR code"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *picture = new QLabel(&dialog);
    picture->setPixmap(QPixmap::fromImage(image));
    picture->setStyleSheet("background: white; padding: 8px;");
    layout->addWidget(picture, 0, Qt::AlignCenter);
    layout->addWidget(caption(tr("The other participant scans this and pastes the text into their wallet at Sign → Open a signing request."), &dialog));
    QPushButton *close = button(tr("Close"), true, &dialog);
    connect(close, SIGNAL(clicked()), &dialog, SLOT(accept()));
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
}

bool SignHubPage::askCode(const QString& title, const QString& hint, QString& text)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(620, 420);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *head = heading(title, &dialog);
    layout->addWidget(head);
    layout->addWidget(caption(hint, &dialog));
    QPlainTextEdit *edit = new QPlainTextEdit(&dialog);
    edit->setFont(GUIUtil::monoFont(12));
    edit->setPlaceholderText("aokms-req1:...");
    layout->addWidget(edit, 1);
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *paste = button(tr("Paste"), false, &dialog);
    QPushButton *open = button(tr("Open a file..."), false, &dialog);
    QPushButton *cancel = button(tr("Cancel"), false, &dialog);
    QPushButton *ok = button(tr("Open"), true, &dialog);
    ok->setMinimumWidth(110);
    buttons->addWidget(paste);
    buttons->addWidget(open);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
    connect(paste, &QPushButton::clicked, &dialog, [edit] { edit->setPlainText(QApplication::clipboard()->text()); });
    connect(open, &QPushButton::clicked, &dialog, [this, edit] {
        const QString path = GUIUtil::getOpenFileName(this, tr("Open a signing request file"), QString(), tr("AokChain multisig file (*.aokms);;All files (*)"), nullptr);
        QFile file(path);
        if (!path.isEmpty() && file.open(QIODevice::ReadOnly) && file.size() < 4000000)
            edit->setPlainText(QString::fromUtf8(file.readAll()));
    });
    connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    connect(ok, SIGNAL(clicked()), &dialog, SLOT(accept()));
    if (dialog.exec() != QDialog::Accepted)
        return false;
    text = edit->toPlainText().trimmed();
    return !text.isEmpty();
}

void SignHubPage::openReceivedRequest()
{
    if (!model)
        return;
    QString text;
    if (!askCode(tr("Open a signing request"),
                 tr("Paste the signing request that another participant sent you, or open its file. Nothing is signed yet: the wallet first shows what the request does."), text))
        return;
    MultisigRequest request;
    QString id, error;
    if (!model->parseRequestCode(text, request, error)) {
        QMessageBox::warning(this, tr("Open a signing request"), error);
        return;
    }
    MultisigWallet wallet;
    MultisigRequest probe = request;
    const MultisigRequestView view = model->analyze(probe);
    if (!view.walletKnown) {
        addWalletFromRequest(request);
        return;
    }
    if (!model->importRequest(request, id, error)) {
        QMessageBox::warning(this, tr("Open a signing request"), error);
        return;
    }
    showRequest(id);
}

void SignHubPage::addWalletFromRequest(const MultisigRequest& request)
{
    MultisigWallet wallet;
    QString error;
    if (!model->walletFromRedeemScript(request.redeemScript, wallet, error)) {
        QMessageBox::warning(this, tr("Open a signing request"), error);
        return;
    }
    wallet.name = request.walletName.isEmpty() ? tr("Multisig") : request.walletName;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add the multisig wallet of this request"));
    dialog.resize(600, 520);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);
    layout->addWidget(noteBox(tr("This request is for a multisig wallet that is not in your wallet yet. Check that it is the multisig wallet you agreed to take part in: compare the address and the public keys with what the maker sent you. If you have its multisig information file, add it with that file instead, to get the names of the participants."), "warning", &dialog));
    describeWallet(layout, wallet, &dialog);
    layout->addWidget(wallet.hasMyKey()
        ? noteBox(tr("One of the public keys belongs to this wallet: you are a signer."), "notice", &dialog)
        : noteBox(tr("None of the public keys belongs to this wallet: it cannot sign this request."), "warning", &dialog));
    QCheckBox *rescan = new QCheckBox(tr("Look for earlier transactions of this address (rescan; can take a while)"), &dialog);
    rescan->setChecked(true);
    layout->addWidget(rescan);
    layout->addStretch(1);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = button(tr("Cancel"), false, &dialog);
    QPushButton *add = button(tr("Add and open the request"), true, &dialog);
    buttons->addWidget(cancel);
    buttons->addWidget(add);
    layout->addLayout(buttons);
    connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    connect(add, SIGNAL(clicked()), &dialog, SLOT(accept()));
    if (dialog.exec() != QDialog::Accepted)
        return;
    pendingRequestCode = MultisigModel::requestCode(request);
    setEnabled(false);
    model->addWallet(wallet, rescan->isChecked());
}

void SignHubPage::mergeSignatures()
{
    if (!model)
        return;
    QString text;
    if (!askCode(tr("Load signatures I received"),
                 tr("Paste the signing request that came back from another participant, or open its file. Its signatures are added to this request."), text))
        return;
    MultisigRequest received;
    QString id, error;
    if (!model->parseRequestCode(text, received, error)) {
        QMessageBox::warning(this, tr("Load signatures I received"), error);
        return;
    }
    if (received.id != currentRequest) {
        QMessageBox::warning(this, tr("Load signatures I received"),
                             tr("This is another request, not the one that is open. (It is for another transaction: a different recipient, amount or coins.) Open it with \"Open a signing request\" to see what it does."));
        return;
    }
    if (!model->importRequest(received, id, error)) {
        QMessageBox::warning(this, tr("Load signatures I received"), error);
        return;
    }
    fillRequestPage();
}

void SignHubPage::signCurrentRequest()
{
    MultisigRequest request;
    if (!model || !model->request(currentRequest, request))
        return;
    const MultisigRequestView view = model->analyze(request);
    if (!view.canSign)
        return;
    QStringList lines;
    for (const MultisigRequestView::Output& output : view.outputs) {
        if (!output.change)
            lines << tr("%1 AOK to %2").arg(amountText(output.amount), output.address);
    }
    QString text = tr("You are about to sign this transaction:") + "<br><br><b>" + lines.join("<br>") + "</b><br><br>"
                   + tr("Fee %1 AOK").arg(amountText(view.fee));
    if (!view.warnings.isEmpty())
        text += "<br><br><span style=\"color:" + GUIUtil::theme().error.name() + "\">" + tr("There are warnings for this request. Sign only if you have read them and they are in order.") + "</span>";
    text += "<br><br>" + tr("A signature cannot be taken back once you have passed the request on.");
    QMessageBox box(QMessageBox::Question, tr("Sign the request"), text, QMessageBox::Cancel, this);
    QPushButton *sign = box.addButton(tr("Sign"), QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != sign)
        return;
    WalletModel::UnlockContext context(walletModel->requestUnlock());
    if (!context.isValid())
        return;
    QString error;
    if (!model->signRequest(currentRequest, error))
        QMessageBox::warning(this, tr("Sign the request"), error);
    fillRequestPage();
}

void SignHubPage::broadcastCurrentRequest()
{
    MultisigRequest request;
    if (!model || !model->request(currentRequest, request))
        return;
    const MultisigRequestView view = model->analyze(request);
    QStringList lines;
    for (const MultisigRequestView::Output& output : view.outputs) {
        if (!output.change)
            lines << tr("%1 AOK to %2").arg(amountText(output.amount), output.address);
    }
    QMessageBox box(QMessageBox::Question, tr("Send the transaction"),
                    tr("Send this transaction to the network now?") + "<br><br><b>" + lines.join("<br>") + "</b><br><br>"
                        + tr("Fee %1 AOK").arg(amountText(view.fee)) + "<br><br>" + tr("A sent transaction cannot be cancelled."),
                    QMessageBox::Cancel, this);
    QPushButton *send = box.addButton(tr("Broadcast"), QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != send)
        return;
    QString txid, error;
    if (!model->broadcastRequest(currentRequest, txid, error))
        QMessageBox::critical(this, tr("Send the transaction"), error);
    fillRequestPage();
}

void SignHubPage::cancelCurrentRequest()
{
    MultisigRequest request;
    if (!model || !model->request(currentRequest, request))
        return;
    const bool finished = !request.txid.isEmpty() || model->analyze(request).spent;
    if (!finished && QMessageBox::question(this, tr("Cancel the request"),
            tr("Remove this request from your wallet? If you already passed it on with your signature, the others can still complete and send it: to make sure it cannot be sent, the coins have to be spent by another transaction."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const QString address = request.address;
    model->removeRequest(currentRequest);
    showWallet(address);
}
