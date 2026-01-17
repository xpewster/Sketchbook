#include "tcp.hpp"
#include "skins/flash_exporter.hpp"
#include "image.hpp"
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <deque>

// Protocol message types
namespace protocol {
    constexpr uint8_t MSG_FULL_FRAME = 0x00;
    constexpr uint8_t MSG_DIRTY_RECTS = 0x01;
    constexpr uint8_t MSG_NO_CHANGE = 0x02;
    constexpr uint8_t MSG_FLASH_DATA = 0x03;
    constexpr uint8_t MSG_RESET = 0x04;
    constexpr uint8_t MSG_SET_MODE = 0x05;
    
    // Mode constants
    constexpr uint8_t MODE_FULL_STREAMING = 0x00;
    constexpr uint8_t MODE_FLASH = 0x01;
}

// Threaded frame sender with frame lock support
class FrameSender {
public:
    FrameSender(int fpsWindow = 10) 
        : running_(false), frameReady_(false), sendError_(false), fpsWindow_(fpsWindow),
          frameConsumed_(false) {}
    
    ~FrameSender() {
        stop();
    }
    
    void start(TcpConnection* conn) {
        connection_ = conn;
        running_ = true;
        sendError_ = false;
        frameConsumed_ = false;
        dirtyTracker_.invalidate();  // Reset tracker on new connection
        sendThread_ = std::thread(&FrameSender::sendLoop, this);
    }
    
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cv_.notify_one();
        if (sendThread_.joinable()) {
            sendThread_.join();
        }
    }
    
    // Queue a frame for sending (called from main thread)
    void queueFrame(const qualia::Image& frame) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingFrame_ = frame;
            frameReady_ = true;
            flashMode_ = false;
        }
        cv_.notify_one();
    }
    
    // Queue a flash mode update (stats + optional dirty rects)
    void queueFlashUpdate(const flash::FlashStatsMessage& stats, 
                          const qualia::Image& frame) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingFlashStats_ = stats;
            pendingFrame_ = frame;
            frameReady_ = true;
            flashMode_ = true;
        }
        cv_.notify_one();
    }
    
    // Check if sender is ready for next frame (for frame lock mode)
    bool isReadyForFrame() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !frameReady_;
    }
    
    // Check and clear the frame consumed flag (for frame lock mode)
    bool checkAndClearFrameConsumed() {
        std::lock_guard<std::mutex> lock(consumedMutex_);
        bool consumed = frameConsumed_;
        frameConsumed_ = false;
        return consumed;
    }

    bool sendReset() {
        if (!connection_) return false;
        uint8_t cmd = protocol::MSG_RESET;
        return connection_->sendPacket(&cmd, 1);
    }
    
    // Send mode selection to remote
    // Returns true if mode was sent and ACKed successfully
    bool sendModeSelection(bool flashMode) {
        if (!connection_) return false;
        
        uint8_t packet[2] = {
            protocol::MSG_SET_MODE,
            flashMode ? protocol::MODE_FLASH : protocol::MODE_FULL_STREAMING
        };
        
        if (!connection_->sendPacket(packet, 2)) {
            return false;
        }
        
        // Wait for ACK
        return connection_->waitForAck(5000);
    }

    void invalidateDirtyTracker() {
        dirtyTracker_.invalidate();
    }
    
    // Get current FPS (thread-safe)
    double getFPS() const {
        std::lock_guard<std::mutex> lock(fpsMutex_);
        
        while (!frameTimestamps_.empty()) {
            if (frameTimestamps_.size() > fpsWindow_) {
                frameTimestamps_.pop_front();
            } else {
                break;
            }
        }

        if (frameTimestamps_.empty()) {
            return 0.0;
        }

        auto latest = frameTimestamps_.back();
        double timeSpan = std::chrono::duration<double>(latest - frameTimestamps_.front()).count();
        return timeSpan > 0.0 ? frameTimestamps_.size() / timeSpan : 0.0;
    }
    
    // Get compression stats
    float getCompressionRatio() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return lastCompressionRatio_;
    }
    
    int getLastRectCount() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return lastRectCount_;
    }
    
    size_t getLastPacketSize() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return lastPacketSize_;
    }

    // For debugging: get dirty rectangles from last frame
    std::vector<qualia::DirtyRect> getLastDirtyRects() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return lastDirtyRects_;
    }
    
    bool hadError() const { return sendError_; }
    void clearError() { sendError_ = false; }
    
