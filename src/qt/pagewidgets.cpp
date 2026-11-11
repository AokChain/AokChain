// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pagewidgets.h"

#include "guiutil.h"
#include "tokenlists.h"

#include <QApplication>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolTip>
#include <QVBoxLayout>

AmountLabel::AmountLabel(int pixelSize, int _weight, QWidget *parent) :
    QWidget(parent), px(pixelSize), weight(_weight), alignment(Qt::AlignLeft | Qt::AlignVCenter)
{
    color = GUIUtil::theme().text;
    unitColor = GUIUtil::theme().textSecondary;
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QFont AmountLabel::amountFont() const
{
    QFont font = GUIUtil::monoFont(px, weight);
    if (px >= 24)
        font.setLetterSpacing(QFont::AbsoluteSpacing, -px / 40.0);
    return font;
}

QFont AmountLabel::unitFont() const
{
    return GUIUtil::appFont(qMax(11, qRound(px * 0.6)), px >= 24 ? QFont::DemiBold : QFont::Medium);
}

void AmountLabel::setAmount(const QString& _amount, const QString& _unit)
{
    if (text == _amount && unit == _unit)
        return;
    text = _amount;
    unit = _unit;
    updateGeometry();
    update();
}

void AmountLabel::setColor(const QColor& _color)
{
    color = _color;
    update();
}

void AmountLabel::setUnitColor(const QColor& _color)
{
    unitColor = _color;
    update();
}

void AmountLabel::setAlignment(Qt::Alignment _alignment)
{
    alignment = _alignment;
    update();
}

QSize AmountLabel::sizeHint() const
{
    int w = GUIUtil::amountWidth(text, amountFont());
    if (!unit.isEmpty())
        w += qMax(4, px / 4) + QFontMetrics(unitFont()).horizontalAdvance(unit);
    return QSize(w + 2, QFontMetrics(amountFont()).height() + 2);
}

void AmountLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const QFont font = amountFont();
    const QFontMetrics fm(font);
    const int amountW = GUIUtil::amountWidth(text, font);
    const int gap = unit.isEmpty() ? 0 : qMax(4, px / 4);
    const int unitW = unit.isEmpty() ? 0 : QFontMetrics(unitFont()).horizontalAdvance(unit);
    int x = 0;
    if (alignment & Qt::AlignRight)
        x = width() - (amountW + gap + unitW);
    else if (alignment & Qt::AlignHCenter)
        x = (width() - (amountW + gap + unitW)) / 2;
    GUIUtil::paintAmount(&p, QRect(x, 0, amountW, height()), text, font, color, Qt::AlignLeft | Qt::AlignVCenter);
    if (!unit.isEmpty()) {
        const int baseline = (height() - fm.height()) / 2 + fm.ascent();
        p.setFont(unitFont());
        p.setPen(unitColor);
        p.drawText(x + amountW + gap, baseline, unit);
    }
}

ChoiceButton::ChoiceButton(const QString& text, Look _look, QWidget *parent) :
    QAbstractButton(parent), look(_look), hovered(false)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(look == Box ? QSizePolicy::Expanding : QSizePolicy::Minimum, QSizePolicy::Fixed);
}

QSize ChoiceButton::sizeHint() const
{
    const QStringList lines = text().split('\n');
    int w = 0;
    const QFontMetrics fm(GUIUtil::appFont(13, QFont::DemiBold));
    for (const QString& line : lines)
        w = qMax(w, fm.horizontalAdvance(line));
    return look == Box ? QSize(w + 24, 56) : QSize(w + 32, 40);
}

