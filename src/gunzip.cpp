// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gunzip.h"

#include <cstring>
#include <vector>

namespace {

const int MAXBITS = 15;         // longest code of deflate
const int MAXLCODES = 286;      // literal/length codes
const int MAXDCODES = 30;       // distance codes
const int FIXLCODES = 288;      // codes of the fixed table
const size_t WINDOW = 32768;    // how far back a match can reach

struct Failure {
    const char* what;
};

/** A canonical Huffman code: the number of codes of each length, and the symbols in code order. */
struct Huffman {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
};

/**
 * Build the code from the code length of each symbol. Returns 0 for a complete code, a negative
 * number for lengths that cannot be a code (too many codes of some length), and a positive number
 * for an incomplete code.
 */
int Construct(Huffman& h, const short* length, int n)
{
    memset(h.count, 0, sizeof(h.count));
    for (int symbol = 0; symbol < n; symbol++)
        h.count[length[symbol]]++;
    if (h.count[0] == n)
        return 0; // no codes at all: complete, but decoding will fail
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0)
            return left;
    }
    short offs[MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h.count[len];
    for (int symbol = 0; symbol < n; symbol++) {
        if (length[symbol] != 0)
            h.symbol[offs[length[symbol]]++] = symbol;
    }
    return left;
}

class Inflater
{
public:
    Inflater(FILE* _in, FILE* _out, uint64_t _maxSize) :
        in(_in), out(_out), maxSize(_maxSize), inPos(0), inLen(0), bitBuf(0), bitCount(0), total(0), crc(0xffffffffU),
        window(WINDOW)
    {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xedb88320U ^ (c >> 1) : c >> 1;
            crcTable[n] = c;
        }
    }

    /** Throws Failure. Returns the number of bytes written. */
    uint64_t Run()
    {
        ReadHeader();
        bool last;
        do {
            last = Bits(1) != 0;
            const int type = Bits(2);
            if (type == 0)
                Stored();
            else if (type == 1)
                Fixed();
            else if (type == 2)
                Dynamic();
            else
                throw Failure{"invalid block type"};
        } while (!last);
        Flush();
        // the trailer starts at the next byte
        bitBuf = 0;
        bitCount = 0;
        uint32_t wantCrc = 0, wantSize = 0;
        for (int i = 0; i < 4; i++)
            wantCrc |= (uint32_t)Byte() << (8 * i);
        for (int i = 0; i < 4; i++)
            wantSize |= (uint32_t)Byte() << (8 * i);
        if (wantCrc != (crc ^ 0xffffffffU))
            throw Failure{"wrong CRC-32"};
        if (wantSize != (uint32_t)(total & 0xffffffffU))
            throw Failure{"wrong length"};
        return total;
    }

