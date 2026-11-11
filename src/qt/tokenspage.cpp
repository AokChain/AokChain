// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokenspage.h"

#include "explorerpage.h"
#include "guiutil.h"
#include "pagewidgets.h"
#include "tokenlists.h"
#include "platformstyle.h"
#include "tokentablemodel.h"
#include "walletmodel.h"

#include "tokens/tokens.h"
#include "validation.h"

#include <univalue.h>

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

/** Tokens of the chain that are read at a time. */
static const int PAGE_SIZE = 60;
/** Cards that are made at most. Root tokens are few; this only bounds the number of widgets. */
static const int MAX_CARDS = TokenLimits::ROOT_CARDS;
static const int CARD_WIDTH = 250;
static const int CARD_GAP = 16;

/**
 * Without a search: root tokens only. The names are read in batches and the sub and
 * unique tokens are skipped, until a page of roots is full. With a search the
 * matches are listed as they are, so that sub tokens can be found.
 */
void TokensWorker::list(const QString& filter, int start, int count, int generation)
{
    UniValue items(UniValue::VARR);
    int next = start;
    bool more = false;
    auto add = [&items](const std::string& name, const UniValue& info) {
        UniValue item(UniValue::VOBJ);
        item.pushKV("name", name);
        const UniValue amount = find_value(info, "amount");
        if (amount.isNum())
            item.pushKV("amount", amount.getValStr());
        items.push_back(item);
    };
    if (filter == "*") {
        const int BATCH = 2000;
        std::vector<std::string> roots;
        while ((int)roots.size() < count) {
            UniValue names;
            if (!NodeRPC::call("listtokens", {"*", "false", std::to_string(BATCH), std::to_string(next)}, names) || !names.isArray())
                break;
            size_t used = 0;
            for (; used < names.size() && (int)roots.size() < count; ++used) {
                const std::string name = names[used].get_str();
                if (name.find('/') == std::string::npos && name.find('#') == std::string::npos)
                    roots.push_back(name);
            }
            next += (int)used;
            more = used < names.size() || (int)names.size() == BATCH;
            if (!more)
                break;
        }
        for (const std::string& name : roots) {
            UniValue info;
            if (NodeRPC::call("listtokens", {name, "true", "1", "0"}, info) && info.isObject() && !find_value(info, name).isNull())
                add(name, find_value(info, name));
            else
                add(name, NullUniValue);
        }
    } else {
        UniValue result;
        if (NodeRPC::call("listtokens", {filter.toStdString(), "true", std::to_string(count), std::to_string(start)}, result) && result.isObject()) {
            const std::vector<std::string>& names = result.getKeys();
            for (const std::string& name : names)
                add(name, result[name]);
            next = start + (int)names.size();
            more = (int)names.size() >= count;
        }
    }
    UniValue answer(UniValue::VOBJ);
    answer.pushKV("items", items);
    answer.pushKV("next", next);
    answer.pushKV("more", more);
    Q_EMIT listed(QString::fromStdString(answer.write()), start, generation);
}

static QLabel *tag(const QString& text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "tag");
    label->setFont(GUIUtil::appFont(11, QFont::Medium));
    return label;
}

static QLabel *caption(const QString& text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("role", "caption");
    label->setFont(GUIUtil::appFont(12));
    return label;
}