void ChoiceButton::enterEvent(QEvent *event)
{
    hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ChoiceButton::leaveEvent(QEvent *event)
{
    hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ChoiceButton::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool on = isChecked();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = look == Box ? 12 : 9;
    if (look == Box) {
        p.setPen(QPen(on || hovered || hasFocus() ? t.accent : t.borderStrong, 1));
        p.setBrush(on ? t.accentSoft : t.surface);
        p.drawRoundedRect(r, radius, radius);
    } else if (on || hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(on ? t.accentSoft : t.surfaceAlt);
        p.drawRoundedRect(r, radius, radius);
    }
    // dark text on the light tint when chosen, the label color otherwise
    const QColor textColor = !isEnabled() ? t.textDisabled : (on ? t.accent : t.label);
    const QStringList lines = text().split('\n');
    p.setPen(textColor);
    if (lines.size() == 1) {
        p.setFont(GUIUtil::appFont(13, on ? QFont::DemiBold : QFont::Medium));
        p.drawText(rect(), Qt::AlignCenter, lines[0]);
    } else {
        p.setFont(GUIUtil::appFont(13, on ? QFont::DemiBold : QFont::Medium));
        p.drawText(QRect(0, height() / 2 - 20, width(), 20), Qt::AlignCenter, lines[0]);
        p.setFont(GUIUtil::appFont(12));
        p.drawText(QRect(0, height() / 2, width(), 18), Qt::AlignCenter, lines[1]);
    }
}

ChipButton::ChipButton(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ChipButton::setText(const QString& _text)
{
    text = _text;
    updateGeometry();
    update();
}

QSize ChipButton::sizeHint() const
{
    return QSize(QFontMetrics(GUIUtil::appFont(12, QFont::Medium)).horizontalAdvance(text) + 12 + 26, 26);
}

void ChipButton::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(t.accentSoft);
    p.drawRoundedRect(rect(), 13, 13);
    p.setPen(t.accent);
    p.setFont(GUIUtil::appFont(12, QFont::Medium));
    p.drawText(QRect(12, 0, width() - 12 - 24, height()), Qt::AlignLeft | Qt::AlignVCenter, text);
    p.drawPixmap(width() - 21, (height() - 12) / 2, GUIUtil::lineIcon("remove", 12, t.accent, 2.4));
}

void ChipButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (!rect().contains(event->pos()))
        return;
    if (event->pos().x() >= width() - 26)
        Q_EMIT removeClicked();
    else
        Q_EMIT clicked();
}

SummaryCard::SummaryCard(const QString& title, QWidget *parent) : QFrame(parent)
{
    setProperty("role", "card");
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(0);

    QLabel *titleLabel = new QLabel(title, this);
    titleLabel->setProperty("role", "title");
    titleLabel->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    layout->addWidget(titleLabel);
    layout->addSpacing(18);

    // the total stands out
    QFrame *totalBox = new QFrame(this);
    totalBox->setProperty("role", "tile");
    QVBoxLayout *totalLayout = new QVBoxLayout(totalBox);
    totalLayout->setContentsMargins(16, 14, 16, 14);
    totalLayout->setSpacing(2);
    totalCaption = new QLabel(totalBox);
    totalCaption->setProperty("role", "caption");
    totalCaption->setFont(GUIUtil::appFont(12));
    total = new AmountLabel(28, QFont::Bold, totalBox);
    total->setUnitColor(GUIUtil::theme().accent);
    totalLayout->addWidget(totalCaption);
    totalLayout->addWidget(total);
    layout->addWidget(totalBox);
    layout->addSpacing(14);

    rowLayout = new QVBoxLayout();
    rowLayout->setSpacing(0);
    layout->addLayout(rowLayout);
    layout->addStretch(1);

    noteLabel = new QLabel(this);
    noteLabel->setProperty("role", "caption");
    noteLabel->setFont(GUIUtil::appFont(12));
    noteLabel->setWordWrap(true);
    noteLabel->hide();
    layout->addWidget(noteLabel);
    layout->addSpacing(12);
    footer = new QVBoxLayout();
    footer->setSpacing(8);
    layout->addLayout(footer);
}

void SummaryCard::setTotal(const QString& caption, const QString& amount, const QString& unit)
{
    totalCaption->setText(caption);
    total->setAmount(amount, unit);
}

