// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_GUIUTIL_H
#define AOKCHAIN_QT_GUIUTIL_H

#include "amount.h"
#include "fs.h"

#include <QEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QObject>
#include <QProgressBar>
#include <QColor>
#include <QFont>
#include <QList>
#include <QStyledItemDelegate>
#include <QString>
#include <QTableView>
#include <QLabel>

class QValidatedLineEdit;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QApplication;
class QAbstractItemView;
class QDateTime;
class QFont;
class QPainter;
class QPixmap;
class QLineEdit;
class QUrl;
class QWidget;
QT_END_NAMESPACE

/** Utility functions used by the AokChain Qt UI.
 */
namespace GUIUtil
{
    /** Colors of the application theme. There is one (light) theme. */
    struct Theme {
        QColor bg;            //!< window background
        QColor surface;       //!< cards, inputs, tables
        QColor surfaceAlt;    //!< secondary buttons, hover, read-only fields
        QColor border;        //!< card borders
        QColor borderStrong;  //!< input and button borders
        QColor line;          //!< separators between rows
        QColor text;
        QColor textSecondary;
        QColor label;         //!< field labels, secondary button text
        QColor textDisabled;
        QColor fill;          //!< primary buttons
        QColor fillHover;
        QColor fillPressed;
        QColor onFill;        //!< text on the primary fill
        QColor accent;        //!< accent text, focus borders
        QColor accentHover;
        QColor accentPressed;
        QColor accentSoft;    //!< selection and hover tint
        QColor onAccent;      //!< text on the accent color
        QColor positive;      //!< received amounts
        QColor positiveSoft;
        QColor negative;      //!< sent amounts
        QColor negativeSoft;
        QColor ok;            //!< confirmed, valid
        QColor okSoft;
        QColor error;         //!< invalid input, failed transactions
        QColor errorSoft;
        QColor warnBg;
        QColor warnText;
        QColor warnBorder;
        QColor chip;
        QColor chipText;
        // side bar
        QColor sideBg;
        QColor sideText;
        QColor sideStrong;
        QColor sideOnBg;      //!< active navigation item
        QColor sideCard;
        QColor sideTrack;
        QColor sideBar;
        QColor sideOff;       //!< switch that is off
    };
    /** The theme. */
    const Theme& theme();
    /** Register the bundled fonts (Noto Sans, Noto Sans KR, Noto Sans Mono). Call once, before any widget is created. */
    void loadAppFonts();
    /** The application font at a pixel size and weight. */
    QFont appFont(int pixelSize = 13, int weight = QFont::Normal);
    /** The fixed-width font for amounts, addresses, transaction ids, dates in tables and block heights. */
    QFont monoFont(int pixelSize = 13, int weight = QFont::Normal);
    /** Apply style, palette, font and style sheet of the theme to the application. */
    void applyTheme(QApplication& app);
    /** Style sheet snippets that follow the theme. */
    QString labelColorStyle();
    QString invalidFieldStyle();
    QString validFieldStyle();
    /** Style sheet of a card: a panel on the surface color with a border and rounded corners. */
    QString cardFrameStyle(const QString& selector = ".QFrame");

    /** First and last four characters of an address ("Kj6i…qVgR"). Text that is not a valid address is returned as it is. */
    QString shortAddress(const QString& address);
    /** Shortened transaction id or block hash. */
    QString shortHash(const QString& hash, int head = 12, int tail = 8);
    /** Put thousands separators (commas) into the integer part of a formatted number. For display only. */
    QString withThousands(const QString& number);
    /** A line icon drawn in the given color. Names: overview, send, receive, history, tokens, explorer,
        create, manage, addressbook, paste, download, search, chevron, plus, remove, copy, lock. */
    QPixmap lineIcon(const QString& name, int size, const QColor& color, qreal strokeWidth = 2.0);
    /** The bundled logo of a token as a round pixmap; a letter avatar when there is none.
        The logo is looked up by the root name (before '/', '#' and '!'). */
    QPixmap tokenLogo(const QString& tokenName, int size);
    /** Paint the tile that shows the direction of a transaction: received (incoming arrow), sent
        (outgoing arrow) or a staking reward. */
    void paintDirectionTile(QPainter* painter, const QRect& tile, bool outgoing, bool reward = false);
    /** HTML that shows already escaped text in the fixed-width font. */
    QString monoHtml(const QString& escapedText);
    /** Tool tip text with the value in the fixed-width font, optionally with a caption line above. */
    QString monoToolTip(const QString& value, const QString& caption = QString());
    /** Date and time in the form used in tables: 2024-04-13 16:06 */
    QString isoDateTime(const QDateTime& dateTime, bool seconds = false);
    /** Is there a bundled logo for the token (looked up by its root name)? */
    bool hasTokenLogo(const QString& tokenName);
    /**
     * Paint an amount: the fraction (the point and the digits after it) at 60 % of the size of
     * the integer part, on the same base line. Every amount on the pages is painted this way.
     * Returns the width used.
     */
    int paintAmount(QPainter* painter, const QRect& rect, const QString& amount, const QFont& font, const QColor& color,
                    Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter);
    /** Width of an amount painted by paintAmount(). */
    int amountWidth(const QString& amount, const QFont& font);
    /** Paint a chip (small rounded label) at the position; returns its width. */
    int paintChip(QPainter* painter, int x, int centerY, const QString& text);
    /** Paint a rounded badge with its own colors, right- or left-aligned in the rectangle. */
    void paintBadge(QPainter* painter, const QRect& rect, const QString& text, const QColor& fg, const QColor& bg, Qt::Alignment align = Qt::AlignLeft);

