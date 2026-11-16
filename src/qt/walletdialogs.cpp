// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletdialogs.h"

#include "guiutil.h"
#include "walletcontroller.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegExp>
#include <QVBoxLayout>

namespace {
/** Title and one line below it, as the dialogs of the program start */
void addHeader(QVBoxLayout *layout, const QString &title, const QString &subtitle, QWidget *parent)
{
    // (the heading of a dialog, as "Check before sending" and the passphrase dialog have it)
    QLabel *head = new QLabel(title, parent);
    head->setProperty("role", "title");
    layout->addWidget(head);
    QLabel *sub = new QLabel(subtitle, parent);
    sub->setProperty("role", "secondary");
    sub->setWordWrap(true);
    layout->addWidget(sub);
}

QPushButton *makeButton(const QString &text, bool primary, QWidget *parent)
{
    QPushButton *button = new QPushButton(text, parent);
    button->setFixedHeight(40);
    button->setCursor(Qt::PointingHandCursor);
    if (primary) {
        button->setProperty("primary", true);
        button->setMinimumWidth(120);
    }
    button->setAutoDefault(false);
    return button;
}

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "fieldLabel");
    return label;
}
} // namespace

CreateWalletDialog::CreateWalletDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Create wallet"));
    setMinimumWidth(520);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 26, 28, 22);
    layout->setSpacing(10);
    addHeader(layout, tr("A new wallet"),
        tr("New keys, addresses and a passphrase of its own, next to the wallets that are open. Switch between them at the top of the side bar."), this);
    layout->addSpacing(8);

    layout->addWidget(fieldLabel(tr("Name"), this));
    nameEdit = new QLineEdit(this);
    nameEdit->setMinimumHeight(40);
    nameEdit->setMaxLength(40);
    nameEdit->setPlaceholderText(tr("For example: Savings, Issuer"));
    layout->addWidget(nameEdit);
    problem = new QLabel(this);
    problem->setProperty("role", "error");
    problem->setWordWrap(true);
    problem->hide();
    layout->addWidget(problem);

    layout->addSpacing(8);
    encryptBox = new QCheckBox(tr("Protect it with a passphrase (asked for next)"), this);
    encryptBox->setChecked(true);
    layout->addWidget(encryptBox);
    QLabel *encryptNote = new QLabel(tr("Without a passphrase, anyone who copies the wallet file or uses this computer can send its coins and tokens."), this);
    encryptNote->setProperty("role", "secondary");
    encryptNote->setWordWrap(true);
    layout->addWidget(encryptNote);

    layout->addSpacing(12);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = makeButton(tr("Cancel"), false, this);
    createButton = makeButton(tr("Create"), true, this);
    createButton->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(createButton);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(createButton, &QPushButton::clicked, this, [this] {
        QString reason;
        if (WalletController::checkNewName(nameEdit->text(), &reason))
            accept();
        else
            check();
    });
    connect(nameEdit, &QLineEdit::textChanged, this, &CreateWalletDialog::check);
    encryptNote->setVisible(false);
    connect(encryptBox, &QCheckBox::toggled, this, [this, encryptNote](bool on) {
        encryptNote->setVisible(!on);
        GUIUtil::fitDialogHeight(this);
    });
    createButton->setEnabled(false);
    nameEdit->setFocus();
}

void CreateWalletDialog::check()
{
    QString reason;
    const bool ok = WalletController::checkNewName(nameEdit->text(), &reason);
    // no complaint about an empty field that nobody has typed into yet
    problem->setText(reason);
    problem->setVisible(!ok && !nameEdit->text().isEmpty());
    createButton->setEnabled(ok);
    GUIUtil::fitDialogHeight(this);
}

QString CreateWalletDialog::walletName() const
{
    return nameEdit->text().trimmed();
}

bool CreateWalletDialog::encrypt() const
{
    return encryptBox->isChecked();
}