void SummaryCard::setRows(const QList<Row>& rows)
{
    while (QLayoutItem *item = rowLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    const GUIUtil::Theme& t = GUIUtil::theme();
    bool first = true;
    for (const Row& row : rows) {
        QWidget *line = new QWidget(this);
        QVBoxLayout *outer = new QVBoxLayout(line);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        if (!first) {
            QFrame *separator = new QFrame(line);
            separator->setProperty("role", "hline");
            separator->setFixedHeight(1);
            outer->addWidget(separator);
        }
        first = false;
        QHBoxLayout *layout = new QHBoxLayout();
        layout->setContentsMargins(0, 9, 0, 9);
        layout->setSpacing(12);
        QLabel *caption = new QLabel(row.caption, line);
        caption->setProperty("role", "secondary");
        caption->setFont(GUIUtil::appFont(13));
        layout->addWidget(caption);
        layout->addStretch(1);
        if (row.isAmount) {
            AmountLabel *value = new AmountLabel(15, QFont::Medium, line);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            value->setAmount(row.value, row.unit);
            if (row.alert)
                value->setColor(t.error);
            layout->addWidget(value);
        } else {
            QLabel *value = new QLabel(row.value, line);
            value->setFont(row.mono ? GUIUtil::monoFont(13, QFont::Medium) : GUIUtil::appFont(13, QFont::Medium));
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (row.alert)
                value->setStyleSheet(QString("QLabel { color: %1; }").arg(t.error.name()));
            layout->addWidget(value);
        }
        if (!row.toolTip.isEmpty())
            line->setToolTip(row.toolTip);
        outer->addLayout(layout);
        rowLayout->addWidget(line);
    }
}

void SummaryCard::setNote(const QString& note)
{
    noteLabel->setText(note);
    noteLabel->setVisible(!note.isEmpty());
}

void SummaryCard::addFooterWidget(QWidget *widget)
{
    footer->addWidget(widget);
}

StatsStrip::StatsStrip(QWidget *parent) : QFrame(parent)
{
    setProperty("role", "card");
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

int StatsStrip::addCell(const QString& caption, bool clickable)
{
    QHBoxLayout *strip = static_cast<QHBoxLayout*>(layout());
    if (!cells.isEmpty()) {
        QFrame *line = new QFrame(this);
        line->setProperty("role", "vline");
        line->setFixedWidth(1);
        strip->addWidget(line);
    }
    QWidget *cell = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(2);
    QHBoxLayout *captionRow = new QHBoxLayout();
    captionRow->setSpacing(4);
    QLabel *label = new QLabel(caption, cell);
    label->setProperty("role", "caption");
    label->setFont(GUIUtil::appFont(12));
    captionRow->addWidget(label);
    captionRow->addStretch();
    if (clickable) {
        QLabel *chevron = new QLabel(cell);
        chevron->setPixmap(GUIUtil::lineIcon("chevron-right", 12, GUIUtil::theme().accent, 2.6));
        captionRow->addWidget(chevron);
        cell->setCursor(Qt::PointingHandCursor);
        cell->setProperty("cell", cells.size());
        cell->installEventFilter(this);
    }
    layout->addLayout(captionRow);
    AmountLabel *value = new AmountLabel(18, QFont::DemiBold, cell);
    value->setAmount(QString::fromUtf8("–"));
    layout->addWidget(value);
    strip->addWidget(cell, 1);
    cells << cell;
    values << value;
    return cells.size() - 1;
}

void StatsStrip::setValue(int cell, const QString& amount, const QString& unit)
{
    if (cell >= 0 && cell < values.size())
        values[cell]->setAmount(amount, unit);
}

void StatsStrip::setCompact(bool compact)
{
    for (QWidget *cell : cells)
        cell->layout()->setContentsMargins(18, compact ? 9 : 14, 18, compact ? 9 : 14);
}

bool StatsStrip::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease && object->property("cell").isValid())
        Q_EMIT cellClicked(object->property("cell").toInt());
    return QFrame::eventFilter(object, event);
}

static const int PICK = 44;     // size of a round token button
static const int PICK_GAP = 8;
static const int PICK_MAX = 20; // root tokens shown without the list

TokenPicker::TokenPicker(QWidget *parent) : QWidget(parent), index(nullptr), hover(-1)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void TokenPicker::setIndex(HeldTokenIndex *_index)
{
    index = _index;
    connect(index, SIGNAL(changed()), this, SLOT(indexChanged()));
    indexChanged();
}

