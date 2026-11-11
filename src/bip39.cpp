// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bip39.h>

#include <bip39_wordlists.h>
#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <support/cleanse.h>
#include <tinyformat.h>

#include <map>
#include <string.h>

namespace bip39 {

namespace {

/** Decode one UTF-8 character; returns false on malformed input. */
bool DecodeUTF8(const std::string& str, size_t& pos, uint32_t& cp)
{
    const unsigned char c = str[pos];
    size_t len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else return false;
    if (pos + len > str.size())
        return false;
    for (size_t i = 1; i < len; i++) {
        const unsigned char cc = str[pos + i];
        if ((cc & 0xC0) != 0x80)
            return false;
        cp = (cp << 6) | (cc & 0x3F);
    }
    // overlong encodings
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000))
        return false;
    pos += len;
    return true;
}

void EncodeUTF8(uint32_t cp, std::string& out)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

const std::map<std::string, int>& WordIndex(const char* const* list)
{
    static std::map<const char* const*, std::map<std::string, int>> cache;
    std::map<std::string, int>& index = cache[list];
    if (index.empty()) {
        for (int i = 0; i < 2048; i++)
            index[list[i]] = i;
    }
    return index;
}

} // namespace

bool Normalize(const std::string& str, bool fWords, std::string& out)
{
    out.clear();
    bool fPendingSpace = false;
    size_t pos = 0;
    while (pos < str.size()) {
        uint32_t cp;
        if (!DecodeUTF8(str, pos, cp))
            return false;
        if (fWords && (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x3000)) {
            fPendingSpace = !out.empty();
            continue;
        }
        if (fPendingSpace) {
            out += ' ';
            fPendingSpace = false;
        }
        if (cp < 0x80) {
            if (fWords && cp >= 'A' && cp <= 'Z')
                cp += 'a' - 'A';
            out += (char)cp;
        } else if (cp >= 0xAC00 && cp <= 0xD7A3) {
            // Hangul syllable: canonical decomposition into conjoining jamo
            const uint32_t s = cp - 0xAC00;
            EncodeUTF8(0x1100 + s / 588, out);
            EncodeUTF8(0x1161 + (s % 588) / 28, out);
            if (s % 28 != 0)
                EncodeUTF8(0x11A7 + s % 28, out);
        } else if (cp >= 0x1100 && cp <= 0x11FF) {
            // conjoining jamo are already decomposed
            EncodeUTF8(cp, out);
        } else {
            return false;
        }
    }
    return true;
}

bool CheckMnemonic(const std::string& mnemonic, std::string& language, std::string& error)
{
    std::vector<std::string> words;
    size_t start = 0;
    while (start <= mnemonic.size()) {
        size_t end = mnemonic.find(' ', start);
        if (end == std::string::npos)
            end = mnemonic.size();
        if (end > start)
            words.push_back(mnemonic.substr(start, end - start));
        start = end + 1;
    }
    if (words.size() < 12 || words.size() > 24 || words.size() % 3 != 0) {
        error = strprintf("a mnemonic has 12, 15, 18, 21 or 24 words, this one has %u", words.size());
        return false;
    }

    static const struct { const char* name; const char* const* list; } lists[] = {
        {"english", bip39_wordlist_english},
        {"korean", bip39_wordlist_korean},
    };
    std::vector<int> indexes;
    std::string strUnknown;
    for (const auto& item : lists) {
        const std::map<std::string, int>& index = WordIndex(item.list);
        indexes.clear();
        size_t i = 0;
        for (; i < words.size(); i++) {
            std::map<std::string, int>::const_iterator it = index.find(words[i]);
            if (it == index.end())
                break;
            indexes.push_back(it->second);
        }
        if (i == words.size()) {
            language = item.name;
            break;
        }
        // remember the first unknown word of the list that matched best
        if (i > 0 || strUnknown.empty())
            strUnknown = strprintf("word %u (\"%s\") is not in the %s word list", i + 1, words[i], item.name);
    }
    if (indexes.size() != words.size()) {
        error = strUnknown;
        return false;
    }

    // 11 bits per word: entropy followed by the first ENT/32 bits of its SHA256
    std::vector<unsigned char> bits((words.size() * 11 + 7) / 8, 0);
    for (size_t i = 0; i < indexes.size(); i++) {
        for (int b = 0; b < 11; b++) {
            if (indexes[i] & (1 << (10 - b))) {
                const size_t n = i * 11 + b;
                bits[n / 8] |= 1 << (7 - n % 8);
            }
        }
    }
    const size_t nChecksumBits = words.size() / 3;
    const size_t nEntropyBytes = (words.size() * 11 - nChecksumBits) / 8;
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(bits.data(), nEntropyBytes).Finalize(hash);
    const unsigned char mask = 0xFF << (8 - nChecksumBits);
    const bool fOk = (bits[nEntropyBytes] & mask) == (hash[0] & mask);
    memory_cleanse(bits.data(), bits.size());
    if (!fOk) {
        error = "the checksum of the mnemonic is wrong (a word is mistyped, missing or out of order)";
        return false;
    }
    return true;
}

