#ifndef __MEM_CACHE_COMPRESSORS_BPC_HH__
#define __MEM_CACHE_COMPRESSORS_BPC_HH__

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"

namespace gem5::compression {

class BPC : public Base {
  public:
    // Encoding tags for each bit plane
    enum PlaneEncoding : uint8_t {
        ALL_ZEROS    = 0,
        ALL_ONES     = 1,
        SAME_AS_PREV = 2,
        COMP_OF_PREV = 3,
        UNCOMPRESSED = 4,
    };

    struct BPCData : public CompressionData {
        // One encoding tag per bit position (32 planes for 32-bit words)
        std::vector<PlaneEncoding> tags;
        // Raw plane data only for UNCOMPRESSED planes
        std::vector<uint32_t> rawPlanes;

        BPCData() : CompressionData() {}
    };

    BPC(const Params& p);
    ~BPC() = default;

    std::unique_ptr<CompressionData> compress(
        const std::vector<Base::Chunk>& chunks,
        Cycles& comp_lat,
        Cycles& decomp_lat) override;

    void decompress(const CompressionData* comp_data,
                    uint64_t* cache_line) override;

  private:
    // Number of 32-bit words per cache line
    static constexpr int WORDS_PER_LINE = 16; // 64B / 4B
    static constexpr int BITS_PER_WORD  = 32;

    // Extract bit plane `bit` from array of 32-bit words
    // Returns a bitmask of WORDS_PER_LINE bits
    uint32_t extractPlane(const uint32_t* words, int bit) const;

    // Reconstruct words from bit planes
    void reconstructWords(const std::vector<uint32_t>& planes,
                          uint32_t* words) const;
};

} // namespace gem5::compression

#endif // __MEM_CACHE_COMPRESSORS_BPC_HH__
