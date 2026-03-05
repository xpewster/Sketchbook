#pragma once

#include "image.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include "log.hpp"
#include "utils/bit.h"
#include "utils/rgb565.h"
#include <format>

namespace qualia {

// Protocol message types
constexpr uint8_t MSG_FULL_FRAME = 0x00;
constexpr uint8_t MSG_DIRTY_RECTS = 0x01;
constexpr uint8_t MSG_NO_CHANGE = 0x02;

// Tile size for dirty detection (larger = fewer rects but more wasted pixels)
constexpr int TILE_WIDTH = 16;
constexpr int TILE_HEIGHT = 16;

constexpr int TILES_X = (DISPLAY_WIDTH + TILE_WIDTH - 1) / TILE_WIDTH;   // 15 tiles
constexpr int TILES_Y = (DISPLAY_HEIGHT + TILE_HEIGHT - 1) / TILE_HEIGHT; // 60 tiles

// Maximum rectangles to send (keep protocol simple)
constexpr int MAX_DIRTY_RECTS = 256;

// Threshold: if more than this fraction is dirty, send full frame instead
constexpr float FULL_FRAME_THRESHOLD = 0.85f;

// Starting escape code color to use for run-length encoding (should be a rare color)
constexpr uint16_t DEFAULT_RLE_ESCAPE_COLOR = 0xe83f;

// Runtime-configurable dirty rect parameters (can be overridden per-skin)
struct DirtyRectConfig {
    int tileWidth = TILE_WIDTH;
    int tileHeight = TILE_HEIGHT;
    int offsetX = 0;                   // Tile grid origin offset X
    int offsetY = 0;                   // Tile grid origin offset Y
    int maxRects = MAX_DIRTY_RECTS;
    float fullFrameThreshold = FULL_FRAME_THRESHOLD;
};

struct DirtyRect {
    uint16_t x, y, w, h;
    uint32_t runLengthEncodingSize = 0; // Size of RLE data if run-length encoded. UNPOPULATED right now
    int pixelCount() const { return (int)w * (int)h; }
    int byteSize() const { return pixelCount() * sizeof(Pixel); }
};

// Packet header for dirty rect protocol
// Format:
//   [1 byte]  message type
//   If MSG_DIRTY_RECTS:
//     [1 byte]  rect count
//     [1 byte]  color mode (0=RGB565, 1=RGB444, 2=RGB343, 3=RGB332)
//     [2 bytes] rle escape color
//     [12 bytes per rect] x, y, w, h as uint16_t little-endian + runLengthEncodingSize as uint32_t little-endian (0 if not run-length encoded)
//     [pixel data for each rect in sequence]
//   If MSG_FULL_FRAME:
//     [1 byte]  color mode (0=RGB565, 1=RGB444, 2=RGB343, 3=RGB332)
//     [raw pixel data]
//   If MSG_FLASH_DATA:
//     [.. bytes] flash stats header (see flash_exporter.hpp)
//     Same as MSG_DIRTY_RECTS for the rest of the packet
//   If MSG_NO_CHANGE:
//     (no additional data)

class DirtyRectTracker {
public:
    DirtyRectTracker() 
        : prevFrame_(DISPLAY_WIDTH, DISPLAY_HEIGHT)
        , hasReference_(false) 
    {
        applyConfig(config_);
    }

    // Apply runtime configuration (e.g. from skin.xml)
    void configure(const DirtyRectConfig& config) {
        config_ = config;
        applyConfig(config_);
        invalidate();
        LOG_INFO << "Dirty rect config: tile=" << tw_ << "x" << th_
                 << " offset=" << effOffX_ << "," << effOffY_
                 << " tiles=" << tilesX_ << "x" << tilesY_
                 << " maxRects=" << config_.maxRects 
                 << " threshold=" << config_.fullFrameThreshold << "\n";
    }
    
    const DirtyRectConfig& getConfig() const { return config_; }

