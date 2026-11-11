// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pastblocks.h"

#include "guiutil.h"
#include "optionsdialog.h"

#include "chainparams.h"
#include "snapshot.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

bool PastBlocks::missing()
{
    return PastBlocksMissing();
}

QString PastBlocks::sizeText()
{
    const PastBlocksInfo info = GetPastBlocksInfo();
    if (info.bytesTotal > 0)
        return tr("about %1 GB").arg(QString::number(info.bytesTotal / 1e9, 'f', 1));
    if (Params().NetworkIDString() == "main")
        return tr("about %1 GB").arg("1.4");
    return QString();
}

QString PastBlocks::timeLeft(qint64 seconds)
{
    if (seconds < 60)
        return tr("less than a minute left");
    if (seconds < 90 * 60)
        return tr("about %1 min left").arg((seconds + 30) / 60);
    return tr("about %1 h %2 min left").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

static bool startDownload(QWidget *parent)
{
    std::string error;
    if (GetPastBlocksInfo().fetching || FetchPastBlocks(error))
        return true;
    QMessageBox::warning(parent, PastBlocks::tr("Past blocks"),
                         PastBlocks::tr("The download could not be started: %1").arg(QString::fromStdString(error)));
    return false;
}

bool PastBlocks::offer(QWidget *parent, const QString& purpose)
{
    if (!PastBlocksMissing())
        return false;
    if (GetPastBlocksInfo().fetching) {
        QMessageBox::information(parent, tr("Past blocks"),
                                 tr("%1 needs the past blocks. They are being downloaded: Settings shows how far the download is. Try again when it has finished.").arg(purpose));
        return true;
    }
    const QString size = sizeText();
    QMessageBox box(QMessageBox::Question, tr("Past blocks"),
                    (size.isEmpty() ? tr("%1 needs the past blocks. Download them now?").arg(purpose)
                                    : tr("%1 needs the past blocks, %2. Download them now?").arg(purpose, size)),
                    QMessageBox::NoButton, parent);
    box.setInformativeText(tr("This wallet started from a snapshot of the chain state and does not hold the blocks before block %1. "
                              "The download runs in the background while you keep using the wallet; the blocks are only stored, not validated again.")
                               .arg(GUIUtil::withThousands(QString::number(SnapshotHeight()))));
    QPushButton *yes = box.addButton(tr("Download"), QMessageBox::AcceptRole);
    box.addButton(tr("Not now"), QMessageBox::RejectRole);
    box.setDefaultButton(yes);
    box.exec();
    if (box.clickedButton() != yes) {
        QMessageBox::information(parent, tr("Past blocks"),
                                 tr("Without the past blocks this is not available. You can download them at any time under Settings."));
        return false;
    }
    return startDownload(parent);
}

bool PastBlocks::offerForWallet(QWidget *parent, const QString& purpose)
{
    if (!PastBlocksMissing())
        return false;
    // the wallet is scanned from the first block when the past blocks have arrived
    SetWalletRescanPending(true);
    return offer(parent, purpose);
}

QFrame *PastBlocks::settingsCard(QWidget *parent)
{
    if (SnapshotHeight() < 0)
        return nullptr;
    QVBoxLayout *body = nullptr;
    QFrame *card = OptionsDialog::makeCard(tr("Past blocks"), parent, &body);
    QLabel *state = new QLabel(card);
    state->setWordWrap(true);
    body->addWidget(state);
    QLabel *hint = new QLabel(card);
    hint->setProperty("role", "secondary");
    hint->setWordWrap(true);
    body->addWidget(hint);
    QHBoxLayout *row = new QHBoxLayout();
    QPushButton *download = new QPushButton(tr("Download the past blocks"), card);
    download->setAutoDefault(false);
    row->addWidget(download);
    row->addStretch();
    body->addLayout(row);
    QObject::connect(download, &QPushButton::clicked, card, [card]() { startDownload(card); });

    QTimer *timer = new QTimer(card);
    // (time in ms, bytes) when the running download was first seen, for the time that is left
    struct Seen { qint64 at = 0; quint64 bytes = 0; };
    Seen *seen = new Seen();
    QObject::connect(card, &QObject::destroyed, [seen]() { delete seen; });
    auto update = [state, hint, download, seen]() {
        const PastBlocksInfo info = GetPastBlocksInfo();
        const QString height = GUIUtil::withThousands(QString::number(info.snapshotHeight));
        const QString waits = WalletRescanPending() ? QString(" ") + tr("The wallet is waiting for them to look for its earlier transactions.") : QString();
        if (info.fetching) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (seen->at == 0 || info.bytesDone < seen->bytes) {
                seen->at = now;
                seen->bytes = info.bytesDone;
            }
            QString left;
            if (now - seen->at > 5000 && info.bytesDone > seen->bytes && info.bytesTotal > info.bytesDone)
                left = " · " + timeLeft(qint64(double(now - seen->at) / 1000 * (info.bytesTotal - info.bytesDone) / (info.bytesDone - seen->bytes)));
            if (info.bytesTotal > 0 && info.bytesDone >= info.bytesTotal && WalletRescanPending() && info.blocksStored > info.snapshotHeight)
                state->setText(tr("The past blocks are stored. Looking for the earlier transactions of the wallet..."));
            else
                state->setText(tr("Downloading the past blocks: %1 of %2 MB%3").arg(info.bytesDone / 1000000).arg(info.bytesTotal / 1000000).arg(left));
            hint->setText(tr("You can keep using the wallet. After a restart the download starts again from here when you ask for it.") + waits);
        } else {
            seen->at = 0;
            if (info.missing) {
                state->setText(tr("This wallet started from a snapshot of the chain state at block %1 and does not hold the blocks before it.").arg(height));
                QString text = tr("It validates new blocks, sends, receives and stakes without them. They are needed to find the earlier transactions of an imported key or a restored wallet, to look up old transactions in the explorer, and to pass the whole chain on to other nodes.") + waits;
                if (!info.error.empty())
                    text += "\n" + tr("The last download stopped: %1").arg(QString::fromStdString(info.error));
                hint->setText(text);
            } else {
                state->setText(tr("The past blocks are on this computer."));
                hint->setText(tr("This wallet started from a snapshot of the chain state at block %1 and has downloaded the blocks before it. After the next start it passes the whole chain on to other nodes.").arg(height));
            }
        }
        download->setVisible(info.missing);
        download->setEnabled(!info.fetching);
    };
    QObject::connect(timer, &QTimer::timeout, card, update);
    timer->start(1000);
    update();
    return card;
}
