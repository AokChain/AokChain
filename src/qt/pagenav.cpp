// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pagenav.h"

#include "clientmodel.h"
#include "guiutil.h"
#include "walletmodel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QPainter>
#include <QTimer>
#include <QToolButton>

#include <limits>

namespace {

/** Entries kept for going back */
const int MAX_HISTORY = 100;
/** A block older than this means the node is still catching up (as in the side bar) */
const qint64 MAX_TIP_AGE = 90 * 60;
/** A read that takes less than this is not shown as one */
const int BUSY_DELAY_MS = 300;
/** A refresh the user asked for is shown at least this long */
const int REFRESH_SHOWN_MS = 600;

QIcon navIcon(const QString& name)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QIcon icon;
    icon.addPixmap(GUIUtil::lineIcon(name, 16, t.text, 2.2), QIcon::Normal);
    icon.addPixmap(GUIUtil::lineIcon(name, 16, t.textDisabled, 2.2), QIcon::Disabled);
    return icon;
}

QString keyText(const QAction *action)
{
    return action->shortcut().toString(QKeySequence::NativeText);
}

} // namespace

// ------------------------------------------------------------------ history

NavigationHistory::NavigationHistory(QObject *parent) :
    QObject(parent), current(-1), restoring(false)
{
    back = new QAction(navIcon("chevron-left"), tr("Back"), this);
    forward = new QAction(navIcon("chevron-right"), tr("Forward"), this);
#ifdef Q_OS_MAC
    // Cmd+[ and Cmd+], as in Safari and Finder (Cmd+arrow moves the cursor in a text field)
    back->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_BracketLeft));
    forward->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_BracketRight));
#else
    back->setShortcut(QKeySequence(Qt::ALT + Qt::Key_Left));
    forward->setShortcut(QKeySequence(Qt::ALT + Qt::Key_Right));
#endif
    // in the whole window, also when the side bar has the focus; each wallet has its own history,
    // and only the view that is shown takes the keys (a hidden widget's shortcuts do not apply)
    back->setShortcutContext(Qt::WindowShortcut);
    forward->setShortcutContext(Qt::WindowShortcut);
    connect(back, SIGNAL(triggered()), this, SLOT(goBack()));
    connect(forward, SIGNAL(triggered()), this, SLOT(goForward()));
    updateActions();
}

void NavigationHistory::setDescriber(const std::function<QString(const PageLocation&)>& describer)
{
    describe = describer;
    updateActions();
}

void NavigationHistory::visit(const PageLocation& location)
{
    if (!location.isValid())
        return;
    if (restoring) {
        // going back or forward: the entry becomes what the page shows now
        if (current >= 0 && current < entries.size())
            entries[current] = location;
        updateActions();
        return;
    }
    if (current >= 0 && entries[current] == location)
        return;
    // somewhere new: what was ahead is gone, as in a browser
    while (entries.size() > current + 1)
        entries.removeLast();
    entries.append(location);
    while (entries.size() > MAX_HISTORY)
        entries.removeFirst();
    current = entries.size() - 1;
    updateActions();
}

void NavigationHistory::goBack()
{
    go(current - 1);
}

void NavigationHistory::goForward()
{
    go(current + 1);
}

void NavigationHistory::go(int index)
{
    if (index < 0 || index >= entries.size() || index == current)
        return;
    current = index;
    restoring = true;
    Q_EMIT restore(entries[current]);
    restoring = false;
    updateActions();
}

void NavigationHistory::updateActions()
{
    const bool canBack = current > 0;
    const bool canForward = current >= 0 && current + 1 < entries.size();
    back->setEnabled(canBack);
    forward->setEnabled(canForward);
    QString backName, forwardName;
    if (describe && canBack)
        backName = describe(entries[current - 1]);
    if (describe && canForward)
        forwardName = describe(entries[current + 1]);
    back->setToolTip(QString("%1 (%2)").arg(backName.isEmpty() ? tr("Back") : tr("Back to %1").arg(backName), keyText(back)));
    forward->setToolTip(QString("%1 (%2)").arg(forwardName.isEmpty() ? tr("Forward") : tr("Forward to %1").arg(forwardName), keyText(forward)));
}