    // Debug: print rectangles
    void debugPrintRects(const std::vector<DirtyRect>& rects) const {
        LOG_DEBUG << "Dirty Rectangles (" << rects.size() << "):\n";
        for (const auto& r : rects) {
            LOG_DEBUG << "  x=" << r.x << " y=" << r.y << " w=" << r.w << " h=" << r.h 
                      << " pixels=" << r.pixelCount() << "\n";
        }
    }
    
    // Compare current frame against previous, returns dirty rectangles and RLE escape code color
    // Also updates internal reference frame
    std::pair<std::vector<DirtyRect>, uint16_t> findDirtyRects(const Image& currentFrame) {
        std::vector<DirtyRect> rects;
        bool shouldRecalculateRLEEscapeCode = false; // If any dirty tile uses the default RLE escape color, we need to recalculate to avoid conflicts
        
        if (!hasReference_) {
            // First frame - mark everything dirty
            rects.push_back({0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT});
            prevFrame_ = currentFrame;
            hasReference_ = true;
            return {rects, DEFAULT_RLE_ESCAPE_COLOR};
        }
        
        // Reset dirty tiles
        std::fill(dirtyTiles_.begin(), dirtyTiles_.end(), false);
        int dirtyTileCount = 0;
        
        // Check each tile for changes
        for (int ty = 0; ty < tilesY_; ++ty) {
            for (int tx = 0; tx < tilesX_; ++tx) {
                auto [tileIsDirty, tileHasDefaultRLEEscapeColor] = isTileDirty(currentFrame, tx, ty);
                if (tileIsDirty) {
                    dirtyTiles_[ty * tilesX_ + tx] = true;
                    dirtyTileCount++;
                }
                if (tileHasDefaultRLEEscapeColor) {
                    shouldRecalculateRLEEscapeCode = true;
                }
            }
        }
        
        // If too many tiles dirty, send full frame and limit rectangle count
        float dirtyRatio = (float)dirtyTileCount / (tilesX_ * tilesY_);
        if (dirtyRatio > config_.fullFrameThreshold) {
            rects.push_back({0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT});
            prevFrame_ = currentFrame;
            // debugPrintRects(rects);
            return {rects, DEFAULT_RLE_ESCAPE_COLOR};
        }
        
        // Merge adjacent dirty tiles into rectangles
        rects = mergeDirtyTiles();
        
        //  If too many rects, send full frame
        if (rects.size() > (size_t)config_.maxRects) {
            rects.clear();
            rects.push_back({0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT});
        }
        
        // Update reference frame
        prevFrame_ = currentFrame;

        // debugPrintRects(rects);

        uint16_t escapeColor = DEFAULT_RLE_ESCAPE_COLOR;
        if (shouldRecalculateRLEEscapeCode) {
            escapeColor = getRLEEscapeColor(currentFrame); // TODO: account for color mode when calculating escape color (e.g. if using RGB332, we need to find an escape color that is rare within the reduced 3-3-2 bit palette)
        }
        
        return {rects, escapeColor};
    }

    void appendDirtyRectData(std::vector<uint8_t>& packet, const Image& frame, const std::vector<DirtyRect>& rects, bool rleEnabled, uint16_t rleEscapeColor = DEFAULT_RLE_ESCAPE_COLOR, ColorMode colorMode = ColorMode::RGB565) {
        // Write rect headers
        packet.push_back(static_cast<uint8_t>(rects.size()));
        packet.push_back(static_cast<uint8_t>(colorMode));
        uint16_t adjustedRleEscapeColor = rleEscapeColor;
        // If using a more compact color mode, we need to adjust the escape color to fit within the reduced bit depth
        if (rleEnabled && colorMode != ColorMode::RGB565) {
            switch (colorMode) {
                case ColorMode::RGB444:
                    adjustedRleEscapeColor = rgb444(((rleEscapeColor >> 11) & 0x1F) << 3, ((rleEscapeColor >> 5) & 0x3F) << 2, (rleEscapeColor & 0x1F) << 3);
                    break;
                case ColorMode::RGB343:
                    adjustedRleEscapeColor = rgb343(((rleEscapeColor >> 11) & 0x1F) << 3, ((rleEscapeColor >> 5) & 0x3F) << 2, (rleEscapeColor & 0x1F) << 3);
                    break;
                case ColorMode::RGB332:
                    adjustedRleEscapeColor = rgb332(((rleEscapeColor >> 11) & 0x1F) << 3, ((rleEscapeColor >> 5) & 0x3F) << 2, (rleEscapeColor & 0x1F) << 3);
                    break;
                default:
                    break;
            }
        }
        appendU16(packet, adjustedRleEscapeColor);
        
        // Write rect dimensions
        for (const auto& rect : rects) {
            appendU16(packet, rect.x);
            appendU16(packet, rect.y);
            appendU16(packet, rect.w);
            appendU16(packet, rect.h);
        }
        
        // Write pixel data for each rect
        for (const auto& rect : rects) {
            appendRectPixels(packet, frame, rect, rleEnabled, adjustedRleEscapeColor, colorMode);
        }
    }
    
