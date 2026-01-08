#pragma once

#include "image.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdint>

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
constexpr int MAX_DIRTY_RECTS = 32;

// Threshold: if more than this fraction is dirty, send full frame instead
constexpr float FULL_FRAME_THRESHOLD = 0.6f;

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
        , dirtyTiles_(TILES_X * TILES_Y, false)
        , hasReference_(false) 
    {}

    // Debug: print rectangles
    void debugPrintRects(const std::vector<DirtyRect>& rects) const {
        std::cout << "Dirty Rectangles (" << rects.size() << "):\n";
        for (const auto& r : rects) {
            std::cout << "  x=" << r.x << " y=" << r.y << " w=" << r.w << " h=" << r.h 
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
        for (int ty = 0; ty < TILES_Y; ++ty) {
            for (int tx = 0; tx < TILES_X; ++tx) {
                if (isTileDirty(currentFrame, tx, ty)) {
                    dirtyTiles_[ty * TILES_X + tx] = true;
                    dirtyTileCount++;
                }
            }
        }
        
        // If too many tiles dirty, return single full-frame rect
        float dirtyRatio = (float)dirtyTileCount / (TILES_X * TILES_Y);
        if (dirtyRatio > FULL_FRAME_THRESHOLD) {
            rects.push_back({0, 0, (uint16_t)DISPLAY_WIDTH, (uint16_t)DISPLAY_HEIGHT});
            prevFrame_ = currentFrame;
            // debugPrintRects(rects);
            return rects;
        }
        
        // Merge adjacent dirty tiles into rectangles
        rects = mergeDirtyTiles();
        
        // Limit rectangle count
        if (rects.size() > MAX_DIRTY_RECTS) {
            rects = consolidateRects(rects, MAX_DIRTY_RECTS);
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
    
    bool isTileDirty(const Image& current, int tx, int ty) const {
        int startX = tx * TILE_WIDTH;
        int startY = ty * TILE_HEIGHT;
        int endX = min(startX + TILE_WIDTH, DISPLAY_WIDTH);
        int endY = min(startY + TILE_HEIGHT, DISPLAY_HEIGHT);
        
        for (int y = startY; y < endY; ++y) {
            for (int x = startX; x < endX; ++x) {
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
        
        for (int ty = 0; ty < TILES_Y; ++ty) {
            for (int tx = 0; tx < TILES_X; ++tx) {
                int idx = ty * TILES_X + tx;
                if (!dirtyTiles_[idx] || processed[idx]) continue;
                
                // Find maximum width of consecutive dirty tiles in this row
                int width = 1;
                while (tx + width < TILES_X && 
                       dirtyTiles_[ty * TILES_X + tx + width] &&
                       !processed[ty * TILES_X + tx + width]) {
                    width++;
                }
                
                // Extend height while all tiles in the strip are dirty
                int height = 1;
                while (ty + height < TILES_Y) {
                    bool rowOk = true;
                    for (int i = 0; i < width; ++i) {
                        int checkIdx = (ty + height) * TILES_X + tx + i;
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
                        processed[(ty + dy) * TILES_X + tx + dx] = true;
                    }
                }
                
                // Create rect in pixel coordinates
                DirtyRect rect;
                rect.x = tx * TILE_WIDTH;
                rect.y = ty * TILE_HEIGHT;
                rect.w = min(width * TILE_WIDTH, DISPLAY_WIDTH - rect.x);
                rect.h = min(height * TILE_HEIGHT, DISPLAY_HEIGHT - rect.y);
                rects.push_back(rect);
            }
        }
        
        return rects;
    }
    
    // Consolidate too many rects by merging small ones
    std::vector<DirtyRect> consolidateRects(std::vector<DirtyRect>& rects, int maxCount) {
        // Sort by area (smallest first)
        std::sort(rects.begin(), rects.end(), [](const DirtyRect& a, const DirtyRect& b) {
            return a.pixelCount() < b.pixelCount();
        });
        
        while (rects.size() > maxCount && rects.size() > 1) {
            // Merge two smallest rects into bounding box
            DirtyRect& a = rects[0];
            DirtyRect& b = rects[1];
            
            uint16_t minX = min(a.x, b.x);
            uint16_t minY = min(a.y, b.y);
            uint16_t maxX = max(a.x + a.w, b.x + b.w);
            uint16_t maxY = max(a.y + a.h, b.y + b.h);
            
            DirtyRect merged = {minX, minY, (uint16_t)(maxX - minX), (uint16_t)(maxY - minY)};
            
            rects.erase(rects.begin(), rects.begin() + 2);
            rects.push_back(merged);
            
            // Re-sort
            std::sort(rects.begin(), rects.end(), [](const DirtyRect& a, const DirtyRect& b) {
                return a.pixelCount() < b.pixelCount();
            });
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
