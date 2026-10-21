#pragma once
// ============================================================
// Microsoft Edge TTS "Sec-MS-GEC" v2 token
//
// Ported from rany2/edge-tts 7.3.1 (drm.py). The v2 token has
// the form "<CS64>-<SHA256>" and the endpoint rejects the old
// v1 tokens with HTTP 403.
//
// The digest is a modified SHA-256: the message is split so the
// first seven bytes land at the end of the first block, and the
// round constants of block one differ from the standard ones.
// This file is a literal port so the digests match bit for bit.
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace edge_drm {

static const uint32_t U32_MASK = 0xFFFFFFFFu;

static const uint32_t INITIAL[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

static const uint32_t STATE[8] = {
    0xFD9FBEA2u, 0x91243CE7u, 0xBE499094u, 0xCD2E1E0Au,
    0x6C006F49u, 0x04BD35ABu, 0xFEB0A7E4u, 0x03EFFBD2u
};

static const uint32_t SCHEDULE[14] = {
    0xEB24BF9Eu, 0x1558691Fu, 0xA0C09EE2u, 0x0CFA32BAu,
    0xA622EE8Cu, 0xD1DD9491u, 0xC0DD25A0u, 0xF9E70AB6u,
    0xF0C31573u, 0x7058CE4Eu, 0x7188E606u, 0x00E5AB33u,
    0xD258B060u, 0x72766564u
};

static const uint32_t ROUND_CONSTANTS[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

// Block one swaps round constant 14 for the adjustment value.
static const uint32_t BLOCK_CONSTANTS[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0xC9DC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static const uint32_t CS64_COEFFICIENTS[14][2] = {
    {0xE82E91B1u, 0xB32700D0u}, {0x44DE24C6u, 0x88DFF51Du},
    {0x3BEB65BCu, 0x29514038u}, {0xC89C5933u, 0xB328CBDFu},
    {0x8D1EC211u, 0xD405B4D5u}, {0x314EF5D5u, 0x26E55F3Cu},
    {0xE92491B1u, 0x607801C6u}, {0xF0A5B9D5u, 0x82B2C1FBu},
    {0x06064CF3u, 0xFA51A3DAu}, {0xDA79FA60u, 0x207CC464u},
    {0xBED20560u, 0x3C3C74F3u}, {0x89F9C9F8u, 0xD2D14B61u},
    {0x35FA38C6u, 0xBB04A2C1u}, {0xCB49E3C6u, 0xC5626167u}
};

static const uint32_t ADJUSTMENT = 0xC9DC06A7u;
static const uint32_t SIGMA0_ADJUSTMENT = 0x059C0B80u;
static const uint32_t SIGMA1_ADJUSTMENT = 0x000B92C0u;
static const uint32_t CS64_FACTOR = 0x82893933u;
static const uint32_t CS64_ADJUSTMENT0 = 0xF2F90B1Au;
static const uint32_t CS64_ADJUSTMENT1 = 0x0B1A0000u;

static inline uint32_t rol16(uint32_t value) {
    value &= U32_MASK;
    return ((value << 16) | (value >> 16)) & U32_MASK;
}

static inline uint32_t ror(uint32_t value, int n) {
    return ((value >> n) | (value << (32 - n))) & U32_MASK;
}

// Sigma helpers used by the v2 digest. Truncating each term to 32 bits
// matches Python's final mask because XOR is bitwise.
static inline uint32_t sigma0_custom(uint32_t value) {
    return ((value >> 7) ^ (value << 25) ^ (value >> 18) ^ (value << 14) ^
            (value >> 3)) & U32_MASK;
}

static inline uint32_t sigma1_custom(uint32_t value) {
    return ((value >> 17) ^ (value << 15) ^ (value >> 19) ^ (value << 13) ^
            (value >> 10)) & U32_MASK;
}

static inline uint32_t sigma0_std(uint32_t value) {
    return sigma0_custom(value);
}

static inline uint32_t sigma1_std(uint32_t value) {
    return sigma1_custom(value);
}

static void expand_schedule(std::vector<uint32_t>& words, int first_word) {
    for (int index = first_word; index < 64; index++) {
        uint32_t x = words[index - 2];
        uint32_t y = words[index - 15];
        uint32_t s1 = sigma1_std(x);
        uint32_t s0 = sigma0_std(y);
        words.push_back((s1 + words[index - 7] + s0 + words[index - 16]) & U32_MASK);
    }
}

// SHA-256 compression rounds. The upstream implementation unrolls the loop
// and rotates the register names, so the unrolled body below is a literal
// port - a plain round sequence does not produce the same digest.
static void sha256_rounds(const uint32_t state[8], const std::vector<uint32_t>& words,
                          int first_round, uint32_t out[8]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    const uint32_t* K = (first_round == 14) ? BLOCK_CONSTANTS : ROUND_CONSTANTS;

    int aligned = ((first_round + 7) / 8) * 8;
    if (aligned > 64) aligned = 64;

    for (int index = first_round; index < aligned; index++) {
        uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) +
                      (g ^ (e & (f ^ g))) + K[index] + words[index];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) +
                      ((a & b) | (c & (a | b)));
        uint32_t na = (t1 + t2) & U32_MASK;
        uint32_t ne = (d + t1) & U32_MASK;
        d = c; c = b; b = a; a = na;
        h = g; g = f; f = e; e = ne;
    }

    for (int index = aligned; index < 64; index += 8) {
        uint32_t t1, t2;

        t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) +
             (g ^ (e & (f ^ g))) + K[index] + words[index];
        d = (d + t1) & U32_MASK;
        h = (t1 + (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) +
             ((a & b) | (c & (a | b)))) & U32_MASK;

        t1 = g + (ror(d, 6) ^ ror(d, 11) ^ ror(d, 25)) +
             (f ^ (d & (e ^ f))) + K[index + 1] + words[index + 1];
        c = (c + t1) & U32_MASK;
        g = (t1 + (ror(h, 2) ^ ror(h, 13) ^ ror(h, 22)) +
             ((h & a) | (b & (h | a)))) & U32_MASK;

        t1 = f + (ror(c, 6) ^ ror(c, 11) ^ ror(c, 25)) +
             (e ^ (c & (d ^ e))) + K[index + 2] + words[index + 2];
        b = (b + t1) & U32_MASK;
        f = (t1 + (ror(g, 2) ^ ror(g, 13) ^ ror(g, 22)) +
             ((g & h) | (a & (g | h)))) & U32_MASK;

        t1 = e + (ror(b, 6) ^ ror(b, 11) ^ ror(b, 25)) +
             (d ^ (b & (c ^ d))) + K[index + 3] + words[index + 3];
        a = (a + t1) & U32_MASK;
        e = (t1 + (ror(f, 2) ^ ror(f, 13) ^ ror(f, 22)) +
             ((f & g) | (h & (f | g)))) & U32_MASK;

        t1 = d + (ror(a, 6) ^ ror(a, 11) ^ ror(a, 25)) +
             (c ^ (a & (b ^ c))) + K[index + 4] + words[index + 4];
        h = (h + t1) & U32_MASK;
        d = (t1 + (ror(e, 2) ^ ror(e, 13) ^ ror(e, 22)) +
             ((e & f) | (g & (e | f)))) & U32_MASK;

        t1 = c + (ror(h, 6) ^ ror(h, 11) ^ ror(h, 25)) +
             (b ^ (h & (a ^ b))) + K[index + 5] + words[index + 5];
        g = (g + t1) & U32_MASK;
        c = (t1 + (ror(d, 2) ^ ror(d, 13) ^ ror(d, 22)) +
             ((d & e) | (f & (d | e)))) & U32_MASK;

        t1 = b + (ror(g, 6) ^ ror(g, 11) ^ ror(g, 25)) +
             (a ^ (g & (h ^ a))) + K[index + 6] + words[index + 6];
        f = (f + t1) & U32_MASK;
        b = (t1 + (ror(c, 2) ^ ror(c, 13) ^ ror(c, 22)) +
             ((c & d) | (e & (c | d)))) & U32_MASK;

        t1 = a + (ror(f, 6) ^ ror(f, 11) ^ ror(f, 25)) +
             (h ^ (f & (g ^ h))) + K[index + 7] + words[index + 7];
        e = (e + t1) & U32_MASK;
        a = (t1 + (ror(b, 2) ^ ror(b, 13) ^ ror(b, 22)) +
             ((b & c) | (d & (b | c)))) & U32_MASK;
    }

    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
    out[4] = e; out[5] = f; out[6] = g; out[7] = h;
}

// Initial state for the v2 digest, derived from the first seven bytes.
static void sha256_init(const unsigned char* head7, uint32_t out[8]) {
    std::vector<uint32_t> words(14, 0);
    words.push_back(((uint32_t)head7[0] << 16) | ((uint32_t)head7[1] << 8) |
                     (uint32_t)head7[2]);
    words.push_back(((uint32_t)head7[3] << 24) | ((uint32_t)head7[4] << 16) |
                     ((uint32_t)head7[5] << 8) | (uint32_t)head7[6]);

    for (int index = 16; index < 30; index++) {
        uint32_t value = SCHEDULE[index - 16];
        if (index - 2 >= 14) {
            uint32_t s = sigma1_custom(words[index - 2]);
            if (index == 16) s ^= SIGMA1_ADJUSTMENT;
            value += s;
        }
        if (index - 7 >= 14) value += words[index - 7];
        if (index - 15 >= 14) {
            uint32_t s = sigma0_custom(words[index - 15]);
            if (index == 29) s ^= SIGMA0_ADJUSTMENT;
            value += s;
        }
        words.push_back(value & U32_MASK);
    }

    words.push_back((sigma1_custom(words[28]) + words[23] + sigma0_custom(words[15]) +
                     words[14] + ADJUSTMENT - ROUND_CONSTANTS[14]) & U32_MASK);

    expand_schedule(words, 31);

    uint32_t working[8];
    sha256_rounds(STATE, words, 14, working);
    for (int i = 0; i < 8; i++) out[i] = (INITIAL[i] + working[i]) & U32_MASK;
}

static void sha256_digest(const std::string& data, unsigned char out[32]) {
    uint64_t length = 57 + (uint64_t)data.size();

    std::string padded = data.substr(7);
    padded.push_back((char)0x80);
    size_t zeros = (size_t)((55 + 64 - (length % 64)) % 64);
    padded.append(zeros, '\0');
    uint64_t bits = length * 8;
    for (int i = 7; i >= 0; i--) padded.push_back((char)((bits >> (i * 8)) & 0xFF));

    uint32_t state[8];
    sha256_init((const unsigned char*)data.data(), state);

    for (size_t off = 0; off < padded.size(); off += 64) {
        uint32_t head[16];
        for (int i = 0; i < 16; i++) {
            const unsigned char* p = (const unsigned char*)padded.data() + off + i * 4;
            head[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
        }
        std::vector<uint32_t> words(head, head + 16);
        expand_schedule(words, 16);

        uint32_t working[8];
        sha256_rounds(state, words, 0, working);
        for (int i = 0; i < 8; i++) state[i] = (state[i] + working[i]) & U32_MASK;
    }

    for (int i = 0; i < 8; i++)
        for (int b = 0; b < 4; b++)
            out[i * 4 + b] = (unsigned char)((state[i] >> (24 - b * 8)) & 0xFF);
}

// Custom 64-bit digest used by the v2 Sec-MS-GEC token.
static void cs64_digest(const std::string& data, const unsigned char* sha256,
                        uint32_t out[2]) {
    uint32_t key_a = (uint32_t)sha256[0] | ((uint32_t)sha256[1] << 8) |
                     ((uint32_t)sha256[2] << 16) | ((uint32_t)sha256[3] << 24);
    uint32_t key_b = (uint32_t)sha256[4] | ((uint32_t)sha256[5] << 8) |
                     ((uint32_t)sha256[6] << 16) | ((uint32_t)sha256[7] << 24);
    key_a |= 1u;
    key_b |= 1u;

    uint32_t factors[2] = {
        (uint32_t)(((uint64_t)key_a * CS64_FACTOR) & U32_MASK),
        (uint32_t)(((uint64_t)key_b * CS64_FACTOR) & U32_MASK)
    };

    std::vector<uint32_t> terms;
    std::vector<uint32_t> offsets;
    terms.reserve(64);
    offsets.reserve(64);

    for (int i = 0; i < 14; i++) {
        terms.push_back((uint32_t)(((uint64_t)CS64_COEFFICIENTS[i][0] *
                                    factors[i % 2]) & U32_MASK));
        offsets.push_back(CS64_COEFFICIENTS[i][1]);
    }

    // Little-endian uint32 view of "\x00" + data, zero padded to a
    // multiple of four bytes and an even number of words.
    std::string buf;
    buf.reserve(data.size() + 4);
    buf.push_back('\0');
    buf += data;
    while (buf.size() % 4 != 0) buf.push_back('\0');

    std::vector<uint32_t> words;
    words.reserve(buf.size() / 4 + 1);
    for (size_t i = 0; i + 3 < buf.size(); i += 4) {
        const unsigned char* p = (const unsigned char*)buf.data() + i;
        words.push_back((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    }
    if (words.size() % 2 != 0) words.push_back(0);

    for (size_t index = 0; index < words.size(); index++) {
        uint32_t word = words[index];
        uint64_t term = (uint64_t)word * (index % 2 == 0 ? key_a : key_b);
        uint64_t offset = (uint64_t)rol16(word) *
                          (index % 2 == 0 ? 0xEF0569FBu : 0xC31713DBu);
        if (index == 0) {
            term += (uint64_t)CS64_ADJUSTMENT0 * factors[0];
            offset += CS64_ADJUSTMENT1;
        }
        terms.push_back((uint32_t)(term & U32_MASK));
        offsets.push_back((uint32_t)(offset & U32_MASK));
    }

    uint32_t swap_acc = 0, swap_last = 0, state = 0, acc = 0;

    for (size_t index = 0; index + 1 < terms.size(); index += 2) {
        uint32_t x = terms[index];
        uint32_t y = terms[index + 1];

        uint32_t t = rol16((offsets[index] + x) & U32_MASK);
        uint32_t q = (uint32_t)(((uint64_t)t * 0x76F50F34u) & U32_MASK);
        swap_acc = (swap_acc + (uint32_t)(((uint64_t)rol16(q) * 0xC3EFEA97u + q) &
                                          U32_MASK)) & U32_MASK;

        t = rol16((offsets[index + 1] + y) & U32_MASK);
        q = (uint32_t)(((uint64_t)t * 0x1881CE3Cu) & U32_MASK);
        swap_last = (uint32_t)(((uint64_t)rol16(q) * 0x35BD1EC9u + q) & U32_MASK);
        swap_acc = (swap_acc + swap_last) & U32_MASK;

        state = (uint32_t)(((uint64_t)state * key_a + x) & U32_MASK);
        state = (uint32_t)(((uint64_t)rol16(state) * 0xCF98B111u) & U32_MASK);
        state = (uint32_t)(((uint64_t)rol16(state) * 0x87085B9Fu) & U32_MASK);
        state = (uint32_t)(((uint64_t)rol16(state) * 0x12CEB96Du) & U32_MASK);
        state = (uint32_t)(((uint64_t)rol16(state) * 0x257E1D83u) & U32_MASK);
        acc = (acc + state) & U32_MASK;

        t = (uint32_t)(((uint64_t)state * key_b + y) & U32_MASK);
        t = (uint32_t)(((uint64_t)rol16(t) * 0xA27416F5u) & U32_MASK);
        t = (uint32_t)(((uint64_t)rol16(t) * 0xD38396FFu) & U32_MASK);
        state = (uint32_t)(((uint64_t)rol16((uint32_t)(((uint64_t)rol16(t) *
                    0x7C932B89u) & U32_MASK)) * 0xBFA49F69u) & U32_MASK);
        acc = (acc + state) & U32_MASK;
    }

    out[0] = swap_last ^ state;
    out[1] = swap_acc ^ acc;
}

static std::string to_hex_upper(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", data[i]);
        out += hex;
    }
    return out;
}

// Build the v2 Sec-MS-GEC token for the given unix timestamp.
static std::string generate_sec_ms_gec_v2(double unix_timestamp,
                                         const std::string& trusted_client_token) {
    const double WIN_EPOCH = 11644473600.0;
    const double S_TO_NS = 1e9;

    double ticks = unix_timestamp;
    ticks += WIN_EPOCH;
    ticks -= fmod(ticks, 300.0);
    ticks *= S_TO_NS / 100.0;

    char numbuf[64];
    snprintf(numbuf, sizeof(numbuf), "%.0f", ticks);
    std::string str_to_hash = std::string(numbuf) + trusted_client_token;

    unsigned char digest[32];
    sha256_digest(str_to_hash, digest);

    uint32_t cs[2];
    cs64_digest(str_to_hash, digest, cs);

    unsigned char cs_bytes[8];
    for (int i = 0; i < 2; i++)
        for (int b = 0; b < 4; b++)
            cs_bytes[i * 4 + b] = (unsigned char)((cs[i] >> (b * 8)) & 0xFF);

    return to_hex_upper(cs_bytes, 8) + "-" + to_hex_upper(digest, 32);
}

} // namespace edge_drm
