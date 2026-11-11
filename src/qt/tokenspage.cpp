// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokenspage.h"

#include "explorerpage.h"
#include "guiutil.h"
#include "pagewidgets.h"
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
static const int CARD_WIDTH = 250;
static const int CARD_GAP = 16;

void TokensWorker::list(const QString& filter, int start, int count, int generation)
{
    UniValue result;
    QString json = "{}";
    if (NodeRPC::call("listtokens", {filter.toStdString(), "true", std::to_string(count), std::to_string(start)}, result) && result.isObject())
        json = QString::fromStdString(result.write());
    Q_EMIT listed(json, start, generation);
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
    QWidget(parent), model(nullptr), columns(0), generation(0), nextStart(0), loading(false), loadedOnce(false)
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
    chainOuter->addWidget(caption(tr("Tokens that exist on the AokChain block chain, as this node knows them. The total supply is shown."), chainCard));
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
        TokenTableModel *tokens = model->getTokenTableModel();
        connect(tokens, SIGNAL(layoutChanged()), this, SLOT(scheduleRebuild()));
        connect(tokens, SIGNAL(modelReset()), this, SLOT(scheduleRebuild()));
        rebuildHeld();
    }
}

void TokensPage::scheduleRebuild()
{
    rebuildTimer->start();
}

/** One card per token of the wallet. Owner tokens, sub tokens and unique tokens as on the overview. */
void TokensPage::rebuildHeld()
{
    if (!model || !model->getTokenTableModel())
        return;
    for (QWidget *card : cards)
        card->deleteLater();
    cards.clear();
    const QSet<QString> before = heldNames;
    heldNames.clear();

    struct Entry { QString key, name, amount; bool admin, locked, ownerOnly; };
    QList<Entry> entries;
    TokenTableModel *tokens = model->getTokenTableModel();
    for (int row = 0; row < tokens->rowCount(QModelIndex()); ++row) {
        const QModelIndex index = tokens->index(row, 0);
        Entry e;
        e.name = index.data(TokenTableModel::TokenNameRole).toString();
        e.amount = GUIUtil::withThousands(index.data(TokenTableModel::FormattedAmountRole).toString());
        e.admin = index.data(TokenTableModel::AdministratorRole).toBool();
        e.locked = index.data(TokenTableModel::IsLockedRole).toBool();
        if (e.locked && e.name.endsWith(" (LOCKED)"))
            e.name.chop(9);
        e.ownerOnly = e.name.endsWith('!');
        if (e.ownerOnly)
            e.name.chop(1);
        e.key = e.name + (e.locked ? QString(QChar(1)) : QString());
        entries << e;
        heldNames.insert(e.name);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });

    const GUIUtil::Theme& theme = GUIUtil::theme();
    LOCK(cs_main);
    auto cache = GetCurrentTokenCache();
    for (const Entry& e : entries) {
        QFrame *card = new QFrame(this);
        card->setProperty("role", "tokenCard");
        card->setFixedHeight(168);
        QVBoxLayout *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(6);

        QHBoxLayout *top = new QHBoxLayout();
        top->setSpacing(10);
        QLabel *logo = new QLabel(card);
        logo->setPixmap(GUIUtil::tokenLogo(e.name, 40));
        logo->setFixedSize(40, 40);
        top->addWidget(logo);
        QVBoxLayout *names = new QVBoxLayout();
        names->setSpacing(3);
        QLabel *name = new QLabel(card);
        name->setFont(GUIUtil::appFont(14, QFont::Medium));
        name->setText(QFontMetrics(name->font()).elidedText(e.name, Qt::ElideMiddle, 170));
        name->setToolTip(e.name);
        names->addWidget(name);
        QHBoxLayout *chips = new QHBoxLayout();
        chips->setSpacing(4);
        if (e.name.contains('#'))
            chips->addWidget(tag(tr("Unique token"), card));
        else if (e.name.contains('/'))
            chips->addWidget(tag(tr("Sub token"), card));
        if (e.admin || e.ownerOnly)
            chips->addWidget(tag(tr("Owner"), card));
        if (e.locked)
            chips->addWidget(tag(tr("Locked"), card));
        chips->addStretch();
        names->addLayout(chips);
        top->addLayout(names, 1);
        layout->addLayout(top);
        layout->addSpacing(4);

        if (e.ownerOnly) {
            QLabel *none = new QLabel(tr("None held"), card);
            none->setProperty("role", "secondary");
            none->setFixedHeight(28);
            layout->addWidget(none);
        } else {
            AmountLabel *amount = new AmountLabel(20, QFont::Medium, card);
            amount->setAmount(e.amount);
            layout->addWidget(amount);
        }

        // decimals and whether more can be issued, from the token database of the node
        QString meta;
        CNewToken token;
        if (cache && cache->GetTokenMetaDataIfExists(e.name.toStdString(), token))
            meta = tr("%1 decimals").arg((int)token.units) + QString::fromUtf8(" · ") + (token.nReissuable ? tr("reissuable") : tr("fixed supply"));
        layout->addWidget(caption(meta, card));
        layout->addStretch();

        QHBoxLayout *buttons = new QHBoxLayout();
        buttons->setSpacing(8);
        QPushButton *send = new QPushButton(tr("Send"), card);
        send->setProperty("look", "soft");
        send->setIcon(QIcon(GUIUtil::lineIcon("send", 14, theme.accent, 2.2)));
        send->setCursor(Qt::PointingHandCursor);
        send->setEnabled(!e.ownerOnly && !e.locked);
        const QString tokenName = e.name;
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
    heldCount->setText(tr("%1 kinds").arg(cards.size()));
    heldEmpty->setVisible(cards.isEmpty());
    columns = 0;
    reflow();
    // what is held is left out of the list of the chain
    if (loadedOnce && before != heldNames)
        startList(true);
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
    const std::vector<std::string>& names = result.getKeys();
    nextStart = start + (int)names.size();
    for (const std::string& key : names) {
        const QString name = QString::fromStdString(key);
        if (heldNames.contains(name))
            continue;
        const UniValue& info = result[key];
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
        const UniValue& amount = find_value(info, "amount");
        if (amount.isNum()) {
            AmountLabel *supply = new AmountLabel(14, QFont::Medium, row);
            supply->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            supply->setAmount(GUIUtil::withThousands(QString::fromStdString(amount.getValStr())));
            layout->addWidget(supply);
        }
        outer->addLayout(layout);
        chainLayout->addWidget(row);
    }
    const int shown = chainLayout->count();
    const bool more = (int)names.size() >= PAGE_SIZE;
    chainStatus->setText(shown == 0 && !more ? tr("none") : (more ? tr("%1 and more").arg(shown) : tr("%1 kinds").arg(shown)));
    moreButton->setVisible(more);
}