    // Build packet with dirty rect header and pixel data
    std::vector<uint8_t> buildPacket(const Image& frame, const std::vector<DirtyRect>& rects, bool rleEnabled, uint16_t rleEscapeColor = DEFAULT_RLE_ESCAPE_COLOR, ColorMode colorMode = ColorMode::RGB565) {
        std::vector<uint8_t> packet;
        packet.reserve(4 + rects.size() * 12); // Reserve base amount of space for header and rect headers
        
        if (rects.empty()) {
            // No changes
            packet.push_back(MSG_NO_CHANGE);
            return packet;
        }
        
        // Check if it's a full frame
        bool isFullFrame = (rects.size() == 1 && 
                           rects[0].x == 0 && rects[0].y == 0 &&
                           rects[0].w == DISPLAY_WIDTH && rects[0].h == DISPLAY_HEIGHT);
        
        if (isFullFrame) {
            packet.push_back(MSG_FULL_FRAME);
            packet.push_back(static_cast<uint8_t>(colorMode));
            // Append raw pixel data
            const uint8_t* data = frame.data();
            if (colorMode == ColorMode::RGB565) {
                packet.insert(packet.end(), data, data + frame.dataSize());
            } else {
                appendRectPixels(packet, frame, {0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT}, false, rleEscapeColor, colorMode);
            }
        } else {
            packet.push_back(MSG_DIRTY_RECTS);
            appendDirtyRectData(packet, frame, rects, rleEnabled, rleEscapeColor, colorMode);
        }

        // size_t expectedSize = 2; // msg type + rect count
        // for (const auto& rect : rects) {
        //     expectedSize += 8; // x, y, w, h headers
        //     expectedSize += 4; // runLengthEncodingSize field
        //     expectedSize += rect.byteSize(); // worst case (uncompressed)
        // }
        // LOG_DEBUG << "Built packet: " << packet.size() << " bytes (raw would be " << expectedSize << ")\n";
        
        return packet;
    }
    
    // Force next frame to be full (e.g., after reconnect)
    void invalidate() {
        hasReference_ = false;
    }
    
    // Statistics
    struct Stats {
        int totalPixels;
        int dirtyPixels;
        int rectCount;
        float compressionRatio; // dirty/total
    };
    
    Stats getLastStats(const std::vector<DirtyRect>& rects) const {
        Stats s;
        s.totalPixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
        s.dirtyPixels = 0;
        for (const auto& r : rects) {
            s.dirtyPixels += r.pixelCount();
        }
        s.rectCount = (int)rects.size();
        s.compressionRatio = (float)s.dirtyPixels / s.totalPixels;
        return s;
    }

private:
    Image prevFrame_;
    std::vector<bool> dirtyTiles_;
    bool hasReference_;
    bool debugHasPrintedFirst_ = false;
    
    // Runtime config
    DirtyRectConfig config_;
    int tw_ = TILE_WIDTH, th_ = TILE_HEIGHT;   // Clamped tile dimensions
    int effOffX_ = 0, effOffY_ = 0;            // Effective offset (offset % tile)
    int tilesX_ = TILES_X, tilesY_ = TILES_Y;
    