std::vector<unsigned char> MnemonicToSeed(const std::string& mnemonic, const std::string& passphrase)
{
    // PBKDF2 with HMAC-SHA512, 2048 rounds; the 64 byte result is one block
    const std::string salt = "mnemonic" + passphrase;
    static const unsigned char block[4] = {0, 0, 0, 1};
    unsigned char u[CHMAC_SHA512::OUTPUT_SIZE];
    std::vector<unsigned char> seed(CHMAC_SHA512::OUTPUT_SIZE);
    CHMAC_SHA512((const unsigned char*)mnemonic.data(), mnemonic.size())
        .Write((const unsigned char*)salt.data(), salt.size())
        .Write(block, sizeof(block))
        .Finalize(u);
    memcpy(seed.data(), u, sizeof(u));
    for (int i = 1; i < 2048; i++) {
        CHMAC_SHA512((const unsigned char*)mnemonic.data(), mnemonic.size()).Write(u, sizeof(u)).Finalize(u);
        for (size_t j = 0; j < sizeof(u); j++)
            seed[j] ^= u[j];
    }
    memory_cleanse(u, sizeof(u));
    return seed;
}

bool ParsePath(const std::string& path, std::vector<uint32_t>& out)
{
    out.clear();
    if (path.empty() || (path[0] != 'm' && path[0] != 'M'))
        return false;
    size_t pos = 1;
    while (pos < path.size()) {
        if (path[pos] != '/')
            return false;
        pos++;
        uint64_t n = 0;
        size_t nDigits = 0;
        while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9') {
            n = n * 10 + (path[pos] - '0');
            if (n >= 0x80000000ULL)
                return false;
            pos++;
            nDigits++;
        }
        if (nDigits == 0)
            return false;
        if (pos < path.size() && (path[pos] == '\'' || path[pos] == 'h' || path[pos] == 'H')) {
            n |= 0x80000000ULL;
            pos++;
        }
        out.push_back((uint32_t)n);
    }
    return true;
}

std::string FormatPath(const std::vector<uint32_t>& path)
{
    std::string ret = "m";
    for (uint32_t n : path) {
        ret += strprintf("/%u", n & 0x7FFFFFFF);
        if (n & 0x80000000)
            ret += "'";
    }
    return ret;
}

std::vector<PathLayout> CandidateLayouts(bool fAllCoinTypes)
{
    const uint32_t H = 0x80000000;
    std::vector<PathLayout> layouts;
    // Layouts without a coin type
    const std::vector<std::vector<uint32_t>> simple = {
        {}, {0}, {0 | H}, {0 | H, 0}, {0 | H, 1}, {0 | H, 0 | H}, {0 | H, 1 | H}, {0, 0}, {0, 1},
    };
    for (const std::vector<uint32_t>& path : simple) {
        layouts.push_back({path, 100, false});
        layouts.push_back({path, 100, true});
    }
    // BIP44, and the same layout under the BIP49 and BIP84 purposes for
    // the common coin types. Likely coin types first.
    std::vector<uint32_t> coins = {0, 1, 175, 2, 5, 3, 7, 20, 28, 88, 105, 119, 2301};
    const size_t nCommon = coins.size();
    if (fAllCoinTypes) {
        for (uint32_t c = 0; c < 2048; c++) {
            if (std::find(coins.begin(), coins.begin() + nCommon, c) == coins.begin() + nCommon)
                coins.push_back(c);
        }
    }
    for (size_t n = 0; n < coins.size(); n++) {
        for (uint32_t purpose : {44u, 49u, 84u}) {
            if (purpose != 44 && n >= nCommon)
                continue;
            for (uint32_t account = 0; account < 2; account++) {
                for (uint32_t change = 0; change < 2; change++)
                    layouts.push_back({{purpose | H, coins[n] | H, account | H, change}, 20, false});
                // without the change level
                layouts.push_back({{purpose | H, coins[n] | H, account | H}, 20, false});
            }
        }
    }
    return layouts;
}

} // namespace bip39