TokensPage::TokensPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent), model(nullptr), heldTokens(nullptr), columns(0), generation(0), nextStart(0), loading(false), loadedOnce(false)
{
    const GUIUtil::Theme& theme = GUIUtil::theme();
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *body = new QWidget();
    body->setObjectName("tokensBody");
    body->setStyleSheet("QWidget#tokensBody { background: transparent; }");
    scroll->setWidget(body);
    scroll->viewport()->setObjectName("tokensView");
    scroll->viewport()->setStyleSheet("QWidget#tokensView { background: transparent; }");
    outer->addWidget(scroll);

    QVBoxLayout *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 10, 0);
    layout->setSpacing(14);

    // tokens of the wallet
    QHBoxLayout *heldHeader = new QHBoxLayout();
    heldHeader->setSpacing(8);
    QLabel *heldTitle = new QLabel(tr("Tokens of this wallet"), body);
    heldTitle->setProperty("role", "title");
    heldTitle->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    heldCount = new QLabel(body);
    heldCount->setProperty("role", "secondary");
    heldHeader->addWidget(heldTitle);
    heldHeader->addWidget(heldCount);
    heldHeader->addStretch();
    layout->addLayout(heldHeader);
    grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(CARD_GAP);
    layout->addLayout(grid);
    heldEmpty = caption(tr("This wallet holds no tokens yet. Tokens that are sent to one of your addresses appear here."), body);
    layout->addWidget(heldEmpty);
    layout->addSpacing(10);

    // tokens of the chain that the wallet does not hold
    QFrame *chainCard = new QFrame(body);
    chainCard->setProperty("role", "card");
    QVBoxLayout *chainOuter = new QVBoxLayout(chainCard);
    chainOuter->setContentsMargins(22, 18, 22, 14);
    chainOuter->setSpacing(0);
    QHBoxLayout *chainHeader = new QHBoxLayout();
    chainHeader->setSpacing(8);
    QLabel *chainTitle = new QLabel(tr("AOK tokens not held"), chainCard);
    chainTitle->setProperty("role", "title");
    chainTitle->setFont(GUIUtil::appFont(16, QFont::DemiBold));
    chainStatus = new QLabel(chainCard);
    chainStatus->setProperty("role", "secondary");
    search = new QLineEdit(chainCard);
    search->setPlaceholderText(tr("Search tokens"));
    search->setFixedSize(220, 40);
    search->setClearButtonEnabled(true);
    search->addAction(QIcon(GUIUtil::lineIcon("search", 14, theme.textSecondary, 2.4)), QLineEdit::LeadingPosition);
    chainHeader->addWidget(chainTitle);
    chainHeader->addWidget(chainStatus);
    chainHeader->addStretch();
    chainHeader->addWidget(search);
    chainOuter->addLayout(chainHeader);
    chainOuter->addSpacing(4);
    chainOuter->addWidget(caption(tr("Root tokens that exist on the AokChain block chain, as this node knows them, with their total supply. Search to find sub tokens."), chainCard));
    chainOuter->addSpacing(10);
    chainLayout = new QVBoxLayout();
    chainLayout->setSpacing(0);
    chainOuter->addLayout(chainLayout);
    moreButton = new QPushButton(tr("Show more"), chainCard);
    moreButton->setProperty("look", "link");
    moreButton->setCursor(Qt::PointingHandCursor);
    moreButton->hide();
    chainOuter->addSpacing(8);
    chainOuter->addWidget(moreButton, 0, Qt::AlignLeft);
    layout->addWidget(chainCard);
    layout->addStretch(1);
    connect(moreButton, SIGNAL(clicked()), this, SLOT(loadMore()));

    rebuildTimer = new QTimer(this);
    rebuildTimer->setSingleShot(true);
    rebuildTimer->setInterval(100);
    connect(rebuildTimer, SIGNAL(timeout()), this, SLOT(rebuildHeld()));
    searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(300);
    connect(searchTimer, SIGNAL(timeout()), this, SLOT(searchChanged()));
    connect(search, SIGNAL(textChanged(QString)), searchTimer, SLOT(start()));

    TokensWorker *worker = new TokensWorker();
    worker->moveToThread(&thread);
    connect(&thread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(this, SIGNAL(requestList(QString,int,int,int)), worker, SLOT(list(QString,int,int,int)));
    connect(worker, SIGNAL(listed(QString,int,int)), this, SLOT(listed(QString,int,int)));
    thread.start();
}

TokensPage::~TokensPage()
{
    thread.quit();
    thread.wait();
}

void TokensPage::setModel(WalletModel *_model)
{
    model = _model;
    if (model && model->getTokenTableModel()) {
        heldTokens = HeldTokenIndex::forModel(model->getTokenTableModel());
        connect(heldTokens, SIGNAL(changed()), this, SLOT(scheduleRebuild()));
        rebuildHeld();
    }
}

void TokensPage::scheduleRebuild()
{
    rebuildTimer->start();
}

/**
 * One card per ROOT token of the wallet. A root is shown when the wallet holds it, its owner
 * token, or something below it. Sub and unique tokens are not cards (there can be tens of
 * thousands): the card of their root has a button that opens their list.
 */
