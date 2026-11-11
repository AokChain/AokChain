// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_TOKENLISTS_H
#define AOKCHAIN_QT_TOKENLISTS_H

#include <QAbstractListModel>
#include <QDialog>
#include <QObject>
#include <QString>
#include <QStyledItemDelegate>
#include <QVector>

class TokenTableModel;

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
QT_END_NAMESPACE

/**
 * A wallet can hold tens of thousands of sub tokens ("ROOT/SUB") and unique
 * tokens ("ROOT#TAG"). The pages therefore show root tokens only; what is
 * below a root is reached through a search or through a list of its own.
 * These lists are models with a view: no widget is made per token.
 */
namespace TokenNames
{
    /** The part before the first '/', '#' or '!' */
    QString root(const QString& name);
    /** A sub token or a unique token */
    bool isChild(const QString& name);
    /** Does the name belong to the root: the root itself, its owner token, or something below it? */
    bool belongsTo(const QString& name, const QString& root);
}

/** One held token. The owner token "NAME!" is not an entry of its own. */
struct HeldToken {
    QString name;       //!< without "!" and without the " (LOCKED)" of the table model
    QString amount;     //!< formatted, without separators; empty when only the owner token is held
    bool held;          //!< false: only the owner token is held
    bool admin;         //!< the owner token is held
    bool locked;        //!< a locked balance
    HeldToken() : held(false), admin(false), locked(false) {}
};

/** A root token as the pages show it. */
struct RootToken {
    QString name;
    QString amount;     //!< of the root itself
    bool held;          //!< the wallet holds the root itself
    bool admin;
    bool locked;
    int children;       //!< held sub and unique tokens below it
    RootToken() : held(false), admin(false), locked(false), children(0) {}
};

/** The held tokens of the wallet, grouped by root. Follows the token table model. */
class HeldTokenIndex : public QObject
{
    Q_OBJECT

public:
    explicit HeldTokenIndex(QObject *parent = nullptr);
    void setSourceModel(TokenTableModel *model);

    const QVector<RootToken>& roots() const { return rootList; }
    const RootToken *findRoot(const QString& name) const;
    /** Held sub and unique tokens of a root whose name contains the text, at most cap of them */
    QVector<HeldToken> children(const QString& root, const QString& text, int cap, bool *more) const;
    /** Held tokens (not the locked balances) whose name contains the text, at most cap of them */
    QVector<HeldToken> search(const QString& text, int cap, bool *more) const;
    /** A held token by its name */
    bool find(const QString& name, HeldToken *token) const;
    int count() const { return all.size(); }

Q_SIGNALS:
    void changed();

public Q_SLOTS:
    void rebuild();

private:
    TokenTableModel *source;
    QVector<HeldToken> all;         //!< sorted by name
    QVector<RootToken> rootList;    //!< sorted by name
};

/** A list of tokens for a view: name, kind, amount. */
class TokenListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles { NameRole = Qt::UserRole + 1, AmountRole, HeldRole, AdminRole, LockedRole, ChildrenRole };

    explicit TokenListModel(QObject *parent = nullptr);
    void setTokens(const QVector<HeldToken>& tokens);
    void setRoots(const QVector<RootToken>& roots);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;

private:
    struct Row { QString name, amount; bool held, admin, locked; int children; };
    QVector<Row> rows;
};

/** Paints a row of a TokenListModel: logo, name, chips, amount. */
class TokenRowDelegate : public QStyledItemDelegate
{
public:
    explicit TokenRowDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/**
 * A window with a search field and a list of held tokens.
 * Without a root: the root tokens; typing also finds sub and unique tokens.
 * With a root: the sub and unique tokens of that root.
 */
class TokenListDialog : public QDialog
{
    Q_OBJECT

public:
    enum Purpose { Choose, Browse };
    TokenListDialog(HeldTokenIndex *index, const QString& root, Purpose purpose, QWidget *parent = nullptr);
    /** The token that was chosen (Choose) or is to be sent (Browse); empty when none */
    QString chosen() const { return result; }

private Q_SLOTS:
    void refill();
    void acceptCurrent();
    void selectionChanged();

private:
    HeldTokenIndex *index;
    QString root;
    QString result;
    TokenListModel *model;
    QLineEdit *search;
    QListView *view;
    QLabel *hint;
    QPushButton *actionButton;
};

#endif // AOKCHAIN_QT_TOKENLISTS_H
