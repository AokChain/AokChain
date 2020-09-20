// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_AOKCHAINADDRESSVALIDATOR_H
#define AOKCHAIN_QT_AOKCHAINADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class AokChainAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit AokChainAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** AokChain address widget validator, checks for a valid aokchain address.
 */
class AokChainAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit AokChainAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // AOKCHAIN_QT_AOKCHAINADDRESSVALIDATOR_H