private:
    FILE* in;
    FILE* out;
    uint64_t maxSize;
    unsigned char inBuf[65536];
    size_t inPos, inLen;
    uint32_t bitBuf;
    int bitCount;
    uint64_t total;
    uint32_t crc;
    uint32_t crcTable[256];
    std::vector<unsigned char> window;

    int Byte()
    {
        if (inPos == inLen) {
            inLen = fread(inBuf, 1, sizeof(inBuf), in);
            inPos = 0;
            if (inLen == 0)
                throw Failure{"the file ends too early"};
        }
        return inBuf[inPos++];
    }

    /** The next need (at most 16) bits of the stream, lowest bit first. */
    int Bits(int need)
    {
        while (bitCount < need) {
            bitBuf |= (uint32_t)Byte() << bitCount;
            bitCount += 8;
        }
        const int value = (int)(bitBuf & ((1U << need) - 1));
        bitBuf >>= need;
        bitCount -= need;
        return value;
    }

    void Flush()
    {
        const size_t pending = (size_t)(total % WINDOW);
        WriteOut(pending);
    }

    void WriteOut(size_t n)
    {
        if (n == 0)
            return;
        for (size_t i = 0; i < n; i++)
            crc = crcTable[(crc ^ window[i]) & 0xff] ^ (crc >> 8);
        if (fwrite(window.data(), 1, n, out) != n)
            throw Failure{"cannot write the output file"};
    }

    void Put(unsigned char c)
    {
        if (total >= maxSize)
            throw Failure{"the decompressed data is longer than expected"};
        window[(size_t)(total % WINDOW)] = c;
        total++;
        if (total % WINDOW == 0)
            WriteOut(WINDOW);
    }

    void ReadHeader()
    {
        if (Byte() != 0x1f || Byte() != 0x8b)
            throw Failure{"not a gzip file"};
        if (Byte() != 8)
            throw Failure{"unknown compression method"};
        const int flags = Byte();
        for (int i = 0; i < 6; i++)
            Byte(); // time, extra flags, operating system
        if (flags & 0xe0)
            throw Failure{"unknown gzip flags"};
        if (flags & 4) { // extra field
            int len = Byte();
            len |= Byte() << 8;
            while (len-- > 0)
                Byte();
        }
        if (flags & 8) { // file name
            while (Byte() != 0) {}
        }
        if (flags & 16) { // comment
            while (Byte() != 0) {}
        }
        if (flags & 2) { // header CRC
            Byte();
            Byte();
        }
    }

    void Stored()
    {
        // the rest of the current byte is not used
        bitBuf = 0;
        bitCount = 0;
        int len = Byte();
        len |= Byte() << 8;
        int inverse = Byte();
        inverse |= Byte() << 8;
        if (len != (~inverse & 0xffff))
            throw Failure{"stored block with wrong length"};
        while (len-- > 0)
            Put((unsigned char)Byte());
    }

    int Decode(const Huffman& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= MAXBITS; len++) {
            code |= Bits(1);
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw Failure{"invalid code"};
    }

    void Codes(const Huffman& lencode, const Huffman& distcode)
    {
        static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                       7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        while (true) {
            int symbol = Decode(lencode);
            if (symbol < 256) {
                Put((unsigned char)symbol);
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29)
                    throw Failure{"invalid length code"};
                int len = lens[symbol] + Bits(lext[symbol]);
                symbol = Decode(distcode);
                if (symbol >= 30)
                    throw Failure{"invalid distance code"};
                const uint64_t dist = (uint64_t)dists[symbol] + (uint64_t)Bits(dext[symbol]);
                if (dist > total)
                    throw Failure{"distance reaches before the start"};
                while (len-- > 0)
                    Put(window[(size_t)((total - dist) % WINDOW)]);
            }
        }
    }

    void Fixed()
    {
        Huffman lencode, distcode;
        short lengths[FIXLCODES];
        int symbol = 0;
        for (; symbol < 144; symbol++) lengths[symbol] = 8;
        for (; symbol < 256; symbol++) lengths[symbol] = 9;
        for (; symbol < 280; symbol++) lengths[symbol] = 7;
        for (; symbol < FIXLCODES; symbol++) lengths[symbol] = 8;
        Construct(lencode, lengths, FIXLCODES);
        for (symbol = 0; symbol < MAXDCODES; symbol++) lengths[symbol] = 5;
        Construct(distcode, lengths, MAXDCODES);
        Codes(lencode, distcode);
    }

    void Dynamic()
    {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = Bits(5) + 257;
        const int ndist = Bits(5) + 1;
        const int ncode = Bits(4) + 4;
        if (nlen > MAXLCODES || ndist > MAXDCODES)
            throw Failure{"too many codes"};
        short lengths[MAXLCODES + MAXDCODES];
        int index;
        for (index = 0; index < ncode; index++)
            lengths[order[index]] = (short)Bits(3);
        for (; index < 19; index++)
            lengths[order[index]] = 0;
        Huffman lencode, distcode;
        if (Construct(lencode, lengths, 19) != 0)
            throw Failure{"invalid code lengths"};
        index = 0;
        while (index < nlen + ndist) {
            int symbol = Decode(lencode);
            if (symbol < 16) {
                lengths[index++] = (short)symbol;
            } else {
                int len = 0, repeat;
                if (symbol == 16) {
                    if (index == 0)
                        throw Failure{"repeat without a length"};
                    len = lengths[index - 1];
                    repeat = 3 + Bits(2);
                } else if (symbol == 17) {
                    repeat = 3 + Bits(3);
                } else {
                    repeat = 11 + Bits(7);
                }
                if (index + repeat > nlen + ndist)
                    throw Failure{"too many code lengths"};
                while (repeat-- > 0)
                    lengths[index++] = (short)len;
            }
        }
        if (lengths[256] == 0)
            throw Failure{"no end of block code"};
        int err = Construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
            throw Failure{"invalid literal/length code"};
        err = Construct(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
            throw Failure{"invalid distance code"};
        Codes(lencode, distcode);
    }
};

} // namespace

bool GunzipFile(FILE* in, FILE* out, uint64_t maxSize, uint64_t& size, std::string& error)
{
    try {
        Inflater inflater(in, out, maxSize);
        size = inflater.Run();
        return true;
    } catch (const Failure& failure) {
        error = failure.what;
        return false;
    }
}