    // Get the font for the sub labels
    QFont getSubLabelFont();
    QFont getSubLabelFontBolded();

    // Get the font for the main labels
    QFont getTopLabelFont();
    QFont getTopLabelFontBolded();
    QFont getTopLabelFont(int weight, int pxsize);

    // Create human-readable string from date
    QString dateTimeStr(const QDateTime &datetime);
    QString dateTimeStr(qint64 nTime);

    // Return a monospace font
    QFont fixedPitchFont();

    // Set up widgets for address and amounts
    void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent);
    void setupAmountWidget(QLineEdit *widget, QWidget *parent);

    // Parse "aokchain:" URI into recipient object, return true on successful parsing
    bool parseAokChainURI(const QUrl &uri, SendCoinsRecipient *out);
    bool parseAokChainURI(QString uri, SendCoinsRecipient *out);
    QString formatAokChainURI(const SendCoinsRecipient &info);

    // Returns true if given address+amount meets "dust" definition
    bool isDust(const QString& address, const CAmount& amount);

    // HTML escaping for rich text controls
    QString HtmlEscape(const QString& str, bool fMultiLine=false);
    QString HtmlEscape(const std::string& str, bool fMultiLine=false);

    /** Copy a field of the currently selected entry of a view to the clipboard. Does nothing if nothing
        is selected.
       @param[in] column  Data column to extract from the model
       @param[in] role    Data role to extract from the model
       @see  TransactionView::copyLabel, TransactionView::copyAmount, TransactionView::copyAddress
     */
    void copyEntryData(QAbstractItemView *view, int column, int role=Qt::EditRole);

    /** Return a field of the currently selected entry as a QString. Does nothing if nothing
        is selected.
       @param[in] column  Data column to extract from the model
       @see  TransactionView::copyLabel, TransactionView::copyAmount, TransactionView::copyAddress
     */
    QList<QModelIndex> getEntryData(QAbstractItemView *view, int column);

    void setClipboard(const QString& str);

    /** Get save filename, mimics QFileDialog::getSaveFileName, except that it appends a default suffix
        when no suffix is provided by the user.

      @param[in] parent  Parent window (or 0)
      @param[in] caption Window caption (or empty, for default)
      @param[in] dir     Starting directory (or empty, to default to documents directory)
      @param[in] filter  Filter specification such as "Comma Separated Files (*.csv)"
      @param[out] selectedSuffixOut  Pointer to return the suffix (file type) that was selected (or 0).
                  Can be useful when choosing the save file format based on suffix.
     */
    QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
        const QString &filter,
        QString *selectedSuffixOut);

    /** Get open filename, convenience wrapper for QFileDialog::getOpenFileName.

      @param[in] parent  Parent window (or 0)
      @param[in] caption Window caption (or empty, for default)
      @param[in] dir     Starting directory (or empty, to default to documents directory)
      @param[in] filter  Filter specification such as "Comma Separated Files (*.csv)"
      @param[out] selectedSuffixOut  Pointer to return the suffix (file type) that was selected (or 0).
                  Can be useful when choosing the save file format based on suffix.
     */
    QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
        const QString &filter,
        QString *selectedSuffixOut);

    /** Get connection type to call object slot in GUI thread with invokeMethod. The call will be blocking.

       @returns If called from the GUI thread, return a Qt::DirectConnection.
                If called from another thread, return a Qt::BlockingQueuedConnection.
    */
    Qt::ConnectionType blockingGUIThreadConnection();

    // Determine whether a widget is hidden behind other windows
    bool isObscured(QWidget *w);

    // Open debug.log
    void openDebugLogfile();

    // Open the config file
    bool openAokChainConf();

    // Replace invalid default fonts with known good ones
    void SubstituteFonts(const QString& language);

    // Concatenate a string given the painter, static text width, left side of rect, and right side of rect
    // and which side the concatenated string is on (default left)
    void concatenate(QPainter* painter, QString& strToCon, int static_width, int left_side, int right_size);

    /** Qt event filter that intercepts ToolTipChange events, and replaces the tooltip with a rich text
      representation if needed. This assures that Qt can word-wrap long tooltip messages.
      Tooltips longer than the provided size threshold (in characters) are wrapped.
     */
    class ToolTipToRichTextFilter : public QObject
    {
        Q_OBJECT

    public:
        explicit ToolTipToRichTextFilter(int size_threshold, QObject *parent = 0);

    protected:
        bool eventFilter(QObject *obj, QEvent *evt);

    private:
        int size_threshold;
    };

    /**
     * Makes a QTableView last column feel as if it was being resized from its left border.
     * Also makes sure the column widths are never larger than the table's viewport.
     * In Qt, all columns are resizable from the right, but it's not intuitive resizing the last column from the right.
     * Usually our second to last columns behave as if stretched, and when on strech mode, columns aren't resizable
     * interactively or programmatically.
     *
     * This helper object takes care of this issue.
     *
     */
    class TableViewLastColumnResizingFixer: public QObject
    {
        Q_OBJECT

        public:
            TableViewLastColumnResizingFixer(QTableView* table, int lastColMinimumWidth, int allColsMinimumWidth, QObject *parent);
            void stretchColumnWidth(int column);

        private:
            QTableView* tableView;
            int lastColumnMinimumWidth;
            int allColumnsMinimumWidth;
            int lastColumnIndex;
            int columnCount;
            int secondToLastColumnIndex;

            void adjustTableColumnsWidth();
            int getAvailableWidthForColumn(int column);
            int getColumnsWidth();
            void connectViewHeadersSignals();
            void disconnectViewHeadersSignals();
            void setViewHeaderResizeMode(int logicalIndex, QHeaderView::ResizeMode resizeMode);
            void resizeColumn(int nColumnIndex, int width);

        private Q_SLOTS:
            void on_sectionResized(int logicalIndex, int oldSize, int newSize);
            void on_geometriesChanged();
    };

    bool GetStartOnSystemStartup();
    bool SetStartOnSystemStartup(bool fAutoStart);

    /* Convert QString to OS specific boost path through UTF-8 */
    fs::path qstringToBoostPath(const QString &path);

    /* Convert OS specific boost path to QString through UTF-8 */
    QString boostPathToQString(const fs::path &path);

    /* Convert seconds into a QString with days, hours, mins, secs */
    QString formatDurationStr(int secs);

    /* Format CNodeStats.nServices bitmask into a user-readable string */
    QString formatServicesStr(quint64 mask);

    /* Format a CNodeCombinedStats.dPingTime into a user-readable string or display N/A, if 0*/
    QString formatPingTime(double dPingTime);

    /* Format a CNodeCombinedStats.nTimeOffset into a user-readable string. */
    QString formatTimeOffset(int64_t nTimeOffset);

    QString formatNiceTimeOffset(qint64 secs);

    QString formatBytes(uint64_t bytes);

    /** Item delegate that shows some columns (amounts, addresses, dates) in the fixed-width font. */
    class MonoColumnDelegate : public QStyledItemDelegate
    {
    public:
        MonoColumnDelegate(const QList<int>& columns, QObject *parent, const QList<int>& amounts = QList<int>()) :
            QStyledItemDelegate(parent), monoColumns(columns), amountColumns(amounts) {}
        /** Amount columns are painted with the smaller fraction, see paintAmount() */
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    protected:
        void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

    private:
        QList<int> monoColumns;
        QList<int> amountColumns;
    };

    class ClickableLabel : public QLabel
    {
        Q_OBJECT

    Q_SIGNALS:
        /** Emitted when the label is clicked. The relative mouse coordinates of the click are
         * passed to the signal.
         */
        void clicked(const QPoint& point);
    protected:
        void mouseReleaseEvent(QMouseEvent *event);
    };
    
    class ClickableProgressBar : public QProgressBar
    {
        Q_OBJECT
        
    Q_SIGNALS:
        /** Emitted when the progressbar is clicked. The relative mouse coordinates of the click are
         * passed to the signal.
         */
        void clicked(const QPoint& point);
    protected:
        void mouseReleaseEvent(QMouseEvent *event);
    };

#if defined(Q_OS_MAC) && QT_VERSION >= 0x050000
    // workaround for Qt OSX Bug:
    // https://bugreports.qt-project.org/browse/QTBUG-15631
    // QProgressBar uses around 10% CPU even when app is in background
    class ProgressBar : public ClickableProgressBar
    {
        bool event(QEvent *e) {
            return (e->type() != QEvent::StyleAnimationUpdate) ? QProgressBar::event(e) : false;
        }
    };
#else
    typedef ClickableProgressBar ProgressBar;
#endif

} // namespace GUIUtil

#endif // AOKCHAIN_QT_GUIUTIL_H
