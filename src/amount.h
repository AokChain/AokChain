// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2014 The BlackCoin developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_AMOUNT_H
#define AOKCHAIN_AMOUNT_H

#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <limits>

/** Amount in corbies (Can be negative) */
typedef int64_t CAmount;

static const CAmount COIN = 100000000;
static const CAmount CENT = 1000000;

/** No amount larger than this (in satoshi) is valid.*/
/** No amount of AOK is larger than this: more than ten times the coins that will ever exist
 *  (2.1 billion). It was the largest 64 bit number, which made the range checks pointless: sums
 *  could overflow before a check saw them. */
static const CAmount MAX_MONEY = 25000000000 * COIN;
static const CAmount MAX_MONEY_TOKENS = 25000000000 * COIN;

inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  AOKCHAIN_AMOUNT_H
