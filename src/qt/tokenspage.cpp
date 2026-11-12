// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokenspage.h"

#include "guiutil.h"
#include "pagewidgets.h"
#include "tokenlists.h"
#include "platformstyle.h"
#include "tokentablemodel.h"
#include "walletmodel.h"

#include "tokens/tokens.h"
#include "validation.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

/** Cards that are made at most. Root tokens are few; this only bounds the number of widgets. */
static const int MAX_CARDS = TokenLimits::ROOT_CARDS;
static const int CARD_WIDTH = 250;
static const int CARD_GAP = 16;

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
    QWidget(parent), model(nullptr), heldTokens(nullptr), columns(0)
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
    heldTitle->setFont(GUIUtil::appFont(15, QFont::Medium));
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

    layout->addStretch(1);

    rebuildTimer = new QTimer(this);
    rebuildTimer->setSingleShot(true);
    rebuildTimer->setInterval(100);
    connect(rebuildTimer, SIGNAL(timeout()), this, SLOT(rebuildHeld()));
}

TokensPage::~TokensPage()
{
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