    void applyConfig(const DirtyRectConfig& cfg) {
        tw_ = max(1, cfg.tileWidth);
        th_ = max(1, cfg.tileHeight);
        effOffX_ = max(0, cfg.offsetX) % tw_;
        effOffY_ = max(0, cfg.offsetY) % th_;
        
        // Tile count: 1 partial (if offset) + full tiles to cover the rest
        if (effOffX_ > 0)
            tilesX_ = 1 + (DISPLAY_WIDTH - effOffX_ + tw_ - 1) / tw_;
        else
            tilesX_ = (DISPLAY_WIDTH + tw_ - 1) / tw_;
        
        if (effOffY_ > 0)
            tilesY_ = 1 + (DISPLAY_HEIGHT - effOffY_ + th_ - 1) / th_;
        else
            tilesY_ = (DISPLAY_HEIGHT + th_ - 1) / th_;
        
        dirtyTiles_.assign(tilesX_ * tilesY_, false);
    }
    
    // Pixel range for tile tx: [tileStartX, tileEndX)
    int tileStartX(int tx) const {
        if (effOffX_ == 0) return tx * tw_;
        return (tx == 0) ? 0 : effOffX_ + (tx - 1) * tw_;
    }
    int tileEndX(int tx) const {
        if (effOffX_ == 0) return min((tx + 1) * tw_, DISPLAY_WIDTH);
        return min(effOffX_ + tx * tw_, DISPLAY_WIDTH);
    }
    int tileStartY(int ty) const {
        if (effOffY_ == 0) return ty * th_;
        return (ty == 0) ? 0 : effOffY_ + (ty - 1) * th_;
    }
    int tileEndY(int ty) const {
        if (effOffY_ == 0) return min((ty + 1) * th_, DISPLAY_HEIGHT);
        return min(effOffY_ + ty * th_, DISPLAY_HEIGHT);
    }
    
    std::pair<bool, bool> isTileDirty(const Image& current, int tx, int ty) const {
        int sx = tileStartX(tx), ex = tileEndX(tx);
        int sy = tileStartY(ty), ey = tileEndY(ty);
        bool tileIsDirty = false;
        bool hasDefaultRLEEscapeColor = false;
        
        for (int y = sy; y < ey; ++y) {
            for (int x = sx; x < ex; ++x) {
                if (current.at(x, y) != prevFrame_.at(x, y)) {
                    tileIsDirty = true;
                }
                if (current.at(x, y) == DEFAULT_RLE_ESCAPE_COLOR) {
                    hasDefaultRLEEscapeColor = true;
                }
            }
        }
        return {tileIsDirty, hasDefaultRLEEscapeColor};
    }
    
    // Simple greedy algorithm to merge adjacent dirty tiles into rectangles
    std::vector<DirtyRect> mergeDirtyTiles() {
        std::vector<DirtyRect> rects;
        std::vector<bool> processed(dirtyTiles_.size(), false);
        
        for (int ty = 0; ty < tilesY_; ++ty) {
            for (int tx = 0; tx < tilesX_; ++tx) {
                int idx = ty * tilesX_ + tx;
                if (!dirtyTiles_[idx] || processed[idx]) continue;
                
                // Find maximum width of consecutive dirty tiles in this row
                int width = 1;
                while (tx + width < tilesX_ && 
                       dirtyTiles_[ty * tilesX_ + tx + width] &&
                       !processed[ty * tilesX_ + tx + width]) {
                    width++;
                }
                
                // Extend height while all tiles in the strip are dirty
                int height = 1;
                while (ty + height < tilesY_) {
                    bool rowOk = true;
                    for (int i = 0; i < width; ++i) {
                        int checkIdx = (ty + height) * tilesX_ + tx + i;
                        if (!dirtyTiles_[checkIdx] || processed[checkIdx]) {
                            rowOk = false;
                            break;
                        }
                    }
                    if (!rowOk) break;
                    height++;
                }
                
                // Mark tiles as processed
                for (int dy = 0; dy < height; ++dy) {
                    for (int dx = 0; dx < width; ++dx) {
                        processed[(ty + dy) * tilesX_ + tx + dx] = true;
                    }
                }
                
                // Create rect from tile pixel ranges
                int px0 = tileStartX(tx);
                int py0 = tileStartY(ty);
                int px1 = tileEndX(tx + width - 1);
                int py1 = tileEndY(ty + height - 1);
                
                DirtyRect rect;
                rect.x = (uint16_t)px0;
                rect.y = (uint16_t)py0;
                rect.w = (uint16_t)(px1 - px0);
                rect.h = (uint16_t)(py1 - py0);
                rects.push_back(rect);
            }
        }
        
        return rects;
    }

