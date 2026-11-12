// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokenlists.h"

#include "guiutil.h"
#include "tokenrecord.h"
#include "tokentablemodel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

/** Most rows a list shows; the user narrows the search to see others. */
static const int LIST_CAP = TokenLimits::LIST_ROWS;

QString TokenNames::root(const QString& name)
{
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name[i];
        if (c == '/' || c == '#' || c == '!')
            return name.left(i);
    }
    return name;
}

bool TokenNames::isChild(const QString& name)
{
    return name.contains('/') || name.contains('#');
}

bool TokenNames::belongsTo(const QString& name, const QString& root)
{
    if (!name.startsWith(root))
        return false;
    if (name.size() == root.size())
        return true;
    const QChar next = name[root.size()];
    return next == '/' || next == '#' || next == '!';
}

HeldTokenIndex::HeldTokenIndex(QObject *parent) : QObject(parent), source(nullptr)
{
}

HeldTokenIndex *HeldTokenIndex::forModel(TokenTableModel *model)
{
    if (!model)
        return nullptr;
    HeldTokenIndex *index = model->findChild<HeldTokenIndex*>(QString(), Qt::FindDirectChildrenOnly);
    if (!index) {
        index = new HeldTokenIndex(model);
        index->setSourceModel(model);
    }
    return index;
}

void HeldTokenIndex::setSourceModel(TokenTableModel *model)
{
    source = model;
    if (source) {
        connect(source, SIGNAL(layoutChanged()), this, SLOT(rebuild()));
        connect(source, SIGNAL(modelReset()), this, SLOT(rebuild()));
    }
    rebuild();
}

void HeldTokenIndex::rebuild()
{
    GUIUtil::LogTimer timer("held token index rebuild");
    all.clear();
    rootList.clear();
    if (source) {
        // straight from the records of the model: one pass, no QVariant per field
        const QList<TokenRecord>& records = source->records();
        all.reserve(records.size());
        for (const TokenRecord& record : records) {
            HeldToken token;
            token.name = QString::fromStdString(record.name);
            token.admin = record.fIsAdministrator;
            token.locked = record.fIsLocked;
            token.quantity = record.quantity;
            token.units = record.units;
            if (token.locked && token.name.endsWith(" (LOCKED)"))
                token.name.chop(9);
            token.held = !token.name.endsWith('!');
            if (token.held)
                token.amount = QString::fromStdString(record.formattedQuantity());
            else
                token.name.chop(1);
            all.push_back(token);
        }
        std::sort(all.begin(), all.end(), [](const HeldToken& a, const HeldToken& b) {
            return a.name != b.name ? a.name < b.name : a.locked < b.locked;
        });

        // one entry per root: the root itself when it is held, and how much is held below it
        for (const HeldToken& token : all) {
            const QString rootName = TokenNames::root(token.name);
            if (rootList.isEmpty() || rootList.last().name != rootName) {
                RootToken root;
                root.name = rootName;
                rootList.push_back(root);
            }
            RootToken& root = rootList.last();
            if (token.name == rootName) {
                if (token.locked) {
                    root.locked = true;
                } else {
                    root.held = token.held;
                    root.amount = token.amount;
                    root.admin = token.admin;
                }
            } else if (!token.locked) {
                ++root.children;
            }
        }
        // names are sorted by code point, so that "A/B" may come between "A" and "AB": merge again
        std::sort(rootList.begin(), rootList.end(), [](const RootToken& a, const RootToken& b) { return a.name < b.name; });
        QVector<RootToken> merged;
        for (const RootToken& root : rootList) {
            if (!merged.isEmpty() && merged.last().name == root.name) {
                RootToken& m = merged.last();
                m.children += root.children;
                m.locked = m.locked || root.locked;
                if (root.held || root.admin) {
                    m.held = root.held;
                    m.amount = root.amount;
                    m.admin = root.admin;
                }
            } else {
                merged.push_back(root);
            }
        }
        rootList = merged;
    }
    timer.setDetail(QString("%1 tokens, %2 roots").arg(all.size()).arg(rootList.size()));
    Q_EMIT changed();
}

const RootToken *HeldTokenIndex::findRoot(const QString& name) const
{
    QVector<RootToken>::const_iterator it = std::lower_bound(rootList.constBegin(), rootList.constEnd(), name,
        [](const RootToken& root, const QString& n) { return root.name < n; });
    return it != rootList.constEnd() && it->name == name ? &*it : nullptr;
}

QVector<HeldToken> HeldTokenIndex::children(const QString& root, const QString& text, int cap, bool *more) const
{
    QVector<HeldToken> result;
    if (more)
        *more = false;
    // everything below a root follows it in the sorted list, before the next name that is not below it
    QVector<HeldToken>::const_iterator it = std::lower_bound(all.constBegin(), all.constEnd(), root,
        [](const HeldToken& token, const QString& n) { return token.name < n; });
    for (; it != all.constEnd() && it->name.startsWith(root); ++it) {
        if (it->locked || it->name.size() == root.size() || !TokenNames::belongsTo(it->name, root))
            continue;
        if (!text.isEmpty() && !it->name.contains(text, Qt::CaseInsensitive))
            continue;
        if (result.size() >= cap) {
            if (more)
                *more = true;
            break;
        }
        result.push_back(*it);
    }
    return result;
}

