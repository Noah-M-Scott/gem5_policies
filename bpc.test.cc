#include "gtest/gtest.h"
#include <cstring>
#include <cstdint>
#include <vector>

// ── Inline the core BPC logic as free functions ──────────────────────────────
// This avoids needing the SimObject/Params machinery entirely.

static constexpr int WORDS = 16;
static constexpr int BITS  = 32;

static uint32_t extractPlane(const uint32_t* words, int bit) {
    uint32_t plane = 0;
    for (int w = 0; w < WORDS; w++)
        if ((words[w] >> bit) & 1u)
            plane |= (1u << w);
    return plane;
}

struct BPCResult {
    uint8_t  tags[BITS];
    uint32_t raw[BITS];
    int      size_bits;
};

static BPCResult compress(const uint8_t* line) {
    const uint32_t* words = reinterpret_cast<const uint32_t*>(line);
    BPCResult r{};
    uint32_t prev = 0;
    int raw_idx = 0;

    for (int i = 0; i < BITS; i++) {
        uint32_t plane = extractPlane(words, i);
        if      (plane == 0u)    { r.tags[i] = 0; r.size_bits += 3; }
        else if (plane == ~0u)   { r.tags[i] = 1; r.size_bits += 3; }
        else if (plane == prev)  { r.tags[i] = 2; r.size_bits += 3; }
        else if (plane == ~prev) { r.tags[i] = 3; r.size_bits += 3; }
        else { r.tags[i] = 4; r.raw[raw_idx++] = plane; r.size_bits += 3 + 16; }
        prev = plane;
    }
    return r;
}

static void decompress(const BPCResult& r, uint8_t* out) {
    uint32_t planes[BITS];
    uint32_t prev = 0;
    int raw_idx = 0;

    for (int i = 0; i < BITS; i++) {
        switch (r.tags[i]) {
            case 0: planes[i] = 0u;           break;
            case 1: planes[i] = ~0u;          break;
            case 2: planes[i] = prev;         break;
            case 3: planes[i] = ~prev;        break;
            case 4: planes[i] = r.raw[raw_idx++]; break;
        }
        prev = planes[i];
    }

    uint32_t words[WORDS] = {};
    for (int bit = 0; bit < BITS; bit++)
        for (int w = 0; w < WORDS; w++)
            if ((planes[bit] >> w) & 1u)
                words[w] |= (1u << bit);

    memcpy(out, words, 64);
}

// ── Tests ────────────────────────────────────────────────────────────────────

// All-zero line: every plane is ALL_ZEROS → 32 × 3 = 96 bits
TEST(BPCTest, AllZerosSize) {
    uint8_t line[64] = {};
    auto r = compress(line);
    EXPECT_EQ(r.size_bits, 96);
}

// All-zero line: max compression, well below uncompressed 512 bits
TEST(BPCTest, AllZerosCompresses) {
    uint8_t line[64] = {};
    auto r = compress(line);
    EXPECT_LT(r.size_bits, 512);
}

// All-ones line: every plane is ALL_ONES → 32 × 3 = 96 bits
TEST(BPCTest, AllOnesSize) {
    uint8_t line[64];
    memset(line, 0xFF, 64);
    auto r = compress(line);
    EXPECT_EQ(r.size_bits, 96);
}

// Alternating 0x00000000 / 0xFFFFFFFF words:
// plane 0: alternating bits → UNCOMPRESSED
// plane 1: same as plane 0 → SAME_AS_PREV ... etc.
// Just verify it round-trips correctly
TEST(BPCTest, AlternatingWordsRoundTrip) {
    uint32_t words[16];
    for (int i = 0; i < 16; i++)
        words[i] = (i % 2 == 0) ? 0x00000000u : 0xFFFFFFFFu;

    uint8_t line[64], out[64] = {};
    memcpy(line, words, 64);

    auto r = compress(line);
    decompress(r, out);
    EXPECT_EQ(memcmp(line, out, 64), 0);
}

// Round-trip: sequential bytes 0..63
TEST(BPCTest, SequentialRoundTrip) {
    uint8_t line[64], out[64] = {};
    for (int i = 0; i < 64; i++) line[i] = (uint8_t)i;

    auto r = compress(line);
    decompress(r, out);
    EXPECT_EQ(memcmp(line, out, 64), 0);
}

// Round-trip: all same word repeated (pointer-heavy workload)
TEST(BPCTest, RepeatedWordRoundTrip) {
    uint32_t words[16];
    for (int i = 0; i < 16; i++) words[i] = 0xDEADBEEFu;
    uint8_t line[64], out[64] = {};
    memcpy(line, words, 64);

    auto r = compress(line);
    decompress(r, out);
    EXPECT_EQ(memcmp(line, out, 64), 0);
}

// Round-trip: random-ish data (worst case for compression)
TEST(BPCTest, RandomDataRoundTrip) {
    uint8_t line[64], out[64] = {};
    // deterministic "random" pattern
    for (int i = 0; i < 64; i++) line[i] = (uint8_t)(i * 37 + 13);

    auto r = compress(line);
    decompress(r, out);
    EXPECT_EQ(memcmp(line, out, 64), 0);
}

// Uncompressed size is 512 bits (16 words × 32 bits)
TEST(BPCTest, UncompressedSizeUpperBound) {
    uint8_t line[64], out[64] = {};
    for (int i = 0; i < 64; i++) line[i] = (uint8_t)(i * 37 + 13);

    auto r = compress(line);
    // worst case: all 32 planes uncompressed = 32 × (3+16) = 608 bits
    // but never more than that
    EXPECT_LE(r.size_bits, 32 * (3 + 16));
}

// Single non-zero word: only plane 0 differs, rest are zeros
TEST(BPCTest, SingleNonZeroWordCompresses) {
    uint8_t line[64] = {};
    // set words[0] = 1
    line[0] = 1;

    auto r = compress(line);
    // should compress significantly vs 512 bits
    EXPECT_LT(r.size_bits, 200);
}