void TokenPicker::indexChanged()
{
    // the chosen token may be gone
    HeldToken token;
    if (!selected.isEmpty() && !(index->find(selected, &token) && token.held)) {
        selected.clear();
        Q_EMIT tokenSelected(selected);
    }
    updateGeometry();
    update();
}

bool TokenPicker::isEmpty() const
{
    return !index || index->roots().isEmpty();
}

void TokenPicker::setCurrent(const QString& name)
{
    HeldToken token;
    const QString next = index && index->find(name, &token) && token.held ? name : QString();
    if (selected == next)
        return;
    selected = next;
    update();
    Q_EMIT tokenSelected(selected);
}

/** The first root tokens; the root of the chosen token takes the last place when it is not among them. */
QStringList TokenPicker::shown() const
{
    QStringList list;
    if (!index)
        return list;
    for (const RootToken& root : index->roots()) {
        if (list.size() >= PICK_MAX)
            break;
        list << root.name;
    }
    const QString selectedRoot = TokenNames::root(selected);
    if (!selected.isEmpty() && !list.contains(selectedRoot)) {
        if (list.size() >= PICK_MAX)
            list.removeLast();
        list << selectedRoot;
    }
    return list;
}

QRect TokenPicker::itemRect(int item, int width) const
{
    const int perRow = qMax(1, (width + PICK_GAP) / (PICK + PICK_GAP));
    return QRect((item % perRow) * (PICK + PICK_GAP), (item / perRow) * (PICK + PICK_GAP), PICK, PICK);
}

int TokenPicker::heightForWidth(int width) const
{
    const int count = shown().size() + 1; // and the "+"
    const int perRow = qMax(1, (width + PICK_GAP) / (PICK + PICK_GAP));
    const int rows = (count + perRow - 1) / perRow;
    return rows * PICK + (rows - 1) * PICK_GAP;
}

QSize TokenPicker::sizeHint() const
{
    return QSize(400, heightForWidth(width() > 0 ? width() : 400));
}

int TokenPicker::itemAt(const QPoint& pos) const
{
    const int count = shown().size() + 1;
    for (int i = 0; i < count; ++i)
        if (itemRect(i, width()).contains(pos))
            return i;
    return -1;
}

void TokenPicker::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QStringList list = shown();
    const QString selectedRoot = TokenNames::root(selected);
    for (int i = 0; i <= list.size(); ++i) {
        const QRect r = itemRect(i, width());
        const QRectF inner = QRectF(r).adjusted(4, 4, -4, -4);
        if (i == list.size()) {
            // "+": the list with the search field
            p.setPen(QPen(hover == i ? t.accent : t.borderStrong, 1, Qt::DashLine));
            p.setBrush(t.surface);
            p.drawEllipse(inner);
            p.drawPixmap(r.center().x() - 7, r.center().y() - 7, GUIUtil::lineIcon("plus", 16, hover == i ? t.accent : t.label, 2.2));
            continue;
        }
        const QString& name = list[i];
        if (GUIUtil::hasTokenLogo(name)) {
            p.drawPixmap(inner.topLeft(), GUIUtil::tokenLogo(name, PICK - 8));
        } else {
            // no logo: the name itself
            p.setPen(Qt::NoPen);
            p.setBrush(t.accentSoft);
            p.drawEllipse(inner);
            p.setPen(t.accent);
            QFont font = GUIUtil::appFont(name.size() <= 3 ? 11 : 9, QFont::DemiBold);
            p.setFont(font);
            p.drawText(inner, Qt::AlignCenter, QFontMetrics(font).elidedText(name, Qt::ElideRight, PICK - 12));
        }
        const bool chosen = !selected.isEmpty() && name == selectedRoot;
        if (chosen || hover == i) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(chosen ? t.accent : t.borderStrong, chosen ? 2.5 : 1.5));
            p.drawEllipse(QRectF(r).adjusted(1.25, 1.25, -1.25, -1.25));
        }
    }
}

