// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_PAGEWIDGETS_H
#define AOKCHAIN_QT_PAGEWIDGETS_H

#include <QAbstractButton>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class HeldTokenIndex;
class QLabel;
class QVBoxLayout;

/**
 * Shows an amount the way all pages do: fixed-width font, the fraction (the
 * point and the digits after it) at 60 % of the size of the integer part on
 * the same base line, optionally followed by a unit. See GUIUtil::paintAmount().
 */
class AmountLabel : public QWidget
{
public:
    explicit AmountLabel(int pixelSize = 14, int weight = QFont::Medium, QWidget *parent = nullptr);

    void setAmount(const QString& amount, const QString& unit = QString());
    void setColor(const QColor& color);
    void setUnitColor(const QColor& color);
    void setAlignment(Qt::Alignment alignment);
    QString amount() const { return text; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QFont amountFont() const;
    QFont unitFont() const;

    int px;
    int weight;
    QString text;
    QString unit;
    QColor color;
    QColor unitColor;
    Qt::Alignment alignment;
};

/**
 * A choice among a few: a segment of a segmented control or one of the fee
 * choices, with an optional second line. It paints itself, so that its colors
 * do not depend on the precedence of style sheet rules.
 */
class ChoiceButton : public QAbstractButton
{
public:
    enum Look { Segment, Box };
    ChoiceButton(const QString& text, Look look, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Look look;
    bool hovered;
};

/** A small rounded label that can be clicked and, optionally, removed with its "x". */
class ChipButton : public QWidget
{
    Q_OBJECT

public:
    explicit ChipButton(QWidget *parent = nullptr);
    void setText(const QString& text);
    QSize sizeHint() const override;

Q_SIGNALS:
    void clicked();
    void removeClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QString text;
};

/** The summary next to a send form: a large total, rows below it, a note and the send button. */
class SummaryCard : public QFrame
{
public:
    struct Row {
        QString caption;
        QString value;      //!< an amount when isAmount, text otherwise
        QString unit;
        bool isAmount;
        bool mono;          //!< text in the fixed-width font (addresses)
        bool alert;         //!< in the error color
        QString toolTip;
        Row(const QString& _caption = QString(), const QString& _value = QString(), const QString& _unit = QString(), bool _isAmount = true) :
            caption(_caption), value(_value), unit(_unit), isAmount(_isAmount), mono(false), alert(false) {}
    };

    explicit SummaryCard(const QString& title, QWidget *parent = nullptr);
    void setTotal(const QString& caption, const QString& amount, const QString& unit);
    void setRows(const QList<Row>& rows);
    void setNote(const QString& note);
    /** Widgets at the bottom: the send button */
    void addFooterWidget(QWidget *widget);

private:
    QLabel *totalCaption;
    AmountLabel *total;
    QVBoxLayout *rowLayout;
    QLabel *noteLabel;
    QVBoxLayout *footer;
};

/**
 * A strip of figures in a card: a caption and a value per cell. The chain
 * status at the top of the overview and of the explorer.
 */
class StatsStrip : public QFrame
{
    Q_OBJECT

public:
    explicit StatsStrip(QWidget *parent = nullptr);
    /** Add a cell; a clickable one shows a chevron and emits cellClicked() */
    int addCell(const QString& caption, bool clickable = false);
    void setValue(int cell, const QString& amount, const QString& unit = QString());
    void setCompact(bool compact);

Q_SIGNALS:
    void cellClicked(int cell);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    QList<AmountLabel*> values;
    QList<QWidget*> cells;
};

/**
 * The root tokens of the wallet as round logo buttons, at most 20; a "+"
 * opens a list with a search field, where sub tokens and unique tokens are
 * found as well. Sub and unique tokens are never buttons of their own: a
 * wallet can hold tens of thousands of them.
 */
class TokenPicker : public QWidget
{
    Q_OBJECT

public:
    explicit TokenPicker(QWidget *parent = nullptr);
    void setIndex(HeldTokenIndex *index);
    void setCurrent(const QString& name);
    QString current() const { return selected; }
    bool isEmpty() const;
    /** Open the list of the sub and unique tokens of a root and choose from it */
    void chooseBelow(const QString& root);

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

Q_SIGNALS:
    void tokenSelected(const QString& name);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private Q_SLOTS:
    void indexChanged();

private:
    QStringList shown() const;
    QRect itemRect(int index, int width) const;
    int itemAt(const QPoint& pos) const;

    HeldTokenIndex *index;
    QString selected;
    int hover;
};

namespace PageDialogs
{
    /** Ask for the label of an address. Returns false when cancelled. */
    bool askLabel(QWidget *parent, QString& label);
    /** Ask until when coins or tokens stay locked for the recipient. Returns false when cancelled. */
    bool askLockTime(QWidget *parent, QDateTime& until);
}

#endif // AOKCHAIN_QT_PAGEWIDGETS_H
