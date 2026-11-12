// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "chainparamsbase.h"
#include "chainparams.h"
#include "config/aokchain-config.h"
#endif

#include "fs.h"
#include "intro.h"
#include "ui_intro.h"

#include "firstrun.h"
#include "guiutil.h"

#include "util.h"

#include <QFileDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSettings>
#include <QMessageBox>

#include <cmath>

static const uint64_t GB_BYTES = 1000000000LL;
/* Minimum free space (in GB) needed for data directory */
static const uint64_t BLOCK_CHAIN_SIZE = 5;
/* Minimum free space (in GB) needed for data directory when pruned; Does not include prune target */
static const uint64_t CHAIN_STATE_SIZE = 3;
/* Total required space (in GB) depending on user choice (prune, not prune) */
static uint64_t requiredSpace;

/* Check free space asynchronously to prevent hanging the UI thread.

   Up to one request to check a path is in flight to this thread; when the check()
   function runs, the current path is requested from the associated Intro object.
   The reply is sent back through a signal.

   This ensures that no queue of checking requests is built up while the user is
   still entering the path, and that always the most recently entered path is checked as
   soon as the thread becomes available.
*/
class FreespaceChecker : public QObject
{
    Q_OBJECT

public:
    explicit FreespaceChecker(Intro *intro);

    enum Status {
        ST_OK,
        ST_ERROR
    };

public Q_SLOTS:
    void check();

Q_SIGNALS:
    void reply(int status, const QString &message, quint64 available);

private:
    Intro *intro;
};

#include "intro.moc"

FreespaceChecker::FreespaceChecker(Intro *_intro)
{
    this->intro = _intro;
}

void FreespaceChecker::check()
{
    QString dataDirStr = intro->getPathToCheck();
    fs::path dataDir = GUIUtil::qstringToBoostPath(dataDirStr);
    uint64_t freeBytesAvailable = 0;
    int replyStatus = ST_OK;
    QString replyMessage = tr("A new data directory will be created.");

    /* Find first parent that exists, so that fs::space does not fail */
    fs::path parentDir = dataDir;
    fs::path parentDirOld = fs::path();
    while(parentDir.has_parent_path() && !fs::exists(parentDir))
    {
        parentDir = parentDir.parent_path();

        /* Check if we make any progress, break if not to prevent an infinite loop here */
        if (parentDirOld == parentDir)
            break;

        parentDirOld = parentDir;
    }

    try {
        freeBytesAvailable = fs::space(parentDir).available;
        if(fs::exists(dataDir))
        {
            if(fs::is_directory(dataDir))
            {
                QString separator = "<code>" + QDir::toNativeSeparators("/") + tr("name") + "</code>";
                replyStatus = ST_OK;
                replyMessage = tr("Directory already exists. Add %1 if you intend to create a new directory here.").arg(separator);
            } else {
                replyStatus = ST_ERROR;
                replyMessage = tr("Path already exists, and is not a directory.");
            }
        }
    } catch (const fs::filesystem_error&)
    {
        /* Parent directory does not exist or is not accessible */
        replyStatus = ST_ERROR;
        replyMessage = tr("Cannot create data directory here.");
    }
    Q_EMIT reply(replyStatus, replyMessage, freeBytesAvailable);
}


