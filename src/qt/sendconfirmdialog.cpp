// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendconfirmdialog.h"

#include "firstrun.h"
#include "guiutil.h"
#include "pagewidgets.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

/** Seconds before the send button can be pressed */
static const int CONFIRM_DELAY = 2;

QString SendConfirmDialog::groupedAddress(const QString &address)
{
    QStringList groups;
    for (int i = 0; i < address.size(); i += 4)
        groups << address.mid(i, 4);
    return groups.join(QChar(' '));
}

static QLabel *plain(const QString &text, const char *role, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    if (role)
        label->setProperty("role", role);
    return label;
}

/** The asset and the amount: the first thing to read. A token looks different from the coin. */
static QFrame *assetCard(const SendConfirmData &data, QWidget *parent)
{
    const GUIUtil::Theme &t = GUIUtil::theme();
    QFrame *card = new QFrame(parent);
    card->setObjectName("assetCard");
    card->setStyleSheet(QString("QFrame#assetCard { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg((data.token ? t.warnBg : t.surface).name(), (data.token ? t.warnBorder : t.accent).name()));
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    QHBoxLayout *head = new QHBoxLayout();
    head->setSpacing(14);
    QLabel *logo = new QLabel(card);
    logo->setFixedSize(52, 52);
    logo->setPixmap(data.token ? GUIUtil::tokenLogo(data.asset, 52) : WizardHeader::logo(52, card->devicePixelRatioF()));
    logo->setScaledContents(true);
    head->addWidget(logo);
    QVBoxLayout *names = new QVBoxLayout();
    names->setSpacing(2);
    QLabel *caption = plain(data.token ? SendConfirmDialog::tr("You are sending a token") : SendConfirmDialog::tr("You are sending the coin"), nullptr, card);
    caption->setFont(GUIUtil::appFont(12, QFont::DemiBold));
    caption->setStyleSheet(QString("color: %1;").arg((data.token ? t.warnText : t.textSecondary).name()));
    names->addWidget(caption);
    QHBoxLayout *nameRow = new QHBoxLayout();
    nameRow->setSpacing(10);
    QLabel *name = new QLabel(data.asset, card);
    name->setFont(GUIUtil::appFont(28, QFont::DemiBold));
    name->setStyleSheet(QString("color: %1;").arg(t.text.name()));
    nameRow->addWidget(name);
    QLabel *chip = new QLabel(data.token ? SendConfirmDialog::tr("Token") : SendConfirmDialog::tr("Coin"), card);
    chip->setFont(GUIUtil::appFont(12, QFont::DemiBold));
    chip->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 11px; padding: 3px 12px;")
        .arg((data.token ? t.warnText : t.accentSoft).name(), (data.token ? t.surface : t.accent).name()));
    nameRow->addWidget(chip, 0, Qt::AlignVCenter);
    nameRow->addStretch();
    names->addLayout(nameRow);
    head->addLayout(names, 1);
    layout->addLayout(head);

    AmountLabel *amount = new AmountLabel(30, QFont::DemiBold, card);
    amount->setAmount(GUIUtil::withThousands(data.total), data.asset);
    amount->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(amount);
    if (!data.source.isEmpty())
        layout->addWidget(plain(data.source, "secondary", card));
    return card;
}

static QWidget *recipientRow(const SendConfirmData &data, const SendConfirmData::Recipient &recipient, int number, QWidget *parent)
{
    const GUIUtil::Theme &t = GUIUtil::theme();
    QFrame *row = new QFrame(parent);
    row->setProperty("role", "tile");
    QVBoxLayout *layout = new QVBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);
    QHBoxLayout *head = new QHBoxLayout();
    head->setSpacing(8);
    QString who = recipient.label.isEmpty() ? SendConfirmDialog::tr("(no label)") : recipient.label;
    if (data.recipients.size() > 1)
        who = QString("%1. %2").arg(number).arg(who);
    QLabel *label = new QLabel(who, row);
    label->setFont(GUIUtil::appFont(13, QFont::DemiBold));
    label->setStyleSheet(QString("color: %1;").arg((recipient.label.isEmpty() ? t.textSecondary : t.text).name()));
    head->addWidget(label);
    if (!recipient.known) {
        QLabel *tag = plain(SendConfirmDialog::tr("New address"), "tag", row);
        tag->setToolTip(SendConfirmDialog::tr("Not in the address book: nothing was sent to this address from this wallet before"));
        head->addWidget(tag);
    }
    head->addStretch();
    if (data.recipients.size() > 1) {
        AmountLabel *amount = new AmountLabel(14, QFont::DemiBold, row);
        amount->setAmount(GUIUtil::withThousands(recipient.amount), data.asset);
        head->addWidget(amount);
    }
    layout->addLayout(head);
    // the whole address, in groups of four so that it can be compared
    QLabel *address = new QLabel(SendConfirmDialog::groupedAddress(recipient.address), row);
    address->setFont(GUIUtil::monoFont(15, QFont::DemiBold));
    address->setWordWrap(true);
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    address->setStyleSheet(QString("color: %1;").arg(t.text.name()));
    layout->addWidget(address);
    if (!recipient.note.isEmpty())
        layout->addWidget(plain(recipient.note, "caption", row));
    return row;
}

