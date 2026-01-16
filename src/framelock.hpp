#include <chrono>

// Frame lock controller - manages animation timing when frame lock is enabled
class FrameLockController {
public:
    FrameLockController(double targetFPS = 20.0) 
        : targetFPS_(targetFPS), frameBudget_(1.0 / targetFPS),
          lockedTime_(0.0), budgetRemaining_(0.0), wallTime_(0.0),
          lastUpdateTime_(std::chrono::steady_clock::now()) {}
    
    void update() {
        auto now = std::chrono::steady_clock::now();
        double deltaWall = std::chrono::duration<double>(now - lastUpdateTime_).count();
        lastUpdateTime_ = now;
        
        // Always track wall time for real-time preview
        wallTime_ += deltaWall;
        
        // Advance locked time within budget
        double advance = min(deltaWall, budgetRemaining_);
        lockedTime_ += advance;
        budgetRemaining_ = max(0.0, budgetRemaining_ - deltaWall);
    }
    
    // Call when sender consumes a frame - replenishes the time budget
    void onFrameConsumed() {
        budgetRemaining_ = frameBudget_;
    }
    
    // Get animation time for locked (sent) frames
    double getLockedTime() const { return lockedTime_; }
    
    // Get animation time for real-time preview (wall clock)
    double getWallTime() const { return wallTime_; }
    
    // Check if animation is currently frozen (budget exhausted)
    bool isFrozen() const { return budgetRemaining_ <= 0.0; }
    
    void reset() {
        lockedTime_ = 0.0;
        budgetRemaining_ = frameBudget_;
        wallTime_ = 0.0;
        lastUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    void setTargetFPS(double fps) {
        targetFPS_ = fps;
        frameBudget_ = 1.0 / fps;
    }
    
private:
    double targetFPS_;
    double frameBudget_;      // Time budget per frame (1/targetFPS)
    double lockedTime_;       // Animation time for locked frames
    double budgetRemaining_;  // Time budget remaining before freeze
    double wallTime_;         // Real wall-clock time for preview
    std::chrono::steady_clock::time_point lastUpdateTime_;
};