Intro::Intro(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Intro),
    thread(0),
    signalled(false)
{
    ui->setupUi(this);

    // The first screen of the first start: the look of the program, the header of the wizard,
    // then the language and the data folder as cards.
    setFont(GUIUtil::appFont(13));
    setMinimumWidth(640);
    ui->verticalLayout->setContentsMargins(28, 26, 28, 22);
    ui->verticalLayout->setSpacing(12);
    ui->welcomeLabel->hide();
    for (int i = ui->verticalLayout->count() - 1; i >= 0; --i) {
        // the spacers of the old form
        if (ui->verticalLayout->itemAt(i)->spacerItem())
            delete ui->verticalLayout->takeAt(i);
    }
    ui->verticalLayout->insertWidget(0, new WizardHeader(WizardHeader::Start, tr("Welcome"), QString(), this));

    // The language, first of all and readable in any language
    {
        QFrame *card = new QFrame(this);
        card->setProperty("role", "card");
        QHBoxLayout *row = new QHBoxLayout(card);
        row->setContentsMargins(18, 12, 18, 12);
        row->setSpacing(12);
        QLabel *globe = new QLabel(card);
        globe->setFixedSize(36, 36);
        globe->setAlignment(Qt::AlignCenter);
        globe->setPixmap(GUIUtil::lineIcon("globe", 20, GUIUtil::theme().accent, 1.8));
        globe->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 3px; }").arg(GUIUtil::theme().accentSoft.name()));
        row->addWidget(globe);
        const QString word = tr("Language");
        QLabel *label = new QLabel(word == "Language" ? QString::fromUtf8("Language / 언어") : QString("Language / %1").arg(word), card);
        label->setProperty("role", "title");
        row->addWidget(label);
        row->addStretch(1);
        QComboBox *language = new QComboBox(card);
        GUIUtil::fillLanguageCombo(language);
        language->setMinimumWidth(260);
        language->setFixedHeight(40);
        QSettings settings;
        const int current = language->findData(settings.value("language", "").toString());
        language->setCurrentIndex(current < 0 ? 0 : current);
        row->addWidget(language);
        ui->verticalLayout->insertWidget(1, card);
        connect(language, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this, language](int index) {
            QSettings store;
            if (store.value("language", "").toString() == language->itemData(index).toString())
                return;
            store.setValue("language", language->itemData(index).toString());
            done(LanguageChanged);
        });
    }

    // Where the data goes
    {
        QFrame *card = new QFrame(this);
        card->setProperty("role", "card");
        QVBoxLayout *body = new QVBoxLayout(card);
        body->setContentsMargins(18, 14, 18, 14);
        body->setSpacing(8);
        QLabel *title = new QLabel(tr("Data folder"), card);
        title->setProperty("role", "title");
        body->addWidget(title);
        for (QLabel *label : {ui->storageLabel, ui->sizeWarningLabel}) {
            ui->verticalLayout->removeWidget(label);
            label->setProperty("role", "secondary");
            label->setWordWrap(true);
            body->addWidget(label);
        }
        for (QWidget *radio : {(QWidget*)ui->dataDirDefault, (QWidget*)ui->dataDirCustom}) {
            ui->verticalLayout->removeWidget(radio);
            body->addWidget(radio);
        }
        // the folder, the button to choose one, and what the check of the folder says
        for (QWidget *widget : {(QWidget*)ui->dataDirectory, (QWidget*)ui->ellipsisButton})
            ui->horizontalLayout_2->removeWidget(widget);
        for (QWidget *widget : {(QWidget*)ui->freeSpace, (QWidget*)ui->errorMessage})
            ui->verticalLayout_2->removeWidget(widget);
        ui->verticalLayout->removeItem(ui->horizontalLayout);
        delete ui->horizontalLayout;
        QHBoxLayout *folderRow = new QHBoxLayout();
        folderRow->setSpacing(6);
        folderRow->addWidget(ui->dataDirectory, 1);
        folderRow->addWidget(ui->ellipsisButton);
        body->addLayout(folderRow);
        body->addWidget(ui->freeSpace);
        body->addWidget(ui->errorMessage);
        ui->dataDirectory->setFont(GUIUtil::monoFont(12));
        ui->dataDirectory->setFixedHeight(40);
        ui->ellipsisButton->setMaximumWidth(QWIDGETSIZE_MAX);
        ui->ellipsisButton->setFixedSize(44, 40);
        ui->freeSpace->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        ui->errorMessage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        ui->freeSpace->setProperty("role", "caption");
        ui->errorMessage->setProperty("role", "caption");
        ui->verticalLayout->insertWidget(2, card);
    }
    for (QLabel *label : {ui->lblExplanation1, ui->lblExplanation2, ui->lblExplanation3})
        label->setProperty("role", "caption");
    QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok);
    QPushButton *cancel = ui->buttonBox->button(QDialogButtonBox::Cancel);
    ok->setText(tr("Continue"));
    ok->setIcon(QIcon());
    ok->setProperty("primary", true);
    ok->setMinimumSize(130, 40);
    cancel->setText(tr("Quit"));
    cancel->setIcon(QIcon());
    cancel->setMinimumHeight(40);
    // the form was laid out for less: room for the cards, whatever the language
    resize(680, 660);

    ui->welcomeLabel->setText(ui->welcomeLabel->text().arg(tr(PACKAGE_NAME)));
    ui->storageLabel->setText(ui->storageLabel->text().arg(tr(PACKAGE_NAME)));

    // (all four at once: a translation that leaves one of them out must not shift the others)
    ui->lblExplanation1->setText(ui->lblExplanation1->text()
        .arg(tr(PACKAGE_NAME), QString::number(BLOCK_CHAIN_SIZE), QString::number(2018), tr("AokChain")));
    // With a chain state snapshot for this network the first start does not process the whole chain.
    try {
        const std::unique_ptr<CChainParams> params = CreateChainParams(ChainNameFromCommandLine());
        if (params->Snapshot().height > 0 && gArgs.GetBoolArg("-snapshot", true) && gArgs.GetBoolArg("-bootstrap", true)) {
            ui->lblExplanation1->setText(tr("When you continue, %1 downloads a snapshot of the state of the block chain at block %2 (about 1 GB), checks it against the values that are built into the program, and follows the chain from there. This takes a few minutes. The blocks before the snapshot are downloaded later, and only if you need them.")
                .arg(tr(PACKAGE_NAME), GUIUtil::withThousands(QString::number(params->Snapshot().height))));
        }
    } catch (const std::exception&) {
    }
    ui->lblExplanation2->setText(ui->lblExplanation2->text().arg(tr(PACKAGE_NAME)));

    uint64_t pruneTarget = std::max<int64_t>(0, gArgs.GetArg("-prune", 0));
    requiredSpace = BLOCK_CHAIN_SIZE;
    QString storageRequiresMsg = tr("At least %1 GB of data will be stored in this directory, and it will grow over time.");
    if (pruneTarget) {
        uint64_t prunedGBs = std::ceil(pruneTarget * 1024 * 1024.0 / GB_BYTES);
        if (prunedGBs <= requiredSpace) {
            requiredSpace = prunedGBs;
            storageRequiresMsg = tr("Approximately %1 GB of data will be stored in this directory.");
        }
        ui->lblExplanation3->setVisible(true);
    } else {
        ui->lblExplanation3->setVisible(false);
    }
    requiredSpace += CHAIN_STATE_SIZE;
    ui->sizeWarningLabel->setText(
        tr("%1 will download and store a copy of the AokChain block chain.").arg(tr(PACKAGE_NAME)) + " " +
        storageRequiresMsg.arg(requiredSpace) + " " +
        tr("The wallet will also be stored in this directory.")
    );
    startThread();
}

