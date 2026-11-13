// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_FIRSTRUN_H
#define AOKCHAIN_QT_FIRSTRUN_H

#include <QDialog>
#include <QWidget>


/**
 * The screens of the first start are one sequence: where the data goes and
 * the language (Intro), the passphrase of the wallet (AskPassphraseDialog),
 * the backup (BackupPromptDialog). Each of them begins with this header: the
 * logo, a title and the steps with the current one marked.
 */
class WizardHeader : public QWidget
{
    Q_OBJECT

public:
    enum Step { Start = 0, Passphrase = 1, Backup = 2 };
    WizardHeader(Step step, const QString &title, const QString &subtitle, QWidget *parent);

    /** The logo of the program with rounded corners */
    static QPixmap logo(int size, qreal devicePixelRatio);
};

#endif // AOKCHAIN_QT_FIRSTRUN_H