static void summaryLine(QGridLayout *grid, const QString &name, const QString &amount, const QString &unit, const QString &note, QWidget *parent, bool strong = false)
{
    const int row = grid->rowCount();
    QLabel *label = plain(name, "secondary", parent);
    grid->addWidget(label, row, 0);
    if (!note.isEmpty())
        grid->addWidget(plain(note, "caption", parent), row, 1);
    AmountLabel *value = new AmountLabel(strong ? 15 : 14, strong ? QFont::DemiBold : QFont::DemiBold, parent);
    value->setAmount(GUIUtil::withThousands(amount), unit);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(value, row, 2);
}

SendConfirmDialog::SendConfirmDialog(const SendConfirmData &data, QWidget *parent) :
    QDialog(parent),
    sendText(data.sendText.isEmpty() ? tr("Send") : data.sendText),
    remaining(CONFIRM_DELAY)
{
    setWindowTitle(tr("Check before sending"));
    setMinimumWidth(560);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 20);
    layout->setSpacing(12);
    layout->addWidget(plain(windowTitle(), "title", this));
    layout->addWidget(assetCard(data, this));

    // to whom
    layout->addWidget(plain(data.recipients.size() > 1 ? tr("Recipients: %n", "", data.recipients.size()) : tr("To"), "fieldLabel", this));
    QWidget *list = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);
    int number = 0;
    for (const SendConfirmData::Recipient &recipient : data.recipients)
        listLayout->addWidget(recipientRow(data, recipient, ++number, list));
    if (data.recipients.size() > 3) {
        QScrollArea *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(list);
        scroll->setFixedHeight(230);
        list->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        layout->addWidget(scroll);
    } else {
        layout->addWidget(list);
    }

    // what it costs and what is left
    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    if (data.recipients.size() > 1)
        summaryLine(grid, tr("Total to the recipients"), data.total, data.asset, QString(), this);
    if (!data.fee.isEmpty())
        summaryLine(grid, tr("Fee"), data.fee, data.feeUnit, data.token ? tr("The fee is paid in %1").arg(data.feeUnit) : data.feeNote, this);
    if (!data.assetDeducted.isEmpty())
        summaryLine(grid, data.token ? tr("Leaves the %1 balance").arg(data.asset) : tr("Leaves the balance (with the fee)"), data.assetDeducted, data.asset, QString(), this, true);
    if (!data.assetAfter.isEmpty())
        summaryLine(grid, tr("%1 balance after sending").arg(data.asset), data.assetAfter, data.asset, QString(), this);
    if (data.token && !data.coinAfter.isEmpty())
        summaryLine(grid, tr("%1 balance after sending").arg(data.feeUnit), data.coinAfter, data.feeUnit, QString(), this);
    QFrame *rule = new QFrame(this);
    rule->setProperty("role", "hline");
    layout->addWidget(rule);
    layout->addLayout(grid);

    // what deserves a second look
    QStringList warnings;
    if (data.token)
        warnings << tr("This sends the token %1, not %2. Check that the recipient expects this token.").arg(data.asset, data.feeUnit);
    if (data.wholeBalance)
        warnings << tr("This sends the whole %1 balance of the wallet.").arg(data.asset);
    int unknown = 0;
    for (const SendConfirmData::Recipient &recipient : data.recipients)
        unknown += recipient.known ? 0 : 1;
    if (unknown > 0)
        warnings << tr("Addresses that are not in the address book: %n. Nothing was sent there from this wallet before. Compare every character; a send cannot be undone.", "", unknown);
    warnings << data.extraWarnings;
    for (const QString &warning : warnings) {
        QLabel *label = plain(warning, "warning", this);
        label->setWordWrap(true);
        layout->addWidget(label);
    }

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setFixedHeight(42);
    cancel->setMinimumWidth(120);
    sendButton = new QPushButton(this);
    sendButton->setProperty("primary", true);
    sendButton->setFixedHeight(42);
    sendButton->setMinimumWidth(170);
    // No default button: Enter does nothing, Space and Escape cancel. Sending takes a deliberate click.
    sendButton->setAutoDefault(false);
    sendButton->setFocusPolicy(Qt::NoFocus);
    cancel->setAutoDefault(false);
    cancel->setFocus();
    buttons->addWidget(cancel);
    buttons->addWidget(sendButton);
    layout->addLayout(buttons);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(sendButton, SIGNAL(clicked()), this, SLOT(accept()));
    connect(&timer, SIGNAL(timeout()), this, SLOT(countDown()));
    sendButton->setEnabled(false);
    sendButton->setText(QString("%1 (%2)").arg(sendText).arg(remaining));
    timer.start(1000);
}