Intro::~Intro()
{
    delete ui;
    /* Ensure thread is finished before it is deleted */
    Q_EMIT stopThread();
    thread->wait();
}

QString Intro::getDataDirectory()
{
    return ui->dataDirectory->text();
}

void Intro::setDataDirectory(const QString &dataDir)
{
    ui->dataDirectory->setText(dataDir);
    if(dataDir == getDefaultDataDirectory())
    {
        ui->dataDirDefault->setChecked(true);
        ui->dataDirectory->setEnabled(false);
        ui->ellipsisButton->setEnabled(false);
    } else {
        ui->dataDirCustom->setChecked(true);
        ui->dataDirectory->setEnabled(true);
        ui->ellipsisButton->setEnabled(true);
    }
}

QString Intro::getDefaultDataDirectory()
{
    return GUIUtil::boostPathToQString(GetDefaultDataDir());
}

static std::function<void()> languageChangedCallback;

void Intro::setLanguageChanged(std::function<void()> callback)
{
    languageChangedCallback = callback;
}

bool Intro::pickDataDirectory()
{
    QSettings settings;
    /* If data directory provided on command line, no need to look at settings
       or show a picking dialog */
    if(!gArgs.GetArg("-datadir", "").empty())
        return true;
    /* 1) Default data directory for operating system */
    QString dataDir = getDefaultDataDirectory();
    /* 2) Allow QSettings to override default dir */
    dataDir = settings.value("strDataDir", dataDir).toString();

    if(!fs::exists(GUIUtil::qstringToBoostPath(dataDir)) || gArgs.GetBoolArg("-choosedatadir", DEFAULT_CHOOSE_DATADIR) || settings.value("fReset", false).toBool() || gArgs.GetBoolArg("-resetguisettings", false))
    {
        /* If current default data directory does not exist, let the user choose one */
        while(true)
        {
            Intro intro;
            intro.setDataDirectory(dataDir);
            intro.setWindowIcon(QIcon(":icons/aokchain"));
            const int result = intro.exec();
            if(!result)
            {
                /* Cancel clicked */
                return false;
            }
            dataDir = intro.getDataDirectory();
            if (result == LanguageChanged) {
                /* the same dialog again, in the language that was chosen */
                if (languageChangedCallback)
                    languageChangedCallback();
                continue;
            }
            try {
                if (TryCreateDirectories(GUIUtil::qstringToBoostPath(dataDir))) {
                    // If a new data directory has been created, make wallets subdirectory too
                    TryCreateDirectories(GUIUtil::qstringToBoostPath(dataDir) / "wallets");
                }
                break;
            } catch (const fs::filesystem_error&) {
                QMessageBox::critical(0, tr(PACKAGE_NAME),
                    tr("Error: Specified data directory \"%1\" cannot be created.").arg(dataDir));
                /* fall through, back to choosing screen */
            }
        }

        settings.setValue("strDataDir", dataDir);
        settings.setValue("fReset", false);
    }
    /* Only override -datadir if different from the default, to make it possible to
     * override -datadir in the aokchain.conf file in the default data directory
     * (to be consistent with aokchaind behavior)
     */
    if(dataDir != getDefaultDataDirectory())
        gArgs.SoftSetArg("-datadir", GUIUtil::qstringToBoostPath(dataDir).string()); // use OS locale for path setting
    return true;
}