OpenWalletDialog::OpenWalletDialog(const QStringList &closedWallets, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Open wallet"));
    setMinimumWidth(520);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 26, 28, 22);
    layout->setSpacing(10);
    addHeader(layout, tr("Open a wallet"),
        closedWallets.isEmpty()
            ? tr("Every wallet of the wallet folder is open. A wallet from a backup file can be restored as a wallet of its own.")
            : tr("The wallets of the wallet folder that are not open. An open wallet is shown next to the others and can be closed again."), this);
    layout->addSpacing(8);

    list = new QListWidget(this);
    list->setMinimumHeight(160);
    for (const QString &name : closedWallets) {
        QListWidgetItem *item = new QListWidgetItem(WalletController::displayName(name), list);
        item->setData(Qt::UserRole, name);
        item->setToolTip(WalletController::walletPath(name));
    }
    list->setVisible(!closedWallets.isEmpty());
    layout->addWidget(list, 1);

    layout->addSpacing(12);
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *restoreFile = makeButton(tr("Restore Wallet..."), false, this);
    buttons->addWidget(restoreFile);
    buttons->addStretch();
    QPushButton *cancel = makeButton(tr("Cancel"), false, this);
    openButton = makeButton(tr("Open"), true, this);
    openButton->setDefault(true);
    openButton->setEnabled(false);
    openButton->setVisible(!closedWallets.isEmpty());
    buttons->addWidget(cancel);
    buttons->addWidget(openButton);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(restoreFile, &QPushButton::clicked, this, [this] { restore = true; accept(); });
    connect(openButton, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem *item = list->currentItem()) {
            chosen = item->data(Qt::UserRole).toString();
            accept();
        }
    });
    connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) { openButton->setEnabled(item != nullptr); });
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        chosen = item->data(Qt::UserRole).toString();
        accept();
    });
    if (list->count() > 0)
        list->setCurrentRow(0);
}

RestoreWalletDialog::RestoreWalletDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Restore wallet"));
    setMinimumWidth(560);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 26, 28, 22);
    layout->setSpacing(10);
    addHeader(layout, tr("Restore a backup"),
        tr("A backup file of a wallet (Backup Wallet, or a copy of a wallet.dat) becomes a wallet of its own, next to the open ones. The backup file itself is not changed."), this);
    layout->addSpacing(8);

    layout->addWidget(fieldLabel(tr("Backup file"), this));
    QHBoxLayout *fileRow = new QHBoxLayout();
    fileRow->setSpacing(8);
    fileEdit = new QLineEdit(this);
    fileEdit->setMinimumHeight(40);
    fileEdit->setReadOnly(true);
    fileEdit->setFont(GUIUtil::monoFont(12));
    fileRow->addWidget(fileEdit, 1);
    QPushButton *choose = makeButton(tr("Choose..."), false, this);
    fileRow->addWidget(choose);
    layout->addLayout(fileRow);

    layout->addSpacing(4);
    layout->addWidget(fieldLabel(tr("Name of the restored wallet"), this));
    nameEdit = new QLineEdit(this);
    nameEdit->setMinimumHeight(40);
    nameEdit->setMaxLength(40);
    layout->addWidget(nameEdit);
    problem = new QLabel(this);
    problem->setProperty("role", "error");
    problem->setWordWrap(true);
    problem->hide();
    layout->addWidget(problem);

    layout->addSpacing(12);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *cancel = makeButton(tr("Cancel"), false, this);
    restoreButton = makeButton(tr("Restore"), true, this);
    restoreButton->setDefault(true);
    restoreButton->setEnabled(false);
    buttons->addWidget(cancel);
    buttons->addWidget(restoreButton);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(choose, &QPushButton::clicked, this, &RestoreWalletDialog::chooseFile);
    connect(nameEdit, &QLineEdit::textChanged, this, &RestoreWalletDialog::check);
    connect(restoreButton, &QPushButton::clicked, this, [this] {
        QString reason;
        if (!fileEdit->text().isEmpty() && WalletController::checkNewName(nameEdit->text(), &reason))
            accept();
        else
            check();
    });
}

void RestoreWalletDialog::chooseFile()
{
    const QString file = GUIUtil::getOpenFileName(this, tr("Backup file of a wallet"), QString(), tr("Wallet Data (*.dat);;All files (*)"), nullptr);
    if (file.isEmpty())
        return;
    fileEdit->setText(file);
    fileEdit->setToolTip(file);
    if (nameEdit->text().isEmpty()) {
        // a name from the file name, which the user may change
        QString base = QFileInfo(file).completeBaseName();
        if (base.compare("wallet", Qt::CaseInsensitive) == 0)
            base = "Restored"; // (not translated: a wallet name is made of ASCII letters)
        base.replace(QRegExp("[^A-Za-z0-9 _.-]"), " ");
        nameEdit->setText(base.simplified().left(40));
    }
    check();
}

void RestoreWalletDialog::check()
{
    QString reason;
    const bool nameOk = WalletController::checkNewName(nameEdit->text(), &reason);
    problem->setText(reason);
    problem->setVisible(!nameOk && !nameEdit->text().isEmpty());
    restoreButton->setEnabled(nameOk && !fileEdit->text().isEmpty());
    GUIUtil::fitDialogHeight(this);
}

QString RestoreWalletDialog::walletName() const
{
    return nameEdit->text().trimmed();
}

QString RestoreWalletDialog::backupFile() const
{
    return fileEdit->text();
}