private:
    void sendLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return frameReady_ || !running_; });
            
            if (!running_) break;
            
            if (frameReady_) {
                // Copy data while holding lock
                qualia::Image frameToSend = std::move(pendingFrame_);
                bool isFlashMode = flashMode_;
                flash::FlashStatsMessage flashStats = pendingFlashStats_;
                // Don't set frameReady_ = false yet - wait for ACK first
                lock.unlock();
                
                bool sendSuccess = false;
                
                if (isFlashMode) {
                    // Flash mode: send stats + dirty rects
                    sendSuccess = sendFlashUpdate(flashStats, frameToSend);
                } else {
                    // Normal mode: send dirty rects
                    sendSuccess = sendNormalFrame(frameToSend);
                }
                
                if (sendSuccess) {
                    // Wait for ACK from remote
                    if (connection_->waitForAck(5000)) {
                        recordFrameSent();
                        // Now mark frame as consumed - main thread can queue next
                        {
                            std::lock_guard<std::mutex> l(mutex_);
                            frameReady_ = false;
                        }
                        {
                            std::lock_guard<std::mutex> consumedLock(consumedMutex_);
                            frameConsumed_ = true;
                        }
                    } else {
                        // ACK timeout - connection problem
                        sendError_ = true;
                        {
                            std::lock_guard<std::mutex> l(mutex_);
                            frameReady_ = false;  // Clear so we don't retry
                        }
                    }
                } else {
                    sendError_ = true;
                    {
                        std::lock_guard<std::mutex> l(mutex_);
                        frameReady_ = false;  // Clear so we don't retry
                    }
                }
            }
        }
    }
    
    bool sendNormalFrame(const qualia::Image& frame) {
        // Find dirty rectangles
        auto rects = dirtyTracker_.findDirtyRects(frame);
        
        // Build packet with dirty rect protocol
        std::vector<uint8_t> packet = dirtyTracker_.buildPacket(frame, rects);
        
        // Update stats
        {
            std::lock_guard<std::mutex> slock(statsMutex_);
            auto stats = dirtyTracker_.getLastStats(rects);
            lastCompressionRatio_ = stats.compressionRatio;
            lastRectCount_ = stats.rectCount;
            lastPacketSize_ = packet.size();
            lastDirtyRects_ = rects;
        }
        
        return connection_->sendPacket(packet.data(), packet.size());
    }
    
    bool sendFlashUpdate(const flash::FlashStatsMessage& stats,
                         const qualia::Image& frame) {
        // Find dirty rects using normal comparison
        auto rects = dirtyTracker_.findDirtyRects(frame);
        
        // Build flash stats header
        uint8_t rectCount = min((size_t)255, rects.size());
        std::vector<uint8_t> header = stats.serialize(rectCount);
        
        // Build dirty rect data (same format as normal mode)
        std::vector<uint8_t> rectData;
        if (rectCount > 0) {
            // Rect headers
            for (size_t i = 0; i < rectCount; i++) {
                const auto& r = rects[i];
                rectData.push_back(r.x & 0xFF);
                rectData.push_back((r.x >> 8) & 0xFF);
                rectData.push_back(r.y & 0xFF);
                rectData.push_back((r.y >> 8) & 0xFF);
                rectData.push_back(r.w & 0xFF);
                rectData.push_back((r.w >> 8) & 0xFF);
                rectData.push_back(r.h & 0xFF);
                rectData.push_back((r.h >> 8) & 0xFF);
            }
            
            // Rect pixel data
            for (size_t i = 0; i < rectCount; i++) {
                const auto& r = rects[i];
                for (int y = r.y; y < r.y + r.h; y++) {
                    for (int x = r.x; x < r.x + r.w; x++) {
                        uint16_t px = frame.getPixel(x, y);
                        rectData.push_back(px & 0xFF);
                        rectData.push_back((px >> 8) & 0xFF);
                    }
                }
            }
        }
        
        // Combine and send
        std::vector<uint8_t> packet;
        packet.reserve(header.size() + rectData.size());
        packet.insert(packet.end(), header.begin(), header.end());
        packet.insert(packet.end(), rectData.begin(), rectData.end());
        
        // Update stats
        {
            std::lock_guard<std::mutex> slock(statsMutex_);
            lastCompressionRatio_ = (float)rectCount / 100.0f;  // Rough indicator
            lastRectCount_ = rectCount;
            lastPacketSize_ = packet.size();
            lastDirtyRects_ = rects;
        }
        
        return connection_->sendPacket(packet.data(), packet.size());
    }
    
    void recordFrameSent() {
        std::lock_guard<std::mutex> lock(fpsMutex_);
        frameTimestamps_.push_back(std::chrono::steady_clock::now());
    }
    
    TcpConnection* connection_ = nullptr;
    std::thread sendThread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    qualia::Image pendingFrame_;
    std::atomic<bool> running_;
    std::atomic<bool> frameReady_;
    std::atomic<bool> sendError_;
    
    // Flash mode pending data
    bool flashMode_ = false;
    flash::FlashStatsMessage pendingFlashStats_;
    
    // Frame consumed signaling (for frame lock)
    mutable std::mutex consumedMutex_;
    bool frameConsumed_;
    
    // Dirty rect tracker
    qualia::DirtyRectTracker dirtyTracker_;
    std::vector<qualia::DirtyRect> lastDirtyRects_;

    // FPS tracking
    const int fpsWindow_;
    mutable std::mutex fpsMutex_;
    mutable std::deque<std::chrono::steady_clock::time_point> frameTimestamps_;
    
    // Stats
    mutable std::mutex statsMutex_;
    float lastCompressionRatio_ = 1.0f;
    int lastRectCount_ = 0;
    size_t lastPacketSize_ = 0;
};
