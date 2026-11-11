// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <cstring>
#include <cstdio>
#include "config/aokchain-config.h"
#endif

#include "utiltime.h"

#include <atomic>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

static std::atomic<int64_t> nMockTime(0); //!< For unit testing

int64_t GetTime()
{
    int64_t mocktime = nMockTime.load(std::memory_order_relaxed);
    if (mocktime) return mocktime;

    time_t now = time(nullptr);
    assert(now > 0);
    return now;
}

void SetMockTime(int64_t nMockTimeIn)
{
    nMockTime.store(nMockTimeIn, std::memory_order_relaxed);
}

int64_t GetMockTime()
{
    return nMockTime.load(std::memory_order_relaxed);
}

int64_t GetTimeMillis()
{
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                   boost::posix_time::ptime(boost::gregorian::date(1970,1,1))).total_milliseconds();
    assert(now > 0);
    return now;
}

int64_t GetTimeMicros()
{
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                   boost::posix_time::ptime(boost::gregorian::date(1970,1,1))).total_microseconds();
    assert(now > 0);
    return now;
}

int64_t GetSystemTimeInSeconds()
{
    return GetTimeMicros()/1000000;
}

void MilliSleep(int64_t n)
{

/**
 * Boost's sleep_for was uninterruptible when backed by nanosleep from 1.50
 * until fixed in 1.52. Use the deprecated sleep method for the broken case.
 * See: https://svn.boost.org/trac/boost/ticket/7238
 */
#if defined(HAVE_WORKING_BOOST_SLEEP_FOR)
    boost::this_thread::sleep_for(boost::chrono::milliseconds(n));
#elif defined(HAVE_WORKING_BOOST_SLEEP)
    boost::this_thread::sleep(boost::posix_time::milliseconds(n));
#else
//should never get here
#error missing boost sleep implementation
#endif
}

std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    // The format of the log and of the UpdateTip line is written without a stream and a locale:
    // those took a tenth of the time of a block during the first synchronization.
    if (strcmp(pszFormat, "%Y-%m-%d %H:%M:%S") == 0 && nTime >= 0 && nTime < 253402300800LL) {
        const int64_t days = nTime / 86400;
        const int seconds = (int)(nTime % 86400);
        // civil date from the days since 1970-01-01 (proleptic Gregorian calendar)
        const int64_t z = days + 719468;
        const int64_t era = z / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        const unsigned day = doy - (153 * mp + 2) / 5 + 1;
        const unsigned month = mp < 10 ? mp + 3 : mp - 9;
        const int64_t year = (int64_t)yoe + era * 400 + (month <= 2 ? 1 : 0);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u %02d:%02d:%02d", (int)year, month, day, seconds / 3600, (seconds / 60) % 60, seconds % 60);
        return buffer;
    }
    static std::locale classic(std::locale::classic());
    // std::locale takes ownership of the pointer
    std::locale loc(classic, new boost::posix_time::time_facet(pszFormat));
    std::stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}