QVector<HeldToken> HeldTokenIndex::search(const QString& text, int cap, bool *more) const
{
    QVector<HeldToken> result;
    if (more)
        *more = false;
    for (const HeldToken& token : all) {
        if (token.locked || !token.name.contains(text, Qt::CaseInsensitive))
            continue;
        if (result.size() >= cap) {
            if (more)
                *more = true;
            break;
        }
        result.push_back(token);
    }
    return result;
}

bool HeldTokenIndex::find(const QString& name, HeldToken *token) const
{
    QVector<HeldToken>::const_iterator it = std::lower_bound(all.constBegin(), all.constEnd(), name,
        [](const HeldToken& t, const QString& n) { return t.name < n; });
    if (it == all.constEnd() || it->name != name)
        return false;
    if (token)
        *token = *it;
    return true;
}

TokenListModel::TokenListModel(QObject *parent) : QAbstractListModel(parent)
{
}

void TokenListModel::setTokens(const QVector<HeldToken>& tokens)
{
    beginResetModel();
    rows.clear();
    rows.reserve(tokens.size());
    for (const HeldToken& token : tokens)
        rows.push_back(Row{token.name, token.amount, token.held, token.admin, token.locked, 0});
    endResetModel();
}

void TokenListModel::setRoots(const QVector<RootToken>& roots)
{
    beginResetModel();
    rows.clear();
    rows.reserve(roots.size());
    for (const RootToken& root : roots)
        rows.push_back(Row{root.name, root.amount, root.held, root.admin, root.locked, root.children});
    endResetModel();
}

int TokenListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : rows.size();
}

QVariant TokenListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rows.size())
        return QVariant();
    const Row& row = rows[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return row.name;
    case AmountRole: return row.amount;
    case HeldRole: return row.held;
    case AdminRole: return row.admin;
    case LockedRole: return row.locked;
    case ChildrenRole: return row.children;
    }
    return QVariant();
}

void TokenRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const GUIUtil::Theme& theme = GUIUtil::theme();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = option.rect.adjusted(8, 0, -12, 0);
    if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.accentSoft);
        painter->drawRoundedRect(option.rect.adjusted(0, 1, -4, -1), 8, 8);
    }
    const QString name = index.data(TokenListModel::NameRole).toString();
    const bool held = index.data(TokenListModel::HeldRole).toBool();
    painter->drawPixmap(r.left(), r.top() + (r.height() - 28) / 2, GUIUtil::tokenLogo(name, 28));

    // amount, right-aligned
    int amountWidth;
    if (held) {
        amountWidth = GUIUtil::paintAmount(painter, r, GUIUtil::withThousands(index.data(TokenListModel::AmountRole).toString()),
                                          GUIUtil::monoFont(15, QFont::DemiBold), theme.text);
    } else {
        const QString none = TokenListDialog::tr("None held");
        painter->setFont(GUIUtil::appFont(13));
        painter->setPen(theme.textSecondary);
        painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, none);
        amountWidth = painter->fontMetrics().horizontalAdvance(none);
    }

    // name and the chips that say what it is
    QStringList chips;
    if (name.contains('#'))
        chips << TokenListDialog::tr("Unique token");
    else if (name.contains('/'))
        chips << TokenListDialog::tr("Sub token");
    if (index.data(TokenListModel::AdminRole).toBool())
        chips << TokenListDialog::tr("Owner");
    const int children = index.data(TokenListModel::ChildrenRole).toInt();
    if (children > 0)
        chips << TokenListDialog::tr("%1 below").arg(children);
    painter->setFont(GUIUtil::appFont(11, QFont::DemiBold));
    int chipsWidth = 0;
    for (const QString& chip : chips)
        chipsWidth += painter->fontMetrics().horizontalAdvance(chip) + 16 + 6;
    const int left = r.left() + 28 + 10;
    painter->setFont(GUIUtil::appFont(14, QFont::DemiBold));
    painter->setPen(theme.text);
    const QString shown = painter->fontMetrics().elidedText(name, Qt::ElideMiddle, qMax(60, r.right() - left - amountWidth - chipsWidth - 16));
    painter->drawText(QRect(left, r.top(), r.width(), r.height()), Qt::AlignLeft | Qt::AlignVCenter, shown);
    int x = left + painter->fontMetrics().horizontalAdvance(shown) + 8;
    for (const QString& chip : chips)
        x += GUIUtil::paintChip(painter, x, r.center().y() + 1, chip) + 6;
    painter->restore();
}

QSize TokenRowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(100, 44);
}