void TokenPicker::mouseMoveEvent(QMouseEvent *event)
{
    const int item = itemAt(event->pos());
    if (item != hover) {
        hover = item;
        setCursor(item >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TokenPicker::leaveEvent(QEvent*)
{
    hover = -1;
    update();
}

void TokenPicker::mouseReleaseEvent(QMouseEvent *event)
{
    const QStringList list = shown();
    const int item = itemAt(event->pos());
    if (item < 0 || !index)
        return;
    if (item == list.size()) {
        TokenListDialog dialog(index, QString(), TokenListDialog::Choose, this);
        if (dialog.exec() == QDialog::Accepted)
            setCurrent(dialog.chosen());
        return;
    }
    const RootToken *root = index->findRoot(list[item]);
    if (root && root->held)
        setCurrent(root->name);
    else if (root && root->children > 0)
        chooseBelow(root->name); // only tokens below this root are held
}

void TokenPicker::chooseBelow(const QString& root)
{
    if (!index)
        return;
    TokenListDialog dialog(index, root, TokenListDialog::Choose, this);
    if (dialog.exec() == QDialog::Accepted)
        setCurrent(dialog.chosen());
}

bool TokenPicker::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *help = static_cast<QHelpEvent*>(event);
        const QStringList list = shown();
        const int item = itemAt(help->pos());
        if (item >= 0 && item < list.size())
            QToolTip::showText(help->globalPos(), list[item], this);
        else if (item == list.size())
            QToolTip::showText(help->globalPos(), tr("Search all tokens of the wallet, also sub tokens and unique tokens"), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

bool PageDialogs::askLabel(QWidget *parent, QString& label)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QApplication::translate("PageDialogs", "Label"));
    dialog.setMinimumWidth(380);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    QLabel *text = new QLabel(QApplication::translate("PageDialogs", "A label for this address. It is saved in your address book and shown instead of the address in your lists. It is not sent to anyone."), &dialog);
    text->setWordWrap(true);
    text->setProperty("role", "secondary");
    layout->addWidget(text);
    QLineEdit *edit = new QLineEdit(label, &dialog);
    edit->setPlaceholderText(QApplication::translate("PageDialogs", "For example: Minsu, exchange deposit"));
    edit->setMinimumHeight(40);
    layout->addWidget(edit);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    edit->setFocus();
    edit->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    label = edit->text().trimmed();
    return true;
}

bool PageDialogs::askLockTime(QWidget *parent, QDateTime& until)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QApplication::translate("PageDialogs", "Send with a lock"));
    dialog.setMinimumWidth(420);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    QLabel *text = new QLabel(QApplication::translate("PageDialogs", "The recipient receives the amount now but cannot spend it before the time you choose here."), &dialog);
    text->setWordWrap(true);
    text->setProperty("role", "secondary");
    layout->addWidget(text);

    const QDateTime now = QDateTime::currentDateTime();
    QDateTimeEdit *edit = new QDateTimeEdit(until.isValid() && until > now ? until : now.addMonths(1), &dialog);
    edit->setDisplayFormat("yyyy-MM-dd HH:mm");
    edit->setCalendarPopup(true);
    edit->setMinimumDateTime(now);
    edit->setFont(GUIUtil::monoFont(14));
    edit->setMinimumHeight(40);
    layout->addWidget(edit);

    QHBoxLayout *quick = new QHBoxLayout();
    quick->setSpacing(8);
    const std::pair<QString, int> choices[] = {
        {QApplication::translate("PageDialogs", "1 month"), 1},
        {QApplication::translate("PageDialogs", "3 months"), 3},
        {QApplication::translate("PageDialogs", "6 months"), 6},
        {QApplication::translate("PageDialogs", "1 year"), 12},
    };
    for (const std::pair<QString, int>& choice : choices) {
        QPushButton *button = new QPushButton(choice.first, &dialog);
        button->setAutoDefault(false);
        button->setProperty("look", "soft");
        const int months = choice.second;
        QObject::connect(button, &QPushButton::clicked, [edit, months] { edit->setDateTime(QDateTime::currentDateTime().addMonths(months)); });
        quick->addWidget(button);
    }
    quick->addStretch();
    layout->addLayout(quick);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    until = edit->dateTime();
    return true;
}
