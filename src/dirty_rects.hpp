#pragma once

#include "image.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include "log.hpp"

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
constexpr float FULL_FRAME_THRESHOLD = 0.6f;

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
    
    int pixelCount() const { return w * h; }
    int byteSize() const { return pixelCount() * sizeof(Pixel); }
};

// Packet header for dirty rect protocol
// Format:
//   [1 byte]  message type
//   If MSG_DIRTY_RECTS:
//     [1 byte]  rect count
//     [8 bytes per rect] x, y, w, h as uint16_t little-endian
//     [pixel data for each rect in sequence]
//   If MSG_FULL_FRAME:
//     [raw pixel data]
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
    
    // Compare current frame against previous, returns dirty rectangles
    // Also updates internal reference frame
    std::vector<DirtyRect> findDirtyRects(const Image& currentFrame) {
        std::vector<DirtyRect> rects;
        
        if (!hasReference_) {
            // First frame - mark everything dirty
            rects.push_back({0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT});
            prevFrame_ = currentFrame;
            hasReference_ = true;
            return rects;
        }
        
        // Reset dirty tiles
        std::fill(dirtyTiles_.begin(), dirtyTiles_.end(), false);
        int dirtyTileCount = 0;
        
        // Check each tile for changes
        for (int ty = 0; ty < tilesY_; ++ty) {
            for (int tx = 0; tx < tilesX_; ++tx) {
                if (isTileDirty(currentFrame, tx, ty)) {
                    dirtyTiles_[ty * tilesX_ + tx] = true;
                    dirtyTileCount++;
                }
            }
        }
        
        // If too many tiles dirty, send full frame and limit rectangle count
        float dirtyRatio = (float)dirtyTileCount / (tilesX_ * tilesY_);
        if (dirtyRatio > config_.fullFrameThreshold) {
            rects.push_back({0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT});
            prevFrame_ = currentFrame;
            // debugPrintRects(rects);
            return rects;
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
        
        return rects;
    }
    
    // Build packet with dirty rect header and pixel data
    std::vector<uint8_t> buildPacket(const Image& frame, const std::vector<DirtyRect>& rects) {
        std::vector<uint8_t> packet;
        
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
            // Append raw pixel data
            const uint8_t* data = frame.data();
            packet.insert(packet.end(), data, data + frame.dataSize());
        } else {
            packet.push_back(MSG_DIRTY_RECTS);
            packet.push_back(static_cast<uint8_t>(rects.size()));
            
            // Write rect headers
            for (const auto& rect : rects) {
                appendU16(packet, rect.x);
                appendU16(packet, rect.y);
                appendU16(packet, rect.w);
                appendU16(packet, rect.h);
            }
            
            // Write pixel data for each rect
            for (const auto& rect : rects) {
                appendRectPixels(packet, frame, rect);
            }
        }
        
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
    
    bool isTileDirty(const Image& current, int tx, int ty) const {
        int sx = tileStartX(tx), ex = tileEndX(tx);
        int sy = tileStartY(ty), ey = tileEndY(ty);
        
        for (int y = sy; y < ey; ++y) {
            for (int x = sx; x < ex; ++x) {
                if (current.at(x, y) != prevFrame_.at(x, y)) {
                    return true;
                }
            }
        }
        return false;
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

    void appendU16(std::vector<uint8_t>& packet, uint16_t val) {
        packet.push_back(val & 0xFF);
        packet.push_back((val >> 8) & 0xFF);
    }
    
    void appendRectPixels(std::vector<uint8_t>& packet, const Image& frame, const DirtyRect& rect) {
        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                Pixel p = frame.at(x, y);
                packet.push_back(p & 0xFF);
                packet.push_back((p >> 8) & 0xFF);
            }
        }
    }
};

} // namespace qualia