TokenListDialog::TokenListDialog(HeldTokenIndex *_index, const QString& _root, Purpose purpose, QWidget *parent) :
    QDialog(parent), index(_index), root(_root)
{
    setWindowTitle(root.isEmpty() ? tr("Choose a token") : tr("Sub tokens of %1").arg(root));
    resize(520, 560);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 16);
    layout->setSpacing(10);

    if (!root.isEmpty()) {
        QHBoxLayout *header = new QHBoxLayout();
        header->setSpacing(10);
        QLabel *logo = new QLabel(this);
        logo->setPixmap(GUIUtil::tokenLogo(root, 32));
        logo->setFixedSize(32, 32);
        QLabel *title = new QLabel(root, this);
        title->setFont(GUIUtil::appFont(15, QFont::DemiBold));
        const RootToken *info = index->findRoot(root);
        QLabel *count = new QLabel(tr("%1 sub and unique tokens held").arg(info ? info->children : 0), this);
        count->setProperty("role", "secondary");
        header->addWidget(logo);
        header->addWidget(title);
        header->addWidget(count);
        header->addStretch();
        layout->addLayout(header);
    }

    search = new QLineEdit(this);
    search->setPlaceholderText(root.isEmpty() ? tr("Search tokens, also sub and unique tokens") : tr("Search within %1").arg(root));
    search->setClearButtonEnabled(true);
    search->setMinimumHeight(40);
    search->addAction(QIcon(GUIUtil::lineIcon("search", 14, GUIUtil::theme().textSecondary, 2.4)), QLineEdit::LeadingPosition);
    layout->addWidget(search);

    // a view on a model: the rows are painted, no widget is made per token
    model = new TokenListModel(this);
    view = new QListView(this);
    view->setModel(model);
    view->setItemDelegate(new TokenRowDelegate(view));
    view->setUniformItemSizes(true);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(view, 1);

    hint = new QLabel(this);
    hint->setProperty("role", "caption");
    hint->setFont(GUIUtil::appFont(12));
    layout->addWidget(hint);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *close = new QPushButton(purpose == Choose ? tr("Cancel") : tr("Close"), this);
    close->setAutoDefault(false);
    actionButton = new QPushButton(purpose == Choose ? tr("Choose") : tr("Send"), this);
    actionButton->setProperty("primary", true);
    actionButton->setAutoDefault(false);
    actionButton->setEnabled(false);
    buttons->addWidget(close);
    buttons->addWidget(actionButton);
    layout->addLayout(buttons);

    // filter a moment after the last key
    QTimer *delay = new QTimer(this);
    delay->setSingleShot(true);
    delay->setInterval(150);
    connect(search, SIGNAL(textChanged(QString)), delay, SLOT(start()));
    connect(delay, SIGNAL(timeout()), this, SLOT(refill()));
    connect(close, SIGNAL(clicked()), this, SLOT(reject()));
    connect(actionButton, SIGNAL(clicked()), this, SLOT(acceptCurrent()));
    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(acceptCurrent()));
    connect(search, SIGNAL(returnPressed()), this, SLOT(acceptCurrent()));
    connect(view->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(selectionChanged()));
    refill();
    search->setFocus();
}

void TokenListDialog::refill()
{
    GUIUtil::LogTimer timer("token list dialog refill");
    const QString text = search->text().trimmed();
    bool more = false;
    if (!root.isEmpty()) {
        model->setTokens(index->children(root, text, LIST_CAP, &more));
    } else if (text.isEmpty()) {
        // the root tokens; sub and unique tokens are found by typing
        QVector<RootToken> roots = index->roots();
        more = roots.size() > LIST_CAP;
        if (more)
            roots.resize(LIST_CAP);
        model->setRoots(roots);
    } else {
        model->setTokens(index->search(text, LIST_CAP, &more));
    }
    if (more)
        hint->setText(tr("The first %1 are shown. Type more to narrow the list.").arg(LIST_CAP));
    else if (model->rowCount() == 0)
        hint->setText(tr("Nothing found."));
    else if (root.isEmpty() && text.isEmpty())
        hint->setText(tr("Root tokens. Type to find sub tokens and unique tokens as well."));
    else
        hint->setText(tr("%1 found").arg(model->rowCount()));
    if (model->rowCount() > 0)
        view->setCurrentIndex(model->index(0, 0));
    selectionChanged();
}

void TokenListDialog::selectionChanged()
{
    const QModelIndex current = view->currentIndex();
    // what is not held itself (only its owner token, or only tokens below it) cannot be sent
    actionButton->setEnabled(current.isValid() && current.data(TokenListModel::HeldRole).toBool());
}

void TokenListDialog::acceptCurrent()
{
    const QModelIndex current = view->currentIndex();
    if (!current.isValid() || !current.data(TokenListModel::HeldRole).toBool())
        return;
    result = current.data(TokenListModel::NameRole).toString();
    accept();
}