    uint16_t getRLEEscapeColor(const Image& frame) const {
        // If the default escape color appears in the image, we need to find a different one
        // that doesn't appear. In practice, this is very unlikely to be needed since the default is bright magenta.
        // But we want to be robust just in case.
        uint16_t escapeColor = DEFAULT_RLE_ESCAPE_COLOR;
        
        // Find an alternative color (simple approach: just try sequential values until we find one that doesn't appear)
        for (uint16_t c = 0; c <= 0xFFFF; c++) {
            bool found = false;
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    Pixel p = frame.at(x, y);
                    // Compare in RGB332 space since that's the most compact mode we support, so it has the highest chance of conflicts. P should already be in the correct format
                    if (p == rgb332((c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F)) {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) {
                return c;
            }
        }
        
        // Extremely unlikely case: all colors are used in the image. In this case, we won't be able to use RLE encoding.
        LOG_WARN << "RLE escape color conflict detected and no alternative color available. This image somehow uses every single color.\n";
        return DEFAULT_RLE_ESCAPE_COLOR; // Will trigger fallback to raw encoding
    }

    void appendU16(std::vector<uint8_t>& packet, uint16_t val) {
        packet.push_back(val & 0xFF);
        packet.push_back((val >> 8) & 0xFF);
    }

    void appendU32(std::vector<uint8_t>& packet, uint32_t val) {
        packet.push_back(val & 0xFF);
        packet.push_back((val >> 8) & 0xFF);
        packet.push_back((val >> 16) & 0xFF);
        packet.push_back((val >> 24) & 0xFF);
    }
    
    void appendRectPixels(std::vector<uint8_t>& packet, const Image& frame,
                      const DirtyRect& rect, bool rleEnabled,
                      uint16_t rleEscapeColor, ColorMode colorMode) {
        if (rleEnabled && tryAppendRunLengthEncodedRect(packet, frame, rect, rleEscapeColor, colorMode)) {
            return;
        }
        if (!rleEnabled) {
            appendU32(packet, 0); // runLengthEncodingSize = 0 indicates raw data
        }
        // Raw packed pixels
        if (colorMode == ColorMode::RGB565) {
            // Fast path: little-endian uint16, directly memcpy-able on ESP32
            for (int y = rect.y; y < rect.y + rect.h; ++y)
                for (int x = rect.x; x < rect.x + rect.w; ++x)
                    appendU16(packet, frame.at(x, y));
        } else {
            // Sub-byte modes: pack via bitstream
            const int bpp = bitsPerPixel(colorMode);
            BitWriter w(packet);
            for (int y = rect.y; y < rect.y + rect.h; ++y)
                for (int x = rect.x; x < rect.x + rect.w; ++x)
                    w.write(frame.at(x, y), bpp);
            w.flush();
        }
    }

    // Appends a escape code run-length encoded version of the rect to the packet if it would save space, returns true if appended
    bool tryAppendRunLengthEncodedRect(std::vector<uint8_t>& packet, const Image& frame, const DirtyRect& rect, uint16_t rleEscapeColor, ColorMode colorMode) {
        const int bpp = bitsPerPixel(colorMode);
        const uint16_t maxRun = maxRunLength(colorMode);

        // Build runs (cap at maxRun since count is N-bit)
        std::vector<std::pair<Pixel, uint16_t>> runs; // (color, length)
        Pixel curColor = frame.at(rect.x, rect.y);
        uint16_t curLen = 0;

        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                Pixel p = frame.at(x, y);
                if (p == curColor && curLen < maxRun) {
                    curLen++;
                } else {
                    runs.push_back({curColor, curLen});
                    curColor = p;
                    curLen = 1;
                }
            }
        }
        runs.push_back({curColor, curLen});
        
