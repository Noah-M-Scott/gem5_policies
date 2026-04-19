#include "mem/cache/compressors/bpc.hh"

#include <cassert>
#include <cstring>

#include "debug/CacheComp.hh"
#include "params/BPC.hh"

namespace gem5::compression {

BPC::BPC(const BaseCacheCompressorParams& p)
    : Base(p)
{
}

uint32_t
BPC::extractPlane(const uint32_t* words, int bit) const
{
    uint32_t plane = 0;
    for (int w = 0; w < WORDS_PER_LINE; w++) {
        if ((words[w] >> bit) & 1u)
            plane |= (1u << w);
    }
    return plane;
}

void
BPC::reconstructWords(const std::vector<uint32_t>& planes,
                      uint32_t* words) const
{
    assert((int)planes.size() == BITS_PER_WORD);
    memset(words, 0, WORDS_PER_LINE * sizeof(uint32_t));
    for (int bit = 0; bit < BITS_PER_WORD; bit++) {
        for (int w = 0; w < WORDS_PER_LINE; w++) {
            if ((planes[bit] >> w) & 1u)
                words[w] |= (1u << bit);
        }
    }
}

std::unique_ptr<Base::CompressionData>
BPC::compress(const std::vector<Base::Chunk>& chunks,
              Cycles& comp_lat,
              Cycles& decomp_lat)
{
    // Flatten chunks into 32-bit words
    // Base::Chunk is uint64_t, so we reinterpret as pairs of uint32_t
    assert(chunks.size() * sizeof(Base::Chunk) ==
           WORDS_PER_LINE * sizeof(uint32_t));

    uint32_t words[WORDS_PER_LINE];
    memcpy(words, chunks.data(), WORDS_PER_LINE * sizeof(uint32_t));

    auto comp_data = std::make_unique<BPCData>();

    uint32_t prev_plane = 0;
    int compressed_bits = 0;
    // 3-bit tag per plane
    constexpr int TAG_BITS = 3;

    for (int bit = 0; bit < BITS_PER_WORD; bit++) {
        uint32_t plane = extractPlane(words, bit);
        PlaneEncoding tag;

        if (plane == 0x00000000u) {
            tag = ALL_ZEROS;
            compressed_bits += TAG_BITS;
        } else if (plane == 0xFFFFFFFFu) {
            tag = ALL_ONES;
            compressed_bits += TAG_BITS;
        } else if (plane == prev_plane) {
            tag = SAME_AS_PREV;
            compressed_bits += TAG_BITS;
        } else if (plane == ~prev_plane) {
            tag = COMP_OF_PREV;
            compressed_bits += TAG_BITS;
        } else {
            tag = UNCOMPRESSED;
            comp_data->rawPlanes.push_back(plane);
            compressed_bits += TAG_BITS + WORDS_PER_LINE;
        }

        comp_data->tags.push_back(tag);
        prev_plane = plane;
    }

    comp_data->setSizeBits(compressed_bits);

    // Simple latency model — 1 cycle each
    comp_lat   = Cycles(1);
    decomp_lat = Cycles(1);

    DPRINTF(CacheComp, "BPC: compressed %d bits (uncompressed %d bits)\n",
            compressed_bits, WORDS_PER_LINE * BITS_PER_WORD);

    return comp_data;
}

void
BPC::decompress(const CompressionData* comp_data, uint64_t* cache_line)
{
    const BPCData* bpc_data = static_cast<const BPCData*>(comp_data);
    assert((int)bpc_data->tags.size() == BITS_PER_WORD);

    std::vector<uint32_t> planes(BITS_PER_WORD);
    uint32_t prev_plane = 0;
    int raw_idx = 0;

    for (int bit = 0; bit < BITS_PER_WORD; bit++) {
        switch (bpc_data->tags[bit]) {
          case ALL_ZEROS:
            planes[bit] = 0x00000000u;
            break;
          case ALL_ONES:
            planes[bit] = 0xFFFFFFFFu;
            break;
          case SAME_AS_PREV:
            planes[bit] = prev_plane;
            break;
          case COMP_OF_PREV:
            planes[bit] = ~prev_plane;
            break;
          case UNCOMPRESSED:
            planes[bit] = bpc_data->rawPlanes[raw_idx++];
            break;
          default:
            panic("BPC: unknown plane encoding tag %d", bpc_data->tags[bit]);
        }
        prev_plane = planes[bit];
    }

    uint32_t words[WORDS_PER_LINE];
    reconstructWords(planes, words);
    memcpy(cache_line, words, WORDS_PER_LINE * sizeof(uint32_t));
}

} // namespace gem5::compression
