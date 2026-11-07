// Small zlib/DEFLATE decoder (RFC 1950 / 1951) used for imported car textures.
#include "inflate.h"
#include <cstring>

namespace {

struct State {
    const uint8_t* in;
    size_t inLen, inPos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;
    std::vector<uint8_t>* out;
    bool err = false;
};

int bits(State& s, int need) {
    uint32_t v = s.bitBuf;
    while (s.bitCnt < need) {
        if (s.inPos >= s.inLen) { s.err = true; return 0; }
        v |= uint32_t(s.in[s.inPos++]) << s.bitCnt;
        s.bitCnt += 8;
    }
    s.bitBuf = v >> need;
    s.bitCnt -= need;
    return int(v & ((1u << need) - 1));
}

struct Huffman {
    short count[16];
    short symbol[320];
};

int decode(State& s, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1);
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (s.err) return -1;
    }
    return -1;
}

int construct(Huffman& h, const short* length, int n) {
    std::memset(h.count, 0, sizeof(h.count));
    for (int i = 0; i < n; i++) h.count[length[i]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = short(offs[len] + h.count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) h.symbol[offs[length[i]]++] = short(i);
    return left;
}

const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(State& s, const Huffman& lencode, const Huffman& distcode) {
    std::vector<uint8_t>& o = *s.out;
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0 || s.err) return false;
        if (sym < 256) {
            o.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int len = kLenBase[sym] + bits(s, kLenExtra[sym]);
            int ds = decode(s, distcode);
            if (ds < 0 || ds >= 30) return false;
            size_t dist = size_t(kDistBase[ds] + bits(s, kDistExtra[ds]));
            if (dist > o.size()) return false;
            size_t from = o.size() - dist;
            for (int i = 0; i < len; i++) o.push_back(o[from + i]);
        }
    }
}

bool stored(State& s) {
    s.bitBuf = 0;
    s.bitCnt = 0;
    if (s.inPos + 4 > s.inLen) return false;
    unsigned len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
    unsigned nlen = s.in[s.inPos + 2] | (s.in[s.inPos + 3] << 8);
    s.inPos += 4;
    if (len != (~nlen & 0xffff) || s.inPos + len > s.inLen) return false;
    s.out->insert(s.out->end(), s.in + s.inPos, s.in + s.inPos + len);
    s.inPos += len;
    return true;
}

bool fixedBlock(State& s) {
    static Huffman lencode, distcode;
    static bool built = false;
    if (!built) {
        short lengths[320];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < 288; sym++) lengths[sym] = 8;
        construct(lencode, lengths, 288);
        for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
        construct(distcode, lengths, 30);
        built = true;
    }
    return codes(s, lencode, distcode);
}

bool dynamicBlock(State& s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) return false;
    int index = 0;
    for (; index < ncode; index++) lengths[order[index]] = short(bits(s, 3));
    for (; index < 19; index++) lengths[order[index]] = 0;
    Huffman lencode, distcode;
    if (construct(lencode, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, lencode);
        if (sym < 0 || s.err) return false;
        if (sym < 16) {
            lengths[index++] = short(sym);
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                rep = 3 + bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + bits(s, 3);
            } else {
                rep = 11 + bits(s, 7);
            }
            if (index + rep > nlen + ndist) return false;
            while (rep--) lengths[index++] = short(len);
        }
    }
    if (lengths[256] == 0) return false;
    int err = construct(lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
    err = construct(distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
    return codes(s, lencode, distcode);
}

}  // namespace

bool zlibInflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expected) {
    if (n < 2) return false;
    if ((src[0] & 0x0f) != 8 || ((src[0] << 8) | src[1]) % 31 != 0) return false;
    State s;
    s.in = src + 2;
    s.inLen = n - 2;
    s.out = &out;
    out.clear();
    if (expected) out.reserve(expected);
    int last;
    do {
        last = bits(s, 1);
        int type = bits(s, 2);
        bool ok = type == 0 ? stored(s) : type == 1 ? fixedBlock(s) : type == 2 ? dynamicBlock(s) : false;
        if (!ok || s.err) return false;
    } while (!last);
    return expected == 0 || out.size() == expected;
}