QWidget *PageNav::makeNavButtons(NavigationHistory *history, QWidget *parent)
{
    QWidget *box = new QWidget(parent);
    QHBoxLayout *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    for (QAction *action : {history->backAction(), history->forwardAction()}) {
        // a plain button that follows the action: the shortcut stays with the view alone
        QToolButton *button = new QToolButton(box);
        button->setProperty("look", "nav");
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(34, 34);
        button->setIconSize(QSize(16, 16));
        button->setIcon(action->icon());
        button->setAccessibleName(action->text());
        auto sync = [button, action] {
            button->setEnabled(action->isEnabled());
            button->setToolTip(action->toolTip());
            button->setCursor(action->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        };
        sync();
        QObject::connect(action, &QAction::changed, button, sync);
        QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
        layout->addWidget(button);
    }
    return box;
}

QWidget *PageChrome::navButtons(QWidget *parent) const
{
    return history ? PageNav::makeNavButtons(history, parent) : nullptr;
}

QWidget *PageChrome::statusPill(QWidget *parent) const
{
    return node ? new PageStatusPill(node, activity, parent) : nullptr;
}

// ------------------------------------------------------------------ node

NodeStatus::NodeStatus(QObject *parent) :
    QObject(parent), tipHeight(-1), rescanProgress(-1), currentLevel(Busy)
{
    currentText = tr("Starting");
    currentDetail = tr("The node is starting.");
    // without a new block the age of the last one still grows
    timer = new QTimer(this);
    timer->setInterval(15000);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start();
}

void NodeStatus::setClientModel(ClientModel *model)
{
    clientModel = model;
    if (model) {
        // once at the start; after that the notifications carry the tip
        tipHeight = model->getNumBlocks();
        tipTime = model->getLastBlockDate();
        connect(model, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(tipChanged(int,QDateTime,double,bool)));
        connect(model, SIGNAL(numConnectionsChanged(int)), this, SLOT(update()));
        connect(model, SIGNAL(networkActiveChanged(bool)), this, SLOT(update()));
    }
    update();
}

void NodeStatus::tipChanged(int count, const QDateTime& blockDate, double progress, bool header)
{
    Q_UNUSED(progress);
    // a new header is not a new block
    if (header)
        return;
    tipHeight = count;
    tipTime = blockDate;
    update();
}

void NodeStatus::setWalletModel(WalletModel *model)
{
    if (model)
        connect(model, SIGNAL(showProgress(QString,int)), this, SLOT(walletProgress(QString,int)));
}

void NodeStatus::walletProgress(const QString& title, int progress)
{
    Q_UNUSED(title);
    rescanProgress = (progress > 0 && progress < 100) ? progress : (progress == 0 ? 0 : -1);
    update();
}

void NodeStatus::update()
{
    Level level = Ok;
    QString text, detail;
    if (!clientModel) {
        level = Busy;
        text = tr("Starting");
        detail = tr("The node is starting.");
    } else {
        const int peers = clientModel->getNumConnections();
        const int blocks = tipHeight;
        const qint64 age = tipTime.isValid() ? qMax<qint64>(0, tipTime.secsTo(QDateTime::currentDateTime())) : std::numeric_limits<qint64>::max();
        const QString ageText = age == std::numeric_limits<qint64>::max() ? QString() : GUIUtil::formatNiceTimeOffset(age);
        const enum BlockSource source = clientModel->getBlockSource();
        const QString peersLine = tr("Connected peers: %1").arg(peers);
        if (rescanProgress >= 0) {
            level = Busy;
            text = tr("Rescanning %1%").arg(rescanProgress);
            detail = tr("The wallet is reading the blocks again for its transactions. Balances and transactions are complete when it is done.");
        } else if (!clientModel->getNetworkActive()) {
            level = Error;
            text = tr("Network off");
            detail = tr("The network activity of this wallet is turned off: new blocks and payments do not arrive, and payments are not sent out.");
        } else if (source == BLOCK_SOURCE_REINDEX || source == BLOCK_SOURCE_DISK) {
            level = Busy;
            text = source == BLOCK_SOURCE_REINDEX ? tr("Reindexing") : tr("Importing blocks");
            detail = tr("The node is processing the blocks on the disk. Balances and transactions are complete when it is done.");
        } else if (age >= MAX_TIP_AGE) {
            level = peers > 0 ? Busy : Error;
            text = peers > 0 ? tr("Synchronizing") : tr("No connection");
            detail = tr("The last block of this node is %1 old. Until the wallet has caught up with the network, balances and transactions may be incomplete.").arg(ageText);
        } else if (peers == 0) {
            level = Error;
            text = tr("No connection");
            detail = tr("Not connected to any peer: new blocks and payments do not arrive, and payments are not sent out.");
        } else {
            level = Ok;
            text = tr("Up to date");
            detail = tr("Block %1, %2 ago.").arg(GUIUtil::withThousands(QString::number(blocks)), ageText);
        }
        detail += "<br>" + peersLine;
    }
    if (level != currentLevel || text != currentText || detail != currentDetail) {
        currentLevel = level;
        currentText = text;
        currentDetail = detail;
        Q_EMIT changed();
    }
}

// ------------------------------------------------------------------ page

PageActivity::PageActivity(QObject *parent) :
    QObject(parent), canRefresh(false), pending(0)
{
    delay = new QTimer(this);
    delay->setSingleShot(true);
    delay->setInterval(BUSY_DELAY_MS);
    connect(delay, SIGNAL(timeout()), this, SIGNAL(changed()));
    hold = new QTimer(this);
    hold->setSingleShot(true);
    hold->setInterval(REFRESH_SHOWN_MS);
    connect(hold, SIGNAL(timeout()), this, SIGNAL(changed()));
}

void PageActivity::setRefreshable(bool refreshable)
{
    if (canRefresh == refreshable)
        return;
    canRefresh = refreshable;
    Q_EMIT changed();
}

void PageActivity::begin(const QString& _what)
{
    if (pending++ == 0) {
        started.start();
        delay->start();
        if (!hold->isActive())
            what = _what;
    } else if (!_what.isEmpty() && !hold->isActive()) {
        what = _what;
    }
}

void PageActivity::done()
{
    if (pending > 0)
        --pending;
    touch();
}

void PageActivity::touch()
{
    lastError.clear();
    lastUpdate = QDateTime::currentDateTime();
    Q_EMIT changed();
}

void PageActivity::failed(const QString& error)
{
    if (pending > 0)
        --pending;
    lastError = error;
    Q_EMIT changed();
}

bool PageActivity::busy() const
{
    return hold->isActive() || (pending > 0 && started.isValid() && started.elapsed() >= BUSY_DELAY_MS);
}

void PageActivity::requestRefresh()
{
    if (!canRefresh)
        return;
    what = tr("Refreshing");
    hold->start();
    Q_EMIT changed();
    Q_EMIT refreshRequested();
}

// ------------------------------------------------------------------ pill

PageStatusPill::PageStatusPill(NodeStatus *_node, PageActivity *_activity, QWidget *parent) :
    QAbstractButton(parent), node(_node), activity(_activity), level(NodeStatus::Busy), hovered(false)
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (node)
        connect(node, SIGNAL(changed()), this, SLOT(updateState()));
    if (activity) {
        connect(activity, SIGNAL(changed()), this, SLOT(updateState()));
        connect(this, SIGNAL(clicked()), activity, SLOT(requestRefresh()));
    }
    updateState();
}

