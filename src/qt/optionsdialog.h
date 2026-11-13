// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_OPTIONSDIALOG_H
#define AOKCHAIN_QT_OPTIONSDIALOG_H

#include <QDialog>
#include <QValidator>

class OptionsModel;
class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QVBoxLayout;
class QValidatedLineEdit;

QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

namespace Ui {
class OptionsDialog;
}

/** Proxy address widget validator, checks for a valid proxy address.
 */
class ProxyAddressValidator : public QValidator
{
    Q_OBJECT

public:
    explicit ProxyAddressValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Preferences dialog. */
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent, bool enableWallet);
    ~OptionsDialog();

    void setModel(OptionsModel *model);
    void setMapper();
    /** Show the options as cards inside a page of the main window; they are saved with a button */
    void embedAsPage();
    /** Add a card of the page below the cards of the options (right column) */
    void addCard(QWidget *card);
    /** A card with a title, as the page uses them */
    static QFrame *makeCard(const QString &title, QWidget *parent, QVBoxLayout **body);
    /** The note about options overridden on the command line, for the end of the information card (page only) */
    QWidget *overrideBlock();

public Q_SLOTS:
    /** The network options (port mapping, incoming connections, proxy, fast synchronization) in a window of their own */
    void showNetworkDialog();

public Q_SLOTS:
    void done(int retval) override;

private Q_SLOTS:
    /* set OK button state (enabled / disabled) */
    void setOkButtonState(bool fState);
    void on_resetButton_clicked();
    void on_openAokChainConfButton_clicked();
    void on_okButton_clicked();
    void on_cancelButton_clicked();
    
    void on_hideTrayIcon_stateChanged(int fState);
    void chooseBootstrapFolder();
    void languageChosen();

    void showRestartWarning(bool fPersistent = false);
    void clearStatusLabel();
    void updateProxyValidationState();
    /* query the networks, for which the default proxy is used */
    void updateDefaultProxyNets();

Q_SIGNALS:
    void proxyIpChecks(QValidatedLineEdit *pUiProxyIp, int nProxyPort);

private:
    Ui::OptionsDialog *ui;
    OptionsModel *model;
    QDataWidgetMapper *mapper;
    QLabel *languageNotice;
    QPushButton *languageRestart;
    QCheckBox *bootstrapUse;
    QPlainTextEdit *bootstrapUrls;
    bool walletEnabled;
    bool embedded;
    QVBoxLayout *extraColumn;
    QDialog *networkDialog;
    QLabel *networkStatus;
    QPushButton *networkSave;
};

#endif // AOKCHAIN_QT_OPTIONSDIALOG_H
