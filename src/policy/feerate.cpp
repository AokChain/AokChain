// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "feerate.h"

#include "tinyformat.h"

#include <limits>

const std::string CURRENCY_UNIT = "AOK";

CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nBytes_)
{
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    const int64_t nLimit = std::numeric_limits<int64_t>::max() / 1000;
    if (nSize <= 0) {
        nSatoshisPerK = 0;
    } else if (nFeePaid <= nLimit && nFeePaid >= -nLimit) {
        nSatoshisPerK = nFeePaid * 1000 / nSize;
    } else {
        // nFeePaid * 1000 does not fit in 64 bits (amounts above 92 million
        // AOK): divide first, and stop at the largest rate
        const int64_t nPerByte = nFeePaid / nSize;
        if (nPerByte > nLimit)
            nSatoshisPerK = std::numeric_limits<int64_t>::max();
        else if (nPerByte < -nLimit)
            nSatoshisPerK = -std::numeric_limits<int64_t>::max();
        else
            nSatoshisPerK = nPerByte * 1000;
    }
}

CAmount CFeeRate::GetFee(size_t nBytes_) const
{
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    CAmount nFee = nSatoshisPerK * nSize / 1000;

    if (nFee == 0 && nSize != 0) {
        if (nSatoshisPerK > 0)
            nFee = CAmount(1);
        if (nSatoshisPerK < 0)
            nFee = CAmount(-1);
    }

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d %s/kB", nSatoshisPerK / COIN, nSatoshisPerK % COIN, CURRENCY_UNIT);
}