        // Calculate size of RLE data
        size_t rleBits = 0;
        for (const auto& [color, length] : runs) {
            if (length < minRleRunLength()) { // Not worth encoding as RLE, would be smaller as raw pixels
                // This will be encoded as a single raw pixel, no RLE overhead
                rleBits += bpp * length; // raw pixel
            } else {
                // This will be encoded as an RLE run
                rleBits += bpp * 3; // RLE entry: escape color + pixel color + run length
            }
        }
        size_t rleBytes = (rleBits + 7) / 8;
        size_t rawSize = packedByteSize(rect.w * rect.h, colorMode);
        size_t OVERHEAD_ESTIMATION = 100; // Since RLE has more overhead, we require a significant reduction to be worth it
        
        if (rleBytes < rawSize - OVERHEAD_ESTIMATION) {
            // Encode RLE stream
            size_t startPos = packet.size();
            appendU32(packet, 0); // placeholder for size

            if (colorMode == ColorMode::RGB565) {
                // Byte-aligned little-endian encoding (fast path on ESP32)
                for (const auto& [color, length] : runs) {
                    if (length < minRleRunLength()) {
                        for (uint16_t i = 0; i < length; ++i)
                            appendU16(packet, color);
                    } else {
                        appendU16(packet, rleEscapeColor);
                        appendU16(packet, color);
                        appendU16(packet, length);
                    }
                }
            } else {
                // Bitstream encoding (all values bpp bits wide)
                BitWriter w(packet);
                for (const auto& [color, length] : runs) {
                    if (length < minRleRunLength()) {
                        for (uint16_t i = 0; i < length; ++i)
                            w.write(color, bpp);
                    } else {
                        w.write(rleEscapeColor, bpp);
                        w.write(color, bpp);
                        w.write(length, bpp);
                    }
                }
                w.flush();
            }

            // Patch the size field
            uint32_t rleSize = (uint32_t)(packet.size() - startPos - 4); // size of RLE data excluding the size field itself
            memcpy(&packet[startPos], &rleSize, 4);
            // if (!debugHasPrintedFirst_) {
            //     LOG_DEBUG << "RLE encoded rect: " << rleSize << " bytes (raw would be " << rawSize << ")\n";
            //     LOG_DEBUG << "RLE escape color: 0x" << std::hex << rleEscapeColor << std::dec << "\n";
            //     // Print original rect pixel data for debugging
            //     std::string hexDump;
            //     for (int y = rect.y; y < rect.y + rect.h; ++y) {
            //         for (int x = rect.x; x < rect.x + rect.w; ++x) {
            //             hexDump += std::format("{:04X} ", frame.at(x, y));
            //         }
            //     }
            //     LOG_DEBUG << "Original rect pixel data:\n" << hexDump << "\n";
            //     for (const auto& [color, length] : runs) {
            //         LOG_DEBUG << "  Color: 0x" << std::hex << color << std::dec << " Length: " << length << "\n";
            //     }
            //     // Print RLE encoded data for debugging
            //     std::string rleHexDump;
            //     for (size_t i = startPos + 4; i < packet.size(); ++i) {
            //         rleHexDump += std::format("{:02X} ", packet[i]);
            //     }
            //     LOG_DEBUG << "RLE encoded data:\n" << rleHexDump << "\n";
            //     debugHasPrintedFirst_ = true;
            // }
            return true;
        } else {
            // Not worth encoding, append raw data
            appendU32(packet, 0); // runLengthEncodingSize = 0 indicates raw data
            return false;
        }
    }
};

} // namespace qualia
