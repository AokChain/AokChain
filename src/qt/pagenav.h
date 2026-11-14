// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_PAGENAV_H
#define AOKCHAIN_QT_PAGENAV_H

#include <QAbstractButton>
#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QAction;
class QTimer;
QT_END_NAMESPACE

/** A place in the wallet view: a page and, on a page with screens of its own, the screen. */
struct PageLocation
{
    int page;
    /** What the page shows: the query of the explorer, the screen of the sign page; empty on other pages */
    QString state;

    explicit PageLocation(int _page = -1, const QString& _state = QString()) : page(_page), state(_state) {}
    bool isValid() const { return page >= 0; }
    bool operator==(const PageLocation& other) const { return page == other.page && state == other.state; }
    bool operator!=(const PageLocation& other) const { return !(*this == other); }
};

/**
 * Back and forward through what the wallet view showed, as in a web browser: the
 * pages of the side bar, the look-ups of the explorer and the screens of the sign page.
 */
class NavigationHistory : public QObject
{
    Q_OBJECT

public:
    explicit NavigationHistory(QObject *parent = nullptr);

    QAction *backAction() const { return back; }
    QAction *forwardAction() const { return forward; }
    /** Names a place for the tool tips of the buttons ("Back to Explorer") */
    void setDescriber(const std::function<QString(const PageLocation&)>& describer);

    /**
     * The view shows this place now. It becomes a new entry unless it is the current one;
     * the entries ahead of the current one are dropped. While going back or forward it
     * corrects the current entry instead.
     */
    void visit(const PageLocation& location);

Q_SIGNALS:
    /** Show this place again (going back or forward). The view reports what it then shows with visit(). */
    void restore(const PageLocation& location);

public Q_SLOTS:
    void goBack();
    void goForward();

private:
    QList<PageLocation> entries;
    int current;
    bool restoring;
    QAction *back;
    QAction *forward;
    std::function<QString(const PageLocation&)> describe;

    void go(int index);
    void updateActions();
};

/** The state of the node for the pages: synchronized, catching up, without peers. */
class NodeStatus : public QObject
{
    Q_OBJECT

public:
    enum Level { Ok, Busy, Error };

    explicit NodeStatus(QObject *parent = nullptr);
    void setClientModel(ClientModel *model);
    void setWalletModel(WalletModel *model);

    Level level() const { return currentLevel; }
    /** A word or two for the pill: "Up to date", "Synchronizing 87%", "No connection" */
    QString text() const { return currentText; }
    /** A sentence for the tool tip */
    QString detail() const { return currentDetail; }

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void update();
    void tipChanged(int count, const QDateTime& blockDate, double progress, bool header);
    void walletProgress(const QString& title, int progress);

private:
    QPointer<ClientModel> clientModel;
    /** The tip as the last notification gave it: reading it from the chain would wait for the main lock */
    int tipHeight;
    QDateTime tipTime;
    int rescanProgress;
    Level currentLevel;
    QString currentText;
    QString currentDetail;
    QTimer *timer;
};

/** What a page itself is doing: reading something, read at a time, or failed. */
class PageActivity : public QObject
{
    Q_OBJECT

public:
    explicit PageActivity(QObject *parent = nullptr);

    /** The page can read its contents again: its status pill can be clicked, and F5 does it */
    void setRefreshable(bool refreshable);
    bool refreshable() const { return canRefresh; }

    /** The page started reading. Shown after a moment, so that quick reads do not blink. Every begin() needs a done() or failed(). */
    void begin(const QString& what = QString());
    /** The page has read what it said it would read with begin() */
    void done();
    /** The page has read its contents without having said so first: a periodic read, or one that took no time */
    void touch();
    /** The page could not read its contents */
    void failed(const QString& error);

    bool busy() const;
    QString busyText() const { return what; }
    QString error() const { return lastError; }
    /** When the page last read its contents; invalid when it never reported it */
    QDateTime updated() const { return lastUpdate; }

Q_SIGNALS:
    void changed();
    /** The user asked to read the contents again */
    void refreshRequested();

public Q_SLOTS:
    /** Read the contents again, as the user asked (the pill shows it for a moment) */
    void requestRefresh();

private:
    bool canRefresh;
    int pending;
    QString what;
    QString lastError;
    QDateTime lastUpdate;
    QElapsedTimer started;
    QTimer *delay;
    QTimer *hold;
};

/**
 * The status light next to the title of a page: green when the page shows what is
 * current, amber while the node or the page is still reading, red when the node has
 * no connection or the page could not read its contents. Clicking it reads the page
 * again when the page can do that.
 */
class PageStatusPill : public QAbstractButton
{
    Q_OBJECT

public:
    PageStatusPill(NodeStatus *node, PageActivity *activity, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private Q_SLOTS:
    void updateState();

private:
    QPointer<NodeStatus> node;
    QPointer<PageActivity> activity;
    NodeStatus::Level level;
    QString label;
    QString time;
    bool hovered;
};

namespace PageNav
{
    /** The back and forward buttons of a page header */
    QWidget *makeNavButtons(NavigationHistory *history, QWidget *parent = nullptr);
}

/** What a page header carries besides the title: back and forward, and the status pill of the page. */
struct PageChrome
{
    NavigationHistory *history;
    NodeStatus *node;
    PageActivity *activity;

    explicit PageChrome(NavigationHistory *_history = nullptr, NodeStatus *_node = nullptr, PageActivity *_activity = nullptr) :
        history(_history), node(_node), activity(_activity) {}
    /** The back and forward buttons; nullptr without a history */
    QWidget *navButtons(QWidget *parent) const;
    /** The status pill of the page; nullptr without the state of the node */
    QWidget *statusPill(QWidget *parent) const;
};

#endif // AOKCHAIN_QT_PAGENAV_H
