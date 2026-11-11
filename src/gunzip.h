// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_GUNZIP_H
#define AOKCHAIN_GUNZIP_H

#include <cstdint>
#include <cstdio>
#include <string>

/**
 * Decompress a gzip file (RFC 1952 with a deflate stream, RFC 1951).
 *
 * A small decoder of its own, so that the node needs no compression library
 * for the block archives of the bootstrap. The input is not trusted: every
 * length and distance is checked, the output is limited to maxSize bytes, and
 * the CRC-32 and the length at the end of the file must match.
 *
 * @param[in]  in       the compressed file, read from its current position
 * @param[in]  out      where the decompressed bytes are written
 * @param[in]  maxSize  the output may not be longer than this
 * @param[out] size     number of bytes written
 * @param[out] error    what is wrong with the file, when false is returned
 */
bool GunzipFile(FILE* in, FILE* out, uint64_t maxSize, uint64_t& size, std::string& error);

#endif // AOKCHAIN_GUNZIP_H
