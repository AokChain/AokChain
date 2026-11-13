// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_BOOTSTRAPFETCH_H
#define AOKCHAIN_QT_BOOTSTRAPFETCH_H

#include <atomic>
#include <cstdint>
#include <string>

/**
 * The fetcher of the block bootstrap for the GUI (see BootstrapFetcher in
 * bootstrap.h): the network stack of Qt, so that https:// addresses work,
 * with the certificate store of the system, and redirects are followed
 * (never from https to http). A file host such as the release assets of
 * GitHub needs both. A partial file is resumed with a Range request.
 *
 * Runs on the download thread of the bootstrap with an event loop of its own.
 */
bool QtBootstrapFetch(const std::string& url, const std::string& path, uint64_t maxBytes,
                      std::atomic<uint64_t>* progress, std::string* body, std::string& error);

#endif // AOKCHAIN_QT_BOOTSTRAPFETCH_H
