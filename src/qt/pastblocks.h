// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_PASTBLOCKS_H
#define AOKCHAIN_QT_PASTBLOCKS_H

#include <QCoreApplication>
#include <QString>

class QFrame;
class QWidget;

/**
 * The past blocks of a wallet that started from a chain state snapshot (see
 * snapshot.h): what the wallet says about them, the question whether to
 * download them, and the card of the settings page.
 */
class PastBlocks
{
    Q_DECLARE_TR_FUNCTIONS(PastBlocks)

public:
    /** True while this node lacks the blocks up to its snapshot height */
    static bool missing();
    /** "about 1.4 GB", or nothing when the size is not known */
    static QString sizeText();
    /** "about 3 min left" from a number of seconds */
    static QString timeLeft(qint64 seconds);
    /**
     * The download is between two archives: the blocks of the last one are being written to the
     * disk, and the count of downloaded bytes stands still meanwhile.
     */
    static bool storing(quint64 bytesDone);
    /**
     * Something needs the past blocks: ask whether to download them now and start the download on
     * yes. purpose names what needs them ("Showing the earlier transactions of this wallet").
     * Returns true when a download runs afterwards.
     */
    static bool offer(QWidget *parent, const QString& purpose);
    /** The same for a wallet scan: the wallet is scanned when the blocks are there. */
    static bool offerForWallet(QWidget *parent, const QString& purpose);
    /** The card of the settings page; nullptr when this node did not start from a snapshot */
    static QFrame *settingsCard(QWidget *parent);
};

#endif // AOKCHAIN_QT_PASTBLOCKS_H
