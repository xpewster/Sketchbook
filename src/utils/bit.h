#include <vector>
#include <cstdint>

class BitWriter {
    std::vector<uint8_t>& out;
    uint32_t buf = 0;
    int bits = 0;
public:
    BitWriter(std::vector<uint8_t>& out) : out(out) {}

    void write(uint16_t value, int nbits) {
        buf |= (uint32_t)(value & ((1u << nbits) - 1)) << (32 - bits - nbits);
        bits += nbits;
        while (bits >= 8) {
            out.push_back((buf >> 24) & 0xFF);
            buf <<= 8;
            bits -= 8;
        }
    }

    void flush() {
        if (bits > 0) {
            out.push_back((buf >> 24) & 0xFF);
            buf = 0;
            bits = 0;
        }
    }

    size_t bytesWritten() const { return out.size(); }
};