void PageStatusPill::updateState()
{
    const bool refreshable = activity && activity->refreshable();
    QStringList lines;
    time.clear();
    if (activity && activity->busy()) {
        level = NodeStatus::Busy;
        label = (activity->busyText().isEmpty() ? tr("Loading") : activity->busyText()) + QString::fromUtf8("…");
        lines << tr("This page is reading its contents.");
    } else if (activity && !activity->error().isEmpty()) {
        level = NodeStatus::Error;
        label = tr("Error");
        lines << tr("This page could not read its contents: %1").arg(GUIUtil::HtmlEscape(activity->error()));
    } else if (node) {
        level = node->level();
        label = node->text();
        if (activity && activity->updated().isValid()) {
            if (level == NodeStatus::Ok)
                time = activity->updated().toString("HH:mm:ss");
            lines << tr("This page was read at %1.").arg(activity->updated().toString("HH:mm:ss"));
        }
    } else {
        level = NodeStatus::Busy;
        label = tr("Starting");
    }
    if (node)
        lines << node->detail();
    if (refreshable)
        lines << tr("Click to read it again (%1).").arg(QKeySequence(QKeySequence::Refresh).toString(QKeySequence::NativeText));
    setToolTip(QString("<qt><b>%1</b><br>%2</qt>").arg(GUIUtil::HtmlEscape(label), lines.join("<br>")));
    setAccessibleName(time.isEmpty() ? label : label + " " + time);
    setCursor(refreshable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateGeometry();
    update();
}

QSize PageStatusPill::sizeHint() const
{
    int width = 12 + 8 + 8 + QFontMetrics(GUIUtil::appFont(12, QFont::DemiBold)).horizontalAdvance(label) + 12;
    if (!time.isEmpty())
        width += 7 + QFontMetrics(GUIUtil::monoFont(11)).horizontalAdvance(time);
    if (activity && activity->refreshable())
        width += 8 + 14;
    return QSize(width, 28);
}

void PageStatusPill::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const GUIUtil::Theme& t = GUIUtil::theme();
    QColor back = t.okSoft, front = t.ok;
    if (level == NodeStatus::Busy) {
        back = t.warnBg;
        front = t.warnText;
    } else if (level == NodeStatus::Error) {
        back = t.errorSoft;
        front = t.error;
    }
    const bool refreshable = activity && activity->refreshable();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(hovered && refreshable ? QPen(front, 1) : QPen(Qt::NoPen));
    p.setBrush(back);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

    // the light: a dot with a soft ring
    const qreal cy = height() / 2.0;
    QColor ring = front;
    ring.setAlphaF(0.22);
    p.setPen(Qt::NoPen);
    p.setBrush(ring);
    p.drawEllipse(QPointF(16, cy), 6, 6);
    p.setBrush(front);
    p.drawEllipse(QPointF(16, cy), 3.6, 3.6);

    qreal x = 12 + 8 + 8;
    const QFont labelFont = GUIUtil::appFont(12, QFont::DemiBold);
    p.setFont(labelFont);
    p.setPen(front);
    const int labelWidth = QFontMetrics(labelFont).horizontalAdvance(label);
    p.drawText(QRectF(x, 0, labelWidth + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, label);
    x += labelWidth;
    if (!time.isEmpty()) {
        x += 7;
        const QFont timeFont = GUIUtil::monoFont(11);
        p.setFont(timeFont);
        QColor muted = front;
        muted.setAlphaF(0.75);
        p.setPen(muted);
        const int timeWidth = QFontMetrics(timeFont).horizontalAdvance(time);
        p.drawText(QRectF(x, 0, timeWidth + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, time);
        x += timeWidth;
    }
    if (refreshable) {
        x += 8;
        p.drawPixmap(QPointF(x, cy - 7), GUIUtil::lineIcon("refresh", 14, front, 2.2));
    }
}

void PageStatusPill::enterEvent(QEvent *event)
{
    hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void PageStatusPill::leaveEvent(QEvent *event)
{
    hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}
