// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "askpassphrasedialog.h"
#include "ui_askpassphrasedialog.h"

#include "firstrun.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "walletmodel.h"

#include "support/allocators/secure.h"

#include <QKeyEvent>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPushButton>

AskPassphraseDialog::AskPassphraseDialog(Mode _mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AskPassphraseDialog),
    mode(_mode),
    model(0),
    fCapsLock(false),
    onboarding(false),
    strengthLabel(nullptr),
    noteLabel(nullptr),
    understood(nullptr),
    stakingOnly(nullptr)
{
    ui->setupUi(this);
    setMinimumWidth(520);
    // (the height is set when the dialog is shown, see DialogPolisher in guiutil.cpp)
    ui->verticalLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    ui->verticalLayout->setSpacing(14);
    ui->formLayout->setVerticalSpacing(10);
    ui->formLayout->setHorizontalSpacing(14);
    ui->formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    for (QLineEdit *edit : {ui->passEdit1, ui->passEdit2, ui->passEdit3}) {
        edit->setMinimumHeight(40);
        edit->setMinimumWidth(260);
        edit->setMaxLength(MAX_PASSPHRASE_SIZE);
        // Setup Caps Lock detection.
        edit->installEventFilter(this);
    }
    for (QLabel *label : {ui->passLabel1, ui->passLabel2, ui->passLabel3})
        label->setProperty("role", "fieldLabel");
    ui->capsLabel->setProperty("role", "error");
    ui->capsLabel->setFont(GUIUtil::appFont(12));
    ui->capsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok);
    QPushButton *cancel = ui->buttonBox->button(QDialogButtonBox::Cancel);
    ok->setProperty("primary", true);
    ok->setMinimumSize(120, 40);
    ok->setIcon(QIcon());
    cancel->setMinimumHeight(40);
    cancel->setIcon(QIcon());
    cancel->setText(tr("Cancel"));

    switch(mode)
    {
        case Encrypt: { // Ask passphrase x2
            ui->warningLabel->setText(tr("Choose a passphrase for the wallet. It is asked for when you send coins. A thief who copies the wallet file cannot spend the coins without it."));
            ui->passLabel1->hide();
            ui->passEdit1->hide();
            setWindowTitle(tr("Encrypt wallet"));
            ok->setText(tr("Encrypt wallet"));
            // how good the passphrase is, below the first field
            strengthLabel = new QLabel(this);
            strengthLabel->setProperty("role", "caption");
            ui->formLayout->insertRow(2, QString(), strengthLabel);
            noteLabel = new QLabel(tr("<b>The passphrase cannot be recovered.</b> Nobody can reset it, not even the developers. If you forget it, the coins in this wallet are lost. Write it down and keep it where only you find it."), this);
            noteLabel->setProperty("role", "warning");
            noteLabel->setWordWrap(true);
            ui->verticalLayout->insertWidget(2, noteLabel);
            understood = new QCheckBox(tr("I wrote the passphrase down and understand that it cannot be recovered"), this);
            ui->verticalLayout->insertWidget(3, understood);
            connect(understood, SIGNAL(toggled(bool)), this, SLOT(textChanged()));
            break;
        }
        case UnlockStaking:
            stakingOnly = new QCheckBox(tr("For staking only (sending coins asks for the passphrase again)"), this);
            stakingOnly->setChecked(true);
            ui->verticalLayout->insertWidget(2, stakingOnly);
            // fall through
        case Unlock: // Ask passphrase
            ui->warningLabel->setText(mode == UnlockStaking
                ? tr("Staking needs the wallet unlocked. Enter the wallet passphrase to start staking.")
                : tr("This operation needs your wallet passphrase to unlock the wallet."));
            ui->passLabel2->hide();
            ui->passEdit2->hide();
            ui->passLabel3->hide();
            ui->passEdit3->hide();
            setWindowTitle(tr("Unlock wallet"));
            ok->setText(mode == UnlockStaking ? tr("Start staking") : tr("Unlock"));
            break;
        case Decrypt:   // Ask passphrase
            ui->warningLabel->setText(tr("This operation needs your wallet passphrase to decrypt the wallet."));
            ui->passLabel2->hide();
            ui->passEdit2->hide();
            ui->passLabel3->hide();
            ui->passEdit3->hide();
            setWindowTitle(tr("Decrypt wallet"));
            break;
        case ChangePass: // Ask old passphrase + new passphrase x2
            setWindowTitle(tr("Change passphrase"));
            ui->warningLabel->setText(tr("Enter the old passphrase and new passphrase to the wallet."));
            ok->setText(tr("Change passphrase"));
            break;
    }
    // rows that are not used take no room
    for (QWidget *widget : {(QWidget*)ui->passLabel1, (QWidget*)ui->passEdit1, (QWidget*)ui->passLabel2, (QWidget*)ui->passEdit2, (QWidget*)ui->passLabel3, (QWidget*)ui->passEdit3}) {
        if (widget->isHidden())
            ui->formLayout->removeWidget(widget);
    }
    ui->capsLabel->hide();
    // the title of the window again inside it, as the pages have it
    QLabel *title = new QLabel(windowTitle(), this);
    title->setObjectName("dialogTitle");
    title->setProperty("role", "title");
    ui->verticalLayout->insertWidget(0, title);
    ui->warningLabel->setProperty("role", "secondary");

    textChanged();
    connect(ui->passEdit1, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
    connect(ui->passEdit2, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
    connect(ui->passEdit3, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
}

AskPassphraseDialog::~AskPassphraseDialog()
{
    secureClearPassFields();
    delete ui;
}

void AskPassphraseDialog::setModel(WalletModel *_model)
{
    this->model = _model;
}

void AskPassphraseDialog::setOnboarding()
{
    onboarding = true;
    setWindowTitle(tr("Welcome"));
    // no way around this step except leaving the program
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint & ~Qt::WindowContextHelpButtonHint);
    if (QLabel *title = findChild<QLabel*>("dialogTitle"))
        title->hide();
    ui->warningLabel->hide();
    ui->verticalLayout->insertWidget(0, new WizardHeader(WizardHeader::Passphrase, tr("Protect the wallet"),
        tr("Choose a passphrase for the new wallet. It is asked for when you send coins. A thief who copies the wallet file cannot spend the coins without it."), this));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Quit"));
    ui->verticalLayout->setContentsMargins(28, 26, 28, 22);
    setMinimumWidth(580);
}

void AskPassphraseDialog::accept()
{
    SecureString oldpass, newpass1, newpass2;
    if(!model)
        return;
    oldpass.reserve(MAX_PASSPHRASE_SIZE);
    newpass1.reserve(MAX_PASSPHRASE_SIZE);
    newpass2.reserve(MAX_PASSPHRASE_SIZE);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make this input mlock()'d to begin with.
    oldpass.assign(ui->passEdit1->text().toStdString().c_str());
    newpass1.assign(ui->passEdit2->text().toStdString().c_str());
    newpass2.assign(ui->passEdit3->text().toStdString().c_str());

    switch(mode)
    {
    case Encrypt: {
        if(newpass1.empty() || newpass2.empty())
        {
            // Cannot encrypt with empty passphrase
            break;
        }
        if(newpass1 != newpass2)
        {
            // the fields stay: only the repetition has to be typed again
            ui->capsLabel->setText(tr("The supplied passphrases do not match."));
            ui->capsLabel->show();
            ui->passEdit3->setFocus();
            ui->passEdit3->selectAll();
            break;
        }
        secureClearPassFields();
        if(model->setWalletEncrypted(true, newpass1))
        {
            // The wallet file is rewritten when the program ends; it starts again by itself
            // and then offers the backup.
            QMessageBox::information(this, tr("Wallet encrypted"),
                                 "<qt>" +
                                 tr("The wallet is encrypted. %1 restarts now to finish.").arg(tr(PACKAGE_NAME)) +
                                 "<br><br>" +
                                 tr("Backups of the wallet file made before today are not encrypted: replace them with a new backup after the restart.") +
                                 "</qt>");
            GUIUtil::requestRestart(QStringList() << "-backupprompt");
            QApplication::quit();
            QDialog::accept(); // Success
        }
        else
        {
            QMessageBox::critical(this, tr("Wallet encryption failed"),
                                 tr("Wallet encryption failed due to an internal error. Your wallet was not encrypted."));
            if (!onboarding)
                QDialog::reject();
        }
        } break;
    case Unlock:
    case UnlockStaking:
        secureClearPassFields();
        if(!model->setWalletLocked(false, oldpass, stakingOnly && stakingOnly->isChecked()))
        {
            QMessageBox::critical(this, tr("Wallet unlock failed"),
                                  tr("The passphrase entered for the wallet decryption was incorrect."));
        }
        else
        {
            QDialog::accept(); // Success
        }
        break;
    case Decrypt:
        secureClearPassFields();
        if(!model->setWalletEncrypted(false, oldpass))
        {
            QMessageBox::critical(this, tr("Wallet decryption failed"),
                                  tr("The passphrase entered for the wallet decryption was incorrect."));
        }
        else
        {
            QDialog::accept(); // Success
        }
        break;
    case ChangePass:
        secureClearPassFields();
        if(newpass1 == newpass2)
        {
            if(model->changePassphrase(oldpass, newpass1))
            {
                QMessageBox::information(this, tr("Wallet encrypted"),
                                     tr("Wallet passphrase was successfully changed."));
                QDialog::accept(); // Success
            }
            else
            {
                QMessageBox::critical(this, tr("Wallet encryption failed"),
                                     tr("The passphrase entered for the wallet decryption was incorrect."));
            }
        }
        else
        {
            QMessageBox::critical(this, tr("Wallet encryption failed"),
                                 tr("The supplied passphrases do not match."));
        }
        break;
    }
}

void AskPassphraseDialog::reject()
{
    if (onboarding) {
        // leaving the first start without a passphrase: the program ends, the wallet stays as it is
        if (QMessageBox::question(this, tr(PACKAGE_NAME), tr("The wallet needs a passphrase before it can be used. Quit %1?").arg(tr(PACKAGE_NAME)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }
    QDialog::reject();
}

/** A rough hint, not a meter: length and variety of the passphrase. */
static int passphraseScore(const QString &text)
{
    if (text.isEmpty())
        return 0;
    int classes = 0;
    bool lower = false, upper = false, digit = false, other = false;
    for (const QChar c : text) {
        if (c.isDigit()) digit = true;
        else if (c.isLower()) lower = true;
        else if (c.isUpper()) upper = true;
        else other = true;
    }
    classes = lower + upper + digit + other;
    const int words = text.split(' ', QString::SkipEmptyParts).size();
    if (text.size() < 8)
        return 1;
    if (text.size() >= 16 || words >= 4 || (text.size() >= 12 && classes >= 3))
        return 3;
    if (text.size() >= 10 && classes >= 2)
        return 2;
    return 1;
}

void AskPassphraseDialog::textChanged()
{
    // Validate input, set Ok button to enabled when acceptable
    bool acceptable = false;
    switch(mode)
    {
    case Encrypt: { // New passphrase x2
        acceptable = !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty() && understood && understood->isChecked();
        if (strengthLabel) {
            const int score = passphraseScore(ui->passEdit2->text());
            const GUIUtil::Theme &t = GUIUtil::theme();
            const QString hint = tr("Use ten or more random characters, or four or more words.");
            const QString text = score == 0 ? hint
                : score == 1 ? tr("Weak.") + " " + hint
                : score == 2 ? tr("Fair. Longer is better.")
                : tr("Strong.");
            strengthLabel->setText(text);
            strengthLabel->setStyleSheet(QString("color: %1;").arg((score == 0 ? t.textSecondary : score == 1 ? t.error : score == 2 ? t.warnText : t.ok).name()));
        }
        if (ui->passEdit3->text() == ui->passEdit2->text())
            ui->capsLabel->clear();
        break;
    }
    case Unlock: // Old passphrase x1
    case UnlockStaking:
    case Decrypt:
        acceptable = !ui->passEdit1->text().isEmpty();
        break;
    case ChangePass: // Old passphrase x1, new passphrase x2
        acceptable = !ui->passEdit1->text().isEmpty() && !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty();
        break;
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(acceptable);
}

bool AskPassphraseDialog::event(QEvent *event)
{
    // Detect Caps Lock key press.
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_CapsLock) {
            fCapsLock = !fCapsLock;
        }
        if (fCapsLock) {
            ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
            ui->capsLabel->show();
        } else {
            ui->capsLabel->clear();
        }
    }
    return QWidget::event(event);
}

bool AskPassphraseDialog::eventFilter(QObject *object, QEvent *event)
{
    /* Detect Caps Lock.
     * There is no good OS-independent way to check a key state in Qt, but we
     * can detect Caps Lock by checking for the following condition:
     * Shift key is down and the result is a lower case character, or
     * Shift key is not down and the result is an upper case character.
     */
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        QString str = ke->text();
        if (str.length() != 0) {
            const QChar *psz = str.unicode();
            bool fShift = (ke->modifiers() & Qt::ShiftModifier) != 0;
            if ((fShift && *psz >= 'a' && *psz <= 'z') || (!fShift && *psz >= 'A' && *psz <= 'Z')) {
                fCapsLock = true;
                ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
            ui->capsLabel->show();
            } else if (psz->isLetter()) {
                fCapsLock = false;
                ui->capsLabel->clear();
            }
        }
    }
    return QDialog::eventFilter(object, event);
}

static void SecureClearQLineEdit(QLineEdit* edit)
{
    // Attempt to overwrite text so that they do not linger around in memory
    edit->setText(QString(" ").repeated(edit->text().size()));
    edit->clear();
}

void AskPassphraseDialog::secureClearPassFields()
{
    SecureClearQLineEdit(ui->passEdit1);
    SecureClearQLineEdit(ui->passEdit2);
    SecureClearQLineEdit(ui->passEdit3);
}

BackupPromptDialog::BackupPromptDialog(WalletModel *_model, QWidget *parent) :
    QDialog(parent),
    model(_model)
{
    setWindowTitle(tr("Back up the wallet"));
    setMinimumWidth(560);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 26, 28, 22);
    layout->setSpacing(16);
    layout->addWidget(new WizardHeader(WizardHeader::Backup, tr("Keep a backup"),
        tr("The wallet is encrypted now. One more step: save a copy of the wallet file."), this));

    QFrame *card = new QFrame(this);
    card->setProperty("role", "card");
    QVBoxLayout *body = new QVBoxLayout(card);
    body->setContentsMargins(20, 16, 20, 16);
    body->setSpacing(10);
    for (const QString &line : {
            tr("The coins are in the wallet file on this computer. If the disk fails or the computer is lost, only a backup brings them back."),
            tr("Save the copy somewhere else: a USB drive or another computer, not only this disk."),
            tr("The backup is encrypted with the same passphrase. Without the passphrase it is of no use, to a thief or to you.")}) {
        QLabel *label = new QLabel(QString::fromUtf8("•  ") + line, card);
        label->setWordWrap(true);
        body->addWidget(label);
    }
    layout->addWidget(card);

    result = new QLabel(this);
    result->setProperty("role", "resultOk");
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result->hide();
    layout->addWidget(result);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    laterButton = new QPushButton(tr("Later"), this);
    laterButton->setFixedHeight(40);
    laterButton->setAutoDefault(false);
    saveButton = new QPushButton(tr("Save a backup..."), this);
    saveButton->setProperty("primary", true);
    saveButton->setFixedHeight(40);
    saveButton->setMinimumWidth(150);
    saveButton->setDefault(true);
    buttons->addWidget(laterButton);
    buttons->addWidget(saveButton);
    layout->addLayout(buttons);
    connect(laterButton, SIGNAL(clicked()), this, SLOT(reject()));
    connect(saveButton, SIGNAL(clicked()), this, SLOT(save()));
}

void BackupPromptDialog::save()
{
    if (!model)
        return;
    const QString filename = GUIUtil::getSaveFileName(this, tr("Backup Wallet"), QString(), tr("Wallet Data (*.dat)"), nullptr);
    if (filename.isEmpty())
        return;
    const bool ok = model->backupWallet(filename);
    result->setProperty("role", ok ? "resultOk" : "resultError");
    result->style()->unpolish(result);
    result->style()->polish(result);
    result->setText(ok ? tr("The backup was saved to %1.").arg(filename)
                       : tr("There was an error trying to save the wallet data to %1.").arg(filename));
    result->show();
    GUIUtil::fitDialogHeight(this);
    if (ok) {
        // done: the one button left closes the window
        laterButton->hide();
        saveButton->setText(tr("Done"));
        disconnect(saveButton, SIGNAL(clicked()), this, SLOT(save()));
        connect(saveButton, SIGNAL(clicked()), this, SLOT(accept()));
    }
}
