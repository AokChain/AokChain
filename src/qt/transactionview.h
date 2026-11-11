// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_TRANSACTIONVIEW_H
#define AOKCHAIN_QT_TRANSACTIONVIEW_H

#include "guiutil.h"

#include <QList>
#include <QSet>
#include <QWidget>
#include <QKeyEvent>

class PlatformStyle;
class TransactionFilterProxy;
class TransactionPageProxy;
class WalletModel;

QT_BEGIN_NAMESPACE
class QComboBox;
class QDateTimeEdit;
class QFrame;
class QLineEdit;
class QMenu;
class QButtonGroup;
class QModelIndex;
class QPushButton;
class QLabel;
class QTimer;
class QSignalMapper;
class QTableView;
QT_END_NAMESPACE

/** Widget showing the transaction list for a wallet, including a filter row.
    Using the filter row, the user can view or export a subset of the transactions.
  */
class TransactionView : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionView(const PlatformStyle *platformStyle, QWidget *parent = 0);

    void setModel(WalletModel *model);
    void showTokens();

    // Date ranges for filter
    enum DateEnum
    {
        All,
        Today,
        ThisWeek,
        ThisMonth,
        LastMonth,
        ThisYear,
        PastYear,
        Range
    };

    enum ColumnWidths {
        STATUS_COLUMN_WIDTH = 132,
        WATCHONLY_COLUMN_WIDTH = 23,
        DATE_COLUMN_WIDTH = 150,
        TYPE_COLUMN_WIDTH = 52,
        TOKEN_NAME_MINIMUM_COLUMN_WIDTH = 200,
        AMOUNT_COLUMN_WIDTH = 270,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 150,
        MINIMUM_COLUMN_WIDTH = 23,
        ROW_HEIGHT = 56
    };

private:
    WalletModel *model;
    TransactionFilterProxy *transactionProxyModel;
    QTableView *transactionView;

    QComboBox *dateWidget;
    QComboBox *typeWidget;
    QButtonGroup *typeButtons;
    QComboBox *watchOnlyWidget;
    QLineEdit *addressWidget;
    QLineEdit *amountWidget;
    QComboBox *assetWidget;
    QTimer *assetListTimer;
    QSet<QString> assetRoots;
    int assetRowsSeen;
    // paging
    TransactionPageProxy *pageProxy;
    QLabel *pageCountLabel;
    QLabel *pageLabel;
    QPushButton *pageFirst;
    QPushButton *pagePrevious;
    QPushButton *pageNext;
    QPushButton *pageLast;
    QComboBox *pageSizeWidget;
    QString selectedHash;
    void gotoPage(int page);
    void firstPage();
    void updatePageControls();
    void rememberSelection();
    void restoreSelection();
    bool assetRescan;
    void noteAssetRow(int row);

    bool showingTokens;

    QMenu *contextMenu;
    QSignalMapper *mapperThirdPartyTxUrls;

    QFrame *dateRangeWidget;
    QDateTimeEdit *dateFrom;
    QDateTimeEdit *dateTo;
    QAction *abandonAction;
    QAction *bumpFeeAction;

    QWidget *createDateRangeWidget();

    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;

    virtual void resizeEvent(QResizeEvent* event);

    bool eventFilter(QObject *obj, QEvent *event);

private Q_SLOTS:
    void contextualMenu(const QPoint &);
    void dateRangeChanged();
    void showDetails();
    void copyAddress();
    void editLabel();
    void copyLabel();
    void copyTokenName();
    void copyAmount();
    void copyTxID();
    void copyTxHex();
    void copyTxPlainText();
    void openThirdPartyTxUrl(QString url);
    void updateWatchOnlyColumn(bool fHaveWatchOnly);
    void abandonTx();
    void showInExplorer();
    void openExplorer();
    void bumpFee();

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);

    /**  Fired when a message should be reported to the user */
    void message(const QString &title, const QString &message, unsigned int style);

    /** Look a transaction up in the explorer; an empty id just opens the explorer */
    void explorerRequested(const QString &txid);

public Q_SLOTS:
    void chooseDate(int idx);
    void chooseType(int idx);
    void chooseTypeSegment(int id);
    void chooseWatchonly(int idx);
    void changedAmount();
    void changedTokenName();
    void updateAssetList();
    void pageSizeChosen(int idx);
    void assetRowsInserted(const QModelIndex &parent, int first, int last);
    void changedPrefix();
    void exportClicked();
    void focusTransaction(const QModelIndex&);

};

#endif // AOKCHAIN_QT_TRANSACTIONVIEW_H
