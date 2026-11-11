// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_RECEIVECOINSDIALOG_H
#define AOKCHAIN_QT_RECEIVECOINSDIALOG_H

#include "guiutil.h"

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>
#include <QVariant>

class PlatformStyle;
class SendCoinsRecipient;
class WalletModel;

namespace Ui {
    class ReceiveCoinsDialog;
}

QT_BEGIN_NAMESPACE
class QLabel;
class QModelIndex;
class QPushButton;
QT_END_NAMESPACE

/** Dialog for requesting payment of AOK */
class ReceiveCoinsDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        DATE_COLUMN_WIDTH = 170,
        LABEL_COLUMN_WIDTH = 120,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 180,
        MINIMUM_COLUMN_WIDTH = 130
    };

    explicit ReceiveCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ReceiveCoinsDialog();

    void setModel(WalletModel *model);
    void setupRequestFrame(const PlatformStyle *platformStyle);
    void setupHistoryFrame(const PlatformStyle *platformStyle);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();

protected:
    virtual void keyPressEvent(QKeyEvent *event);

private:
    Ui::ReceiveCoinsDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;

    // the request that is shown on the page with its QR code
    QLabel *qrImage;
    QLabel *qrAddress;
    QLabel *qrDetails;
    QLabel *qrPlaceholder;
    QPushButton *copyQrButton;
    QPushButton *copyAddressButton;
    QString shownAddress;
    void buildLayout();
    void showRequest(const SendCoinsRecipient &info);

    QModelIndex selectedRow();
    void copyColumnToClipboard(int column);
    virtual void resizeEvent(QResizeEvent *event);

private Q_SLOTS:
    void on_receiveButton_clicked();
    void on_showRequestButton_clicked();
    void on_removeRequestButton_clicked();
    void on_recentRequestsView_doubleClicked(const QModelIndex &index);
    void recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void updateDisplayUnit();
    void showMenu(const QPoint &point);
    void copyURI();
    void copyLabel();
    void copyMessage();
    void copyAmount();
    void copyQrImage();
    void copyShownAddress();
};

#endif // AOKCHAIN_QT_RECEIVECOINSDIALOG_H