void TokensPage::rebuildHeld()
{
    if (!model || !heldTokens)
        return;
    // The cards are made again only when a root changed. Sub and unique tokens that come
    // and go change the count on the card of their root, nothing else.
    QString signature;
    int listed = 0;
    for (const RootToken& root : heldTokens->roots()) {
        if (listed++ >= MAX_CARDS)
            break;
        signature += root.name + QChar('|') + root.amount + QChar('|') + QString::number(root.children) +
                     (root.held ? "h" : "-") + (root.admin ? "a" : "-") + (root.locked ? "l" : "-") + QChar('\n');
    }
    signature += QString::number(heldTokens->roots().size());
    if (signature == cardSignature && !cards.isEmpty())
        return;
    cardSignature = signature;
    GUIUtil::LogTimer timer("tokens page cards rebuild");
    for (QWidget *card : cards)
        card->deleteLater();
    cards.clear();
    const QSet<QString> before = heldNames;
    heldNames.clear();
    for (const RootToken& root : heldTokens->roots())
        heldNames.insert(root.name);

    const GUIUtil::Theme& theme = GUIUtil::theme();
    const QVector<RootToken>& roots = heldTokens->roots();
    LOCK(cs_main);
    auto cache = GetCurrentTokenCache();
    for (const RootToken& root : roots) {
        if (cards.size() >= MAX_CARDS)
            break;
        QFrame *card = new QFrame(this);
        card->setProperty("role", "tokenCard");
        card->setFixedHeight(180);
        QVBoxLayout *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(6);

        QHBoxLayout *top = new QHBoxLayout();
        top->setSpacing(10);
        QLabel *logo = new QLabel(card);
        logo->setPixmap(GUIUtil::tokenLogo(root.name, 40));
        logo->setFixedSize(40, 40);
        top->addWidget(logo);
        QVBoxLayout *names = new QVBoxLayout();
        names->setSpacing(3);
        QLabel *name = new QLabel(card);
        name->setFont(GUIUtil::appFont(14, QFont::Medium));
        name->setText(QFontMetrics(name->font()).elidedText(root.name, Qt::ElideMiddle, 170));
        name->setToolTip(root.name);
        names->addWidget(name);
        QHBoxLayout *chips = new QHBoxLayout();
        chips->setSpacing(4);
        if (root.admin)
            chips->addWidget(tag(tr("Owner"), card));
        if (root.locked)
            chips->addWidget(tag(tr("Locked"), card));
        chips->addStretch();
        names->addLayout(chips);
        top->addLayout(names, 1);
        layout->addLayout(top);
        layout->addSpacing(4);

        if (!root.held) {
            QLabel *none = new QLabel(tr("None held"), card);
            none->setProperty("role", "secondary");
            none->setFixedHeight(28);
            layout->addWidget(none);
        } else {
            AmountLabel *amount = new AmountLabel(20, QFont::Medium, card);
            amount->setAmount(GUIUtil::withThousands(root.amount));
            layout->addWidget(amount);
        }

        // decimals and whether more can be issued; the sub and unique tokens open in a window of their own
        QHBoxLayout *metaRow = new QHBoxLayout();
        QString meta;
        CNewToken token;
        if (cache && cache->GetTokenMetaDataIfExists(root.name.toStdString(), token))
            meta = tr("%1 decimals").arg((int)token.units) + QString::fromUtf8(" · ") + (token.nReissuable ? tr("reissuable") : tr("fixed supply"));
        QLabel *metaLabel = caption(meta, card);
        metaLabel->setMinimumHeight(30);
        metaRow->addWidget(metaLabel);
        metaRow->addStretch();
        const QString tokenName = root.name;
        if (root.children > 0) {
            QPushButton *below = new QPushButton(tr("%1 sub tokens").arg(root.children), card);
            below->setProperty("look", "chipButton");
            below->setLayoutDirection(Qt::RightToLeft);
            below->setIcon(QIcon(GUIUtil::lineIcon("chevron-right", 11, theme.label, 2.6)));
            below->setIconSize(QSize(11, 11));
            below->setCursor(Qt::PointingHandCursor);
            below->setToolTip(tr("Show the sub tokens and unique tokens of this token that the wallet holds"));
            connect(below, &QPushButton::clicked, [this, tokenName] { showTokensBelow(tokenName); });
            metaRow->addWidget(below);
        }
        layout->addLayout(metaRow);
        layout->addStretch();

        QHBoxLayout *buttons = new QHBoxLayout();
        buttons->setSpacing(8);
        QPushButton *send = new QPushButton(tr("Send"), card);
        send->setProperty("look", "soft");
        send->setIcon(QIcon(GUIUtil::lineIcon("send", 14, theme.accent, 2.2)));
        send->setCursor(Qt::PointingHandCursor);
        send->setEnabled(root.held);
        connect(send, &QPushButton::clicked, [this, tokenName] { Q_EMIT sendToken(tokenName); });
        QPushButton *receive = new QPushButton(tr("Receive"), card);
        receive->setProperty("look", "chipButton");
        receive->setIcon(QIcon(GUIUtil::lineIcon("receive", 14, theme.label, 2.2)));
        receive->setCursor(Qt::PointingHandCursor);
        connect(receive, SIGNAL(clicked()), this, SIGNAL(receiveRequested()));
        for (QPushButton *button : {send, receive}) {
            button->setIconSize(QSize(14, 14));
            button->setFixedHeight(34);
            buttons->addWidget(button, 1);
        }
        layout->addLayout(buttons);
        cards << card;
    }
    heldCount->setText(roots.size() > cards.size() ? tr("%1 kinds, the first %2 shown").arg(roots.size()).arg(cards.size())
                                                    : tr("%1 kinds").arg(roots.size()));
    heldEmpty->setVisible(cards.isEmpty());
    columns = 0;
    reflow();
    // what is held is left out of the list of the chain
    if (loadedOnce && before != heldNames)
        startList(true);
}