void Intro::setStatus(int status, const QString &message, quint64 bytesAvailable)
{
    switch(status)
    {
    case FreespaceChecker::ST_OK:
        ui->errorMessage->setText(message);
        ui->errorMessage->setStyleSheet("");
        break;
    case FreespaceChecker::ST_ERROR:
        ui->errorMessage->setText(tr("Error") + ": " + message);
        ui->errorMessage->setStyleSheet(QString("QLabel { color: %1 }").arg(GUIUtil::theme().error.name()));
        break;
    }
    /* Indicate number of bytes available */
    if(status == FreespaceChecker::ST_ERROR)
    {
        ui->freeSpace->setText("");
    } else {
        QString freeString = tr("%n GB of free space available", "", bytesAvailable/GB_BYTES);
        if(bytesAvailable < requiredSpace * GB_BYTES)
        {
            freeString += " " + tr("(of %n GB needed)", "", requiredSpace);
            ui->freeSpace->setStyleSheet(QString("QLabel { color: %1 }").arg(GUIUtil::theme().error.name()));
        } else {
            ui->freeSpace->setStyleSheet("");
        }
        ui->freeSpace->setText(freeString + ".");
    }
    /* Don't allow confirm in ERROR state */
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(status != FreespaceChecker::ST_ERROR);
}

void Intro::on_dataDirectory_textChanged(const QString &dataDirStr)
{
    /* Disable OK button until check result comes in */
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    checkPath(dataDirStr);
}

void Intro::on_ellipsisButton_clicked()
{
    QString dir = QDir::toNativeSeparators(QFileDialog::getExistingDirectory(0, "Choose data directory", ui->dataDirectory->text()));
    if(!dir.isEmpty())
        ui->dataDirectory->setText(dir);
}

void Intro::on_dataDirDefault_clicked()
{
    setDataDirectory(getDefaultDataDirectory());
}

void Intro::on_dataDirCustom_clicked()
{
    ui->dataDirectory->setEnabled(true);
    ui->ellipsisButton->setEnabled(true);
}

void Intro::startThread()
{
    thread = new QThread(this);
    FreespaceChecker *executor = new FreespaceChecker(this);
    executor->moveToThread(thread);

    connect(executor, SIGNAL(reply(int,QString,quint64)), this, SLOT(setStatus(int,QString,quint64)));
    connect(this, SIGNAL(requestCheck()), executor, SLOT(check()));
    /*  make sure executor object is deleted in its own thread */
    connect(this, SIGNAL(stopThread()), executor, SLOT(deleteLater()));
    connect(this, SIGNAL(stopThread()), thread, SLOT(quit()));

    thread->start();
}

void Intro::checkPath(const QString &dataDir)
{
    mutex.lock();
    pathToCheck = dataDir;
    if(!signalled)
    {
        signalled = true;
        Q_EMIT requestCheck();
    }
    mutex.unlock();
}

QString Intro::getPathToCheck()
{
    QString retval;
    mutex.lock();
    retval = pathToCheck;
    signalled = false; /* new request can be queued now */
    mutex.unlock();
    return retval;
}