void SendConfirmDialog::countDown()
{
    if (--remaining > 0) {
        sendButton->setText(QString("%1 (%2)").arg(sendText).arg(remaining));
        return;
    }
    timer.stop();
    sendButton->setText(sendText);
    sendButton->setEnabled(true);
}

bool SendConfirmDialog::confirm(const SendConfirmData &data, QWidget *parent)
{
    SendConfirmDialog dialog(data, parent);
    return dialog.exec() == QDialog::Accepted;
}

void SendConfirmDialog::showSent(const SendConfirmData &data, const QString &txid, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Sent"));
    dialog.setMinimumWidth(520);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(26, 22, 26, 20);
    layout->setSpacing(12);
    QLabel *done = plain(tr("Sent: %1 %2").arg(GUIUtil::withThousands(data.total), data.asset)
        + (data.recipients.size() > 1 ? " · " + tr("recipients: %n", "", data.recipients.size()) : QString()), "resultOk", &dialog);
    done->setWordWrap(true);
    layout->addWidget(done);
    layout->addWidget(plain(tr("Transaction ID"), "fieldLabel", &dialog));
    QLabel *id = new QLabel(txid, &dialog);
    id->setFont(GUIUtil::monoFont(13));
    id->setWordWrap(true);
    id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFrame *box = new QFrame(&dialog);
    box->setProperty("role", "tile");
    QVBoxLayout *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(14, 10, 14, 10);
    // a transaction id has no spaces to wrap at: two halves
    id->setText(txid.left(txid.size() / 2) + "\n" + txid.mid(txid.size() / 2));
    boxLayout->addWidget(id);
    layout->addWidget(box);
    layout->addWidget(plain(tr("It shows in the transaction history; it is final when a block confirms it."), "caption", &dialog));
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *copy = new QPushButton(tr("Copy the transaction ID"), &dialog);
    copy->setFixedHeight(40);
    copy->setAutoDefault(false);
    QPushButton *close = new QPushButton(tr("Close"), &dialog);
    close->setProperty("primary", true);
    close->setFixedHeight(40);
    close->setMinimumWidth(110);
    close->setDefault(true);
    buttons->addWidget(copy);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    QObject::connect(copy, &QPushButton::clicked, &dialog, [copy, txid] {
        QApplication::clipboard()->setText(txid);
        copy->setText(SendConfirmDialog::tr("Copied"));
    });
    QObject::connect(close, SIGNAL(clicked()), &dialog, SLOT(accept()));
    dialog.exec();
}

void SendConfirmDialog::showFailed(const QString &reason, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Not sent"));
    dialog.setMinimumWidth(480);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(26, 22, 26, 20);
    layout->setSpacing(12);
    QLabel *failed = plain(tr("Nothing was sent."), "resultError", &dialog);
    layout->addWidget(failed);
    QLabel *why = new QLabel(reason, &dialog);
    why->setWordWrap(true);
    why->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(why);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *close = new QPushButton(tr("Close"), &dialog);
    close->setFixedHeight(40);
    close->setMinimumWidth(110);
    close->setDefault(true);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    QObject::connect(close, SIGNAL(clicked()), &dialog, SLOT(accept()));
    dialog.exec();
}