/** The window with the held sub and unique tokens of a root; "Send" there leads to the send page. */
void TokensPage::showTokensBelow(const QString& root)
{
    if (!heldTokens)
        return;
    TokenListDialog dialog(heldTokens, root, TokenListDialog::Browse, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.chosen().isEmpty())
        Q_EMIT sendToken(dialog.chosen());
}

/** As many cards in a row as fit. */
void TokensPage::reflow()
{
    const int available = width() - 10;
    const int fit = qMax(1, (available + CARD_GAP) / (CARD_WIDTH + CARD_GAP));
    if (fit == columns)
        return;
    columns = fit;
    while (QLayoutItem *item = grid->takeAt(0))
        delete item;
    for (int i = 0; i < cards.size(); ++i)
        grid->addWidget(cards[i], i / columns, i % columns);
    for (int c = 0; c < 12; ++c)
        grid->setColumnStretch(c, c < columns ? 1 : 0);
}

void TokensPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflow();
}

void TokensPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!loadedOnce) {
        loadedOnce = true;
        startList(true);
    }
}

void TokensPage::searchChanged()
{
    startList(true);
}

void TokensPage::loadMore()
{
    startList(false);
}

void TokensPage::startList(bool reset)
{
    if (reset) {
        ++generation;
        nextStart = 0;
        while (QLayoutItem *item = chainLayout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
    } else if (loading) {
        return;
    }
    loading = true;
    moreButton->hide();
    chainStatus->setText(tr("loading..."));
    // token names are upper case; '*' matches the rest of the name
    QString filter = search->text().trimmed().toUpper();
    filter = filter.isEmpty() ? "*" : filter + "*";
    Q_EMIT requestList(filter, nextStart, PAGE_SIZE, generation);
}

void TokensPage::listed(const QString& json, int start, int gen)
{
    if (gen != generation)
        return; // the answer to an older search
    loading = false;
    UniValue result;
    if (!result.read(json.toStdString()) || !result.isObject()) {
        chainStatus->setText(QString());
        return;
    }
    const UniValue items = find_value(result, "items");
    nextStart = (int)find_value(result, "next").get_int64();
    for (size_t i = 0; items.isArray() && i < items.size(); ++i) {
        const QString name = QString::fromStdString(find_value(items[i], "name").get_str());
        // what the wallet holds, or holds something below, is in the cards above
        if (heldNames.contains(TokenNames::root(name)))
            continue;
        QWidget *row = new QWidget(this);
        QVBoxLayout *outer = new QVBoxLayout(row);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        QFrame *line = new QFrame(row);
        line->setProperty("role", "hline");
        line->setFixedHeight(1);
        outer->addWidget(line);
        QHBoxLayout *layout = new QHBoxLayout();
        layout->setContentsMargins(0, 8, 0, 8);
        layout->setSpacing(10);
        QLabel *logo = new QLabel(row);
        logo->setPixmap(GUIUtil::tokenLogo(name, 28));
        logo->setFixedSize(28, 28);
        layout->addWidget(logo);
        QLabel *label = new QLabel(name, row);
        label->setFont(GUIUtil::appFont(14, QFont::Medium));
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
        if (name.contains('#'))
            layout->addWidget(tag(tr("Unique token"), row));
        else if (name.contains('/'))
            layout->addWidget(tag(tr("Sub token"), row));
        layout->addStretch();
        const UniValue amount = find_value(items[i], "amount");
        if (amount.isStr() || amount.isNum()) {
            AmountLabel *supply = new AmountLabel(14, QFont::Medium, row);
            supply->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            supply->setAmount(GUIUtil::withThousands(QString::fromStdString(amount.getValStr())));
            layout->addWidget(supply);
        }
        outer->addLayout(layout);
        chainLayout->addWidget(row);
    }
    const int shown = chainLayout->count();
    const UniValue moreValue = find_value(result, "more");
    const bool more = moreValue.isBool() ? moreValue.get_bool() : moreValue.getValStr() == "1";
    chainStatus->setText(shown == 0 && !more ? tr("none") : (more ? tr("%1 and more").arg(shown) : tr("%1 kinds").arg(shown)));
    moreButton->setVisible(more);
}
