#define UNICODE

#include <winsock2.h>
#include <locale.h>

#include "log.hpp"
#include "system_stats.h"
#include "image.hpp"
#include "dirty_rects.hpp"
#include "tcp.hpp"
#include "tray.hpp"

#include <SFML/Graphics.hpp>
#include <Shlobj.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>

#include "ui/text_box.cpp"
#include "ui/button.cpp"
#include "ui/dropdown.cpp"
#include "ui/checkbox.cpp"
#include "ui/info.cpp"
#include "skins/skin.h"
#include "skins/debug_skin.cpp"
#include "skins/anime_skin.cpp"
#include "skins/flash_exporter.hpp"
#include "skins/anime_flash_exporter.cpp"
#include "settings.hpp"
#include "weather.hpp"
#include "train.hpp"

// Transparent color key for flash mode (magenta = 0xF81F)
const sf::Color FLASH_TRANSPARENT_COLOR(248, 0, 248);
constexpr uint16_t FLASH_TRANSPARENT_RGB565 = 0xF81F;



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

// Convert RenderTexture to RGB565 for Qualia
void textureToRGB565(sf::RenderTexture& texture, qualia::Image& image) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(x, y));
            image.at(x, y) = qualia::rgb565(c.r, c.g, c.b);
        }
    }
}

// Rotate 90 degrees clockwise during conversion
void textureToRGB565Rot90(sf::RenderTexture& texture, qualia::Image& image) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    // texture is (height, width), output is (width, height)
    // Output pixel (x, y) comes from input pixel (y, width-1-x)
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(y, image.width - 1 - x));
            image.at(x, y) = qualia::rgb565(c.r, c.g, c.b);
        }
    }
}

// Rotate 90 degrees counter-clockwise during conversion  
void textureToRGB565RotNeg90(sf::RenderTexture& texture, qualia::Image& image) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    // Output pixel (x, y) comes from input pixel (height-1-y, x)
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(image.height - 1 - y, x));
            image.at(x, y) = qualia::rgb565(c.r, c.g, c.b);
        }
    }
}

// Get weather icon index for flash mode
int getWeatherIconIndex(const WeatherData& weather) {
    if (!weather.available) return 0xFF;
    
    const std::string& weatherType = getWeatherIconNameSimplified(weather);
    
    if (weatherType == "sunny") return flash::WEATHER_SUNNY;
    if (weatherType == "cloudy") return flash::WEATHER_CLOUDY;
    if (weatherType == "rainy") return flash::WEATHER_RAINY;
    if (weatherType == "thunderstorm") return flash::WEATHER_THUNDERSTORM;
    if (weatherType == "foggy") return flash::WEATHER_FOGGY;
    if (weatherType == "windy") return flash::WEATHER_WINDY;
    
    if (weather.isNight) return flash::WEATHER_NIGHT;
    return flash::WEATHER_SUNNY;
}

// Build flash stats message from current state
flash::FlashStatsMessage buildFlashStats(const SystemStats& stats, const WeatherData& weather,
                                          const TrainData& train, Skin* skin) {
    flash::FlashStatsMessage msg;
    
    msg.weatherIconIndex = getWeatherIconIndex(weather);
    
    msg.flags = 0;
    if (stats.cpuTempC >= skin->getWarmTempThreshold()) {
        msg.flags |= flash::FlashStatsMessage::FLAG_CPU_WARM;
    }
    if (stats.cpuTempC >= skin->getHotTempThreshold()) {
        msg.flags |= flash::FlashStatsMessage::FLAG_CPU_HOT;
    }
    if (weather.available) {
        msg.flags |= flash::FlashStatsMessage::FLAG_WEATHER_AVAIL;
    }
    if (train.available0) {
        msg.flags |= flash::FlashStatsMessage::FLAG_TRAIN0_AVAIL;
    }
    if (train.available1) {
        msg.flags |= flash::FlashStatsMessage::FLAG_TRAIN1_AVAIL;
    }
    
    msg.cpuPercent10 = static_cast<uint16_t>(stats.cpuPercent * 10);
    msg.cpuTemp10 = static_cast<uint16_t>(stats.cpuTempC * 10);
    msg.memPercent10 = static_cast<uint16_t>(stats.memPercent * 10);
    msg.weatherTemp10 = static_cast<int16_t>(weather.currentTemp * 10);
    msg.train0Mins10 = static_cast<uint16_t>(train.minsToNextTrain0 * 10);
    msg.train1Mins10 = static_cast<uint16_t>(train.minsToNextTrain1 * 10);
    
    return msg;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    PSTR lpCmdLine, int nCmdShow)
{
    if (!IsUserAnAdmin()) {
        LOG_ERROR << "This application must be run as administrator.\n";
        return 1;
    }
    setlocale(LC_ALL, "");

    LOG_INFO << "Starting Sketchbook...\n";

    Settings settings;
    if (!settings.load()) {
        LOG_ERROR << "Failed to load settings.toml\n";
        return 1;
    }

    if (settings.weather.apiKey == "YOUR_API_KEY_HERE" || settings.weather.apiKey.empty()) {
        LOG_WARN << "Please set your OpenWeatherMap API key in settings.toml\n";
        return 1;
    }

    LOG_INFO << "Successfully loaded settings.\n";

    const int menuHeight = 40;
    const int previewScale = 1;
    const int previewWidth = qualia::DISPLAY_HEIGHT / previewScale; // Use DISPLAY_HEIGHT since the preview will be rotated 90 degrees
    const int previewHeight = qualia::DISPLAY_WIDTH / previewScale; // Use DISPLAY_WIDTH since the preview will be rotated 90 degrees
    const int windowWidth = previewWidth + 40; 
    const int windowHeight = menuHeight + previewHeight + 50;
    
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(windowWidth, windowHeight)), "Sketchbook", sf::Style::Titlebar | sf::Style::Close);
    
    // Initialize tray manager (runs in its own thread)
    HWND hwnd = window.getNativeHandle();
    TrayManager trayManager(hwnd);
    window.setFramerateLimit(30);
    
    // Load font
    sf::Font font;
    if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
        LOG_ERROR << "Failed to load default font\n";
        return 1;
    }

    std::unordered_map<std::string, Skin*> skins;
    std::vector<std::unique_ptr<AnimeSkin>> animeSkins; // Storage

    std::string skinName = settings.preferences.selectedSkin;
    LOG_INFO << "Selected skin: " << skinName << "\n";

    DebugSkin debugSkin = DebugSkin(std::string("Debug"), qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH);
    debugSkin.initialize("");
    skins["Debug"] = &debugSkin;

    // Dynamically load skins from skins/ folder
    namespace fs = std::filesystem;
    std::string skinsPath = "skins/";

    if (fs::exists(skinsPath) && fs::is_directory(skinsPath)) {
        for (const auto& entry : fs::directory_iterator(skinsPath)) {
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();
                std::string skinXmlPath = entry.path().string() + "/skin.xml";
                
                if (fs::exists(skinXmlPath)) {
                    auto skin = std::make_unique<AnimeSkin>(folderName, qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH);
                    skins[folderName] = skin.get();
                    animeSkins.push_back(std::move(skin));
                    LOG_INFO << "Loaded skin: " << folderName << "\n";
                } else {
                    LOG_WARN << "Skipping folder '" << folderName << "' - no skin.xml found\n";
                }
            }
        }
    } else {
        LOG_WARN << "Skins directory not found: " << skinsPath << "\n";
    }

    // Check if drive is already flash enabled
    flash::AnimeSkinFlashExporter flashChecker(settings.network.espDrive);
    if (flashChecker.isFlashable() && flashChecker.isFlashEnabled()) {
        LOG_INFO << "Flash enabled for drive " << settings.network.espDrive << "\n";
        settings.preferences.flashMode = true;
    }
    
    // Create render texture at Qualia's native resolution
    sf::RenderTexture qualiaTexture(sf::Vector2u(qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH)); // Swapped dimensions for 90 degree rotation
    
    // Secondary texture for frame lock with real-time preview (renders the locked frame for sending)
    sf::RenderTexture lockedTexture(sf::Vector2u(qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH));
    
    // RGB565 image buffer for sending
    qualia::Image frameBuffer(qualia::DISPLAY_WIDTH, qualia::DISPLAY_HEIGHT);
    
    // TCP connection and sender thread
    TcpConnection connection;
    FrameSender sender;
    bool connected = false;
    
    // Frame lock controller
    FrameLockController frameLock(20.0);  // Target 20 FPS
    
    // System monitor
    SystemMonitor monitor;

    // Weather monitor
    WeatherMonitor weatherMonitor(settings.weather.apiKey, settings.weather.latitude, settings.weather.longitude, settings.weather.units);
    
    // Train monitor
    TrainMonitor trainMonitor(settings.train.apiBase, settings.train.apiKey, settings.train.stopId0, settings.train.stopId1);

    // Status
    std::string statusMsg = "Disconnected";
    
    // UI elements
    TextInput ipInput(10, 8, 120, 24, settings.network.espIP, font);
    Button connectBtn(140, 8, 90, 24, "Connect", font);
    connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
    std::vector<std::string> skinOptions;
    for (const auto& pair : skins) {
        skinOptions.push_back(pair.first);
    }
    int defaultSkinIndex = 0;
    for (size_t i = 0; i < skinOptions.size(); ++i) {
        if (skinOptions[i] == skinName) {
            defaultSkinIndex = static_cast<int>(i);
            skins[skinName]->initialize(skinsPath + skinName + "/skin.xml"); // Initialize the selected skin
            break;
        }
    }
    DropdownSelector skinDropdown(240, 8, 120, 24, skinOptions, font, defaultSkinIndex);
    Button refreshBtn(370, 8, 24, 24, "", font);
    sf::Texture refreshIconTexture;
    refreshBtn.setColor(sf::Color(252, 186, 3), sf::Color(252, 205, 76));
    refreshBtn.setIcon("resources/Refresh.png", 0, 0, 24, 24);
    Checkbox frameLockCB(400, 8, 12, "Frame lock", font, 4, -2, settings.preferences.frameLock);
    InfoIcon frameLockInfo(485, 8, 15, "resources/Info.png", "When enabled, the sender thread will wait for the remote device to finish processing each frame before progressing the animation. This prevents frame drops at the expense of slower animation.", font);
    Checkbox flashModeCB(400, 24, 12, "Flash mode", font, 4, -2, settings.preferences.flashMode);
    InfoIcon flashModeInfo(485, 22, 15, "resources/Info.png", "When enabled, the program will only send raw data and selected image streaming to the remote device. The rest of the image will have to be flashed to the remote device along with any relevant config and developed there.", font);
    Checkbox dirtyRectCB((float)(windowWidth - 150), (float)(windowHeight - 22), 12, "Show dirty rects", font, 4, -2, settings.preferences.showDirtyRects);
    dirtyRectCB.setLabelColor(sf::Color::White);
    TextInput flashDriveInput(400, 176, 40, 24, settings.network.espDrive, font);
    Button flashBtn(450, 176, 90, 24, "MemFlash", font);
    flashBtn.setColor(sf::Color(0, 64, 255), sf::Color(54, 99, 235));
    flashBtn.setLabelColor(sf::Color::White);
    flashModeInfo.setExtraHeight(30);
    flashModeInfo.enableHoverOverBox(true);
    Checkbox realtimeCB((float)(windowWidth - 280), (float)(windowHeight - 22), 12, "Real-time preview", font, 4, -2, settings.preferences.frameLockRealTimePreview);
    realtimeCB.setLabelColor(sf::Color::White);
    float previewCompositeCBX0 = (float)(windowWidth - 410);
    float previewCompositeCBX1 = (float)(windowWidth - 280);
    Checkbox previewCompositeCB(previewCompositeCBX0, (float)(windowHeight - 22), 12, "Preview composite", font, 4, -2, true);
    previewCompositeCB.setLabelColor(sf::Color::White);
    InfoIcon settingsInfo((float)(windowWidth - 50), 10, 15, "resources/Settings.png", "Settings", font, InfoBoxDirection::Left);
    settingsInfo.setExtraHeight(60);
    settingsInfo.enableHoverOverBox(true);
    bool startupSettingChecked = false;
    Checkbox startupSettingCB((float)(windowWidth - 200), 64, 12, "Start with Windows", font, 4, -2, startupSettingChecked);
    Checkbox closeToTraySettingCB((float)(windowWidth - 200), 84, 12, "Close to tray", font, 4, -2, settings.preferences.closeToTray);
    
    // Status indicator
    sf::CircleShape statusIndicator(8);
    statusIndicator.setPosition(sf::Vector2f((float)(windowWidth - 28), (float)(menuHeight / 2 - 8)));
    statusIndicator.setFillColor(sf::Color::Red);
    sf::CircleShape statusIndicatorBorder(8);
    statusIndicatorBorder.setPosition(sf::Vector2f((float)(windowWidth - 28), (float)(menuHeight / 2 - 8)));
    statusIndicatorBorder.setOutlineColor(sf::Color::Black);
    statusIndicatorBorder.setOutlineThickness(1);
    statusIndicatorBorder.setFillColor(sf::Color::Transparent);
    
    // Preview sprite
    sf::Sprite previewSprite(qualiaTexture.getTexture());
    int previewX = 20;
    int previewY = 20;
    
    // Preview border
    sf::RectangleShape previewBorder(sf::Vector2f((float)(previewWidth + 4), (float)(previewHeight + 4)));
    previewBorder.setPosition(sf::Vector2f((float)(previewX - 2), (float)(previewY - 2) + menuHeight));
    previewBorder.setFillColor(sf::Color(80, 80, 80));
    
    // Menu bar background
    sf::RectangleShape menuBar(sf::Vector2f((float)windowWidth, (float)menuHeight));
    menuBar.setFillColor(sf::Color(214, 207, 182));
    
    // Status text
    sf::Text statusText(font, statusMsg, 14);
    statusText.setPosition(sf::Vector2f(10, (float)(windowHeight - 25)));
    statusText.setFillColor(sf::Color::White);
    
    // Timing
    sf::Clock sendClock;
    const float sendInterval = 0.05f;  // ~20 FPS target
    
    // Wall clock for animation
    auto startTime = std::chrono::steady_clock::now();
    
    // Flash export status
    std::string flashExportStatus;
    
    // Async connection state
    ConnectionState connectionState = ConnectionState::Disconnected;
    std::thread connectThread;
    std::atomic<bool> connectResult{false};
    std::atomic<bool> connectFinished{false};
    std::string connectingIP;
    sf::Clock ellipsisClock;
    sf::Clock logFlushClock;

    while (window.isOpen()) {

        // Check if user wants to restore from tray
        if (trayManager.ShouldRestore()) {
            trayManager.RestoreFromTray();
        }
        
        // Check if user wants to exit from tray menu
        if (trayManager.ShouldExit()) {
            window.close();
        }

        bool debugFlag = false;

        // Handle events
        bool mousePressed = false;
        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
        
        bool enabledFlashDuringEvents = false;
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                if (settings.preferences.closeToTray) {
                    trayManager.MinimizeToTray();
                } else {
                    window.close();
                }
            }
            if (const auto* buttonPressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (buttonPressed->button == sf::Mouse::Button::Left) {
                    mousePressed = true;
                }
            }
            ipInput.handleEvent(*event, mousePos, window);
            skinDropdown.handleEvent(*event, mousePos, window);
            skinName = skinDropdown.getSelectedValue();
            if (skinName != settings.preferences.selectedSkin) {
                LOG_INFO << "Skin changed from " << settings.preferences.selectedSkin << " to: " << skinName << " (" << (skins[skinName]->initialized ? "initialized" : "not initialized") << ")\n";
                settings.preferences.selectedSkin = skinName;
                debugFlag = true;
                if (!skins[skinName]->initialized) {
                    LOG_INFO << "First time initializing skin: " << skinName << "\n";
                    skins[skinName]->initialize(("skins/" + skinName + "/skin.xml").c_str());
                }

                // Check if new skin supports flash mode and if flash mode is already enabled. 
                if (skins[skinName]->hasFlashConfig()) {
                    flash::AnimeSkinFlashExporter flashChecker(settings.network.espDrive);
                    if (flashChecker.isFlashable() && flashChecker.isFlashEnabled()) {
                        LOG_INFO << "New skin supports flash mode and flash mode is already enabled on the drive. Enabling flash mode for new skin.\n";
                        flashModeCB.setChecked(true);
                        settings.preferences.flashMode = true;
                        enabledFlashDuringEvents = true;
                    }
                }
            }
            dirtyRectCB.handleEvent(*event, mousePos, window);
            settings.preferences.showDirtyRects = dirtyRectCB.isChecked();
            frameLockCB.handleEvent(*event, mousePos, window);
            settings.preferences.frameLock = frameLockCB.isChecked();
            flashModeCB.handleEvent(*event, mousePos, window);
            settings.preferences.flashMode = flashModeCB.isChecked();
            frameLockInfo.handleEvent(*event, mousePos, window);
            flashModeInfo.handleEvent(*event, mousePos, window);
            if (flashModeInfo.isHovered()) {
                flashDriveInput.handleEvent(*event, mousePos, window);
                settings.network.espDrive = flashDriveInput.value;
            }
            if (frameLockCB.isChecked()) {
                realtimeCB.handleEvent(*event, mousePos, window);
                settings.preferences.frameLockRealTimePreview = realtimeCB.isChecked();
            }
            if (settings.preferences.flashMode) {
                previewCompositeCB.setPosition(frameLockCB.isChecked() ? previewCompositeCBX0 : previewCompositeCBX1, (float)(windowHeight - 22));
                previewCompositeCB.handleEvent(*event, mousePos, window);
                skins[skinName]->getFlashConfig().previewComposite = previewCompositeCB.isChecked();
            }
            settingsInfo.handleEvent(*event, mousePos, window);
            if (settingsInfo.isHovered()) {
                startupSettingCB.handleEvent(*event, mousePos, window);
                closeToTraySettingCB.handleEvent(*event, mousePos, window);
                settings.preferences.closeToTray = closeToTraySettingCB.isChecked();
            }
        }


        ipInput.update(mousePos, window);
        skinDropdown.update(mousePos, window);
        flashDriveInput.update(mousePos, window);

        static bool lastFlashModeChecked = settings.preferences.flashMode;
        if (enabledFlashDuringEvents) {
            lastFlashModeChecked = true;
        }
        // Disable flash mode checkbox if skin doesn't support it
        if (skins[skinName]->initialized && !skins[skinName]->hasFlashConfig()) {
            flashModeCB.setChecked(false);
            flashModeCB.setDisabled(true);
            settings.preferences.flashMode = false;
            if (lastFlashModeChecked) {
                lastFlashModeChecked = false;
                LOG_INFO << "Skin does not support flash mode. Disabling flash mode.\n";
            }
        } else {
            flashModeCB.setDisabled(false);

            // Handle flash mode checkbox - controls ENABLED file on device
            if (flashModeCB.isChecked() != lastFlashModeChecked) {
                flash::AnimeSkinFlashExporter exporter(settings.network.espDrive);

                if (flashModeCB.isChecked()) {
                    // Trying to enable - check if flashable and enable
                    LOG_INFO << "Attempting to enable flash mode...\n";
                    if (exporter.isFlashable()) {
                        if (exporter.enableFlashMode()) {
                            settings.preferences.flashMode = true;
                            LOG_INFO << "Flash mode enabled successfully.\n";
                            flashExportStatus = "Flash mode enabled";
                        } else {
                            flashModeCB.setChecked(false);
                            LOG_ERROR << "Failed to enable flash mode on device.\n";
                            flashExportStatus = "Failed to enable flash mode";
                        }
                    } else {
                        flashModeCB.setChecked(false);
                        LOG_ERROR << "Device is not flashable. Make sure the FLASHABLE marker file is present on the drive.\n";
                        flashExportStatus = "Drive not flashable (no FLASHABLE marker)";
                    }
                } else {
                    // Disabling flash mode
                    LOG_INFO << "Attempting to disable flash mode...\n";
                    if (exporter.isFlashable()) {
                        if (exporter.disableFlashMode()) {
                            LOG_INFO << "Flash mode disabled successfully.\n";
                        } else {
                            LOG_ERROR << "Failed to disable flash mode on device.\n";
                        }
                    } else {
                        LOG_WARN << "Device is not flashable. Make sure the FLASHABLE marker file is present on the drive.\n";
                    }
                    settings.preferences.flashMode = false;
                    flashExportStatus = "Flash mode disabled";
                }

                lastFlashModeChecked = flashModeCB.isChecked();
            }
        }
        
        // Handle flash export button
        if (flashBtn.update(mousePos, mousePressed, window)) {
            flash::AnimeSkinFlashExporter exporter(settings.network.espDrive);
            
            // Check if flashable first
            if (!exporter.isFlashable()) {
                flashExportStatus = "Drive not flashable (no FLASHABLE marker)";
            } else {
                // Clear old files first
                exporter.clearAssetDirectory();
                
                // Use same rotation as frame streaming
                flash::ExportRotation rotation = settings.preferences.rotate180 
                    ? flash::ExportRotation::RotNeg90 
                    : flash::ExportRotation::Rot90;
                auto result = exporter.exportSkin(skins[skinName], rotation);
                if (result.success) {
                    flashExportStatus = "Flash export OK: " + std::to_string(result.exportedFiles.size()) + " files";
                    // Also enable flash mode checkbox
                    flashModeCB.setChecked(true);
                    settings.preferences.flashMode = true;
                    lastFlashModeChecked = true;
                } else {
                    flashExportStatus = "Flash export failed: " + result.error;
                }
            }
        }
        
        // Check for send errors from background thread
        if (connected && sender.hadError()) {
            LOG_INFO << "Sender thread reported an error. Disconnecting...\n";
            sender.stop();
            connection.disconnect();
            connected = false;
            connectionState = ConnectionState::Disconnected;
            statusMsg = "Connection lost";
            connectBtn.setLabel("Connect");
            connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
            statusIndicator.setFillColor(sf::Color::Red);
            sender.clearError();
        }
        
        // Connect button
        if (connectBtn.update(mousePos, mousePressed, window)) {
            if (connected) {
                sender.stop();
                connection.disconnect();
                connected = false;
                connectionState = ConnectionState::Disconnected;
                statusMsg = "Disconnected";
                connectBtn.setLabel("Connect");
                connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
                statusIndicator.setFillColor(sf::Color::Red);
            } else if (connectionState == ConnectionState::Disconnected) {
                // Start async connection
                connectionState = ConnectionState::Connecting;
                connectingIP = ipInput.value;
                connectResult = false;
                connectFinished = false;
                ellipsisClock.restart();
                connectBtn.setLabel("Cancel");
                connectBtn.setColor(sf::Color(255, 200, 100), sf::Color(255, 220, 150));
                statusIndicator.setFillColor(sf::Color::Yellow);
                
                // Launch connection thread
                if (connectThread.joinable()) {
                    connectThread.join();
                }
                connectThread = std::thread([&connection, &connectResult, &connectFinished, ip = connectingIP, port = settings.network.espPort]() {
                    connectResult = connection.connect(ip, port);
                    LOG_INFO << "Connection attempt to " << ip << ":" << port << (connectResult ? " succeeded" : " failed") << "\n";
                    connectFinished = true;
                });
            } else if (connectionState == ConnectionState::Connecting) {
                // Cancel connection attempt
                if (connectThread.joinable()) {
                    connectThread.join();
                }
                connection.disconnect();
                connectionState = ConnectionState::Disconnected;
                statusMsg = "Connection cancelled";
                connectBtn.setLabel("Connect");
                connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
                statusIndicator.setFillColor(sf::Color::Red);
            }
        }
        
        // Check async connection result
        if (connectionState == ConnectionState::Connecting) {
            // Animated ellipsis
            int ellipsisHz = 6; // How many times per second to update the ellipsis
            int dots = (int)(ellipsisClock.getElapsedTime().asSeconds() * ellipsisHz) % 4;
            std::string ellipsis(dots, '.');
            statusMsg = "Connecting" + ellipsis;
            
            if (connectFinished) {
                if (connectThread.joinable()) {
                    connectThread.join();
                }
                if (connectResult) {
                    settings.network.espIP = connectingIP;
                    connected = true;
                    connectionState = ConnectionState::Connected;
                    statusMsg = "Connected to " + connectingIP;
                    connectBtn.setLabel("Disconnect");
                    connectBtn.setColor(sf::Color(255, 100, 100), sf::Color(255, 150, 150));
                    statusIndicator.setFillColor(sf::Color::Green);
                    sender.start(&connection);
                    frameLock.reset();  // Reset frame lock timing on new connection
                } else {
                    connectionState = ConnectionState::Disconnected;
                    statusMsg = "Connection timed out";
                    connectBtn.setLabel("Connect");
                    connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
                    statusIndicator.setFillColor(sf::Color::Red);
                }
            }
        }

        if (refreshBtn.update(mousePos, mousePressed, window)) {
            // Force refresh skin parameters
            for (auto& pair : skins) {
                if (pair.second->initialized) {
                    pair.second->initialize(pair.second->xmlFilePath);
                }
            }
        }

        // Update frame lock controller
        frameLock.update();
        
        // Check if sender consumed a frame (for frame lock)
        if (connected && settings.preferences.frameLock && sender.checkAndClearFrameConsumed()) {
            frameLock.onFrameConsumed();
        }
        
        // Get system stats
        SystemStats stats = monitor.getStats();
        WeatherData weather = weatherMonitor.getWeather();
        TrainData train = trainMonitor.getTrain();
        
        // Calculate animation time
        double wallAnimTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        
        // Determine which layers to skip for flash mode
        FlashLayer flashedLayers = FlashLayer::None;
        bool isFlashModeActive = settings.preferences.flashMode;
        if (isFlashModeActive) {
            flashedLayers = skins[skinName]->getFlashConfig().enabledLayers;
        }
        
        // Draw to texture based on mode
        if (connected && settings.preferences.frameLock) {
            double lockedAnimTime = frameLock.getLockedTime();
            
            if (settings.preferences.frameLockRealTimePreview) {
                // Real-time preview: draw with wall time for display
                // previewComposite controls what we SEE, not what we SEND
                if (isFlashModeActive && !skins[skinName]->getFlashConfig().previewComposite) {
                    skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, wallAnimTime, 
                                                   flashedLayers, FLASH_TRANSPARENT_COLOR);
                } else {
                    skins[skinName]->draw(qualiaTexture, stats, weather, train, wallAnimTime);
                }
                
                // Draw with locked time for sending - ALWAYS use drawForFlash when flash mode active
                if (sendClock.getElapsedTime().asSeconds() >= sendInterval && sender.isReadyForFrame()) {
                    if (isFlashModeActive) {
                        skins[skinName]->drawForFlash(lockedTexture, stats, weather, train, lockedAnimTime,
                                                       flashedLayers, FLASH_TRANSPARENT_COLOR);
                    } else {
                        skins[skinName]->draw(lockedTexture, stats, weather, train, lockedAnimTime);
                    }
                    sendClock.restart();
                    if (settings.preferences.rotate180) {
                        textureToRGB565RotNeg90(lockedTexture, frameBuffer);
                    } else {
                        textureToRGB565Rot90(lockedTexture, frameBuffer);
                    }
                    
                    if (isFlashModeActive) {
                        auto flashStats = buildFlashStats(stats, weather, train, skins[skinName]);
                        sender.queueFlashUpdate(flashStats, frameBuffer);
                    } else {
                        sender.queueFrame(frameBuffer);
                    }
                }
            } else {
                // Standard frame lock: previewComposite controls preview, always send with drawForFlash
                if (isFlashModeActive && !skins[skinName]->getFlashConfig().previewComposite) {
                    skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, lockedAnimTime,
                                                   flashedLayers, FLASH_TRANSPARENT_COLOR);
                } else {
                    skins[skinName]->draw(qualiaTexture, stats, weather, train, lockedAnimTime);
                }
                
                if (sendClock.getElapsedTime().asSeconds() >= sendInterval && sender.isReadyForFrame()) {
                    // For sending, ALWAYS use drawForFlash when flash mode is active
                    if (isFlashModeActive) {
                        // Re-render with drawForFlash for sending (preview might have used draw())
                        skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, lockedAnimTime,
                                                       flashedLayers, FLASH_TRANSPARENT_COLOR);
                    }
                    sendClock.restart();
                    if (settings.preferences.rotate180) {
                        textureToRGB565RotNeg90(qualiaTexture, frameBuffer);
                    } else {
                        textureToRGB565Rot90(qualiaTexture, frameBuffer);
                    }
                    
                    if (isFlashModeActive) {
                        auto flashStats = buildFlashStats(stats, weather, train, skins[skinName]);
                        sender.queueFlashUpdate(flashStats, frameBuffer);
                        // Re-render with composite for preview if needed
                        if (skins[skinName]->getFlashConfig().previewComposite) {
                            skins[skinName]->draw(qualiaTexture, stats, weather, train, lockedAnimTime);
                        }
                    } else {
                        sender.queueFrame(frameBuffer);
                    }
                }
            }
            
            // Status message
            float ratio = sender.getCompressionRatio();
            int rects = sender.getLastRectCount();
            size_t packetKB = sender.getLastPacketSize() / 1024;
            std::string lockStatus = frameLock.isFrozen() ? " [FROZEN]" : "";
            std::string flashStatus = isFlashModeActive ? " [FLASH]" : "";
            statusMsg = std::format("Connected | FPS: {:.1f} | {:.0f}% dirty ({} rects, {}KB){}{}", 
                                   sender.getFPS(), ratio * 100.0f, rects, packetKB, lockStatus, flashStatus);
        } else {
            // No frame lock: previewComposite controls preview, always send with drawForFlash
            if (isFlashModeActive && !skins[skinName]->getFlashConfig().previewComposite) {
                skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, wallAnimTime,
                                               flashedLayers, FLASH_TRANSPARENT_COLOR);
            } else {
                skins[skinName]->draw(qualiaTexture, stats, weather, train, wallAnimTime);
            }
            
            if (connected && sendClock.getElapsedTime().asSeconds() >= sendInterval) {
                // For sending, ALWAYS use drawForFlash when flash mode is active
                if (isFlashModeActive) {
                    skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, wallAnimTime,
                                                   flashedLayers, FLASH_TRANSPARENT_COLOR);
                }
                sendClock.restart();
                if (settings.preferences.rotate180) {
                    textureToRGB565RotNeg90(qualiaTexture, frameBuffer);
                } else {
                    textureToRGB565Rot90(qualiaTexture, frameBuffer);
                }
                
                if (isFlashModeActive) {
                    auto flashStats = buildFlashStats(stats, weather, train, skins[skinName]);
                    sender.queueFlashUpdate(flashStats, frameBuffer);
                    // Re-render with composite for preview if needed
                    if (skins[skinName]->getFlashConfig().previewComposite) {
                        skins[skinName]->draw(qualiaTexture, stats, weather, train, wallAnimTime);
                    }
                } else {
                    sender.queueFrame(frameBuffer);
                }
                
                float ratio = sender.getCompressionRatio();
                int rects = sender.getLastRectCount();
                size_t packetKB = sender.getLastPacketSize() / 1024;
                std::string flashStatus = isFlashModeActive ? " [FLASH]" : "";
                statusMsg = std::format("Connected | FPS: {:.1f} | {:.0f}% dirty ({} rects, {}KB){}", 
                                       sender.getFPS(), ratio * 100.0f, rects, packetKB, flashStatus);
            }
        }
        
        // Update status text
        if (!flashExportStatus.empty()) {
            statusMsg = flashExportStatus;
        }
        statusText.setString(statusMsg);
        
        // Draw window
        window.clear(sf::Color(60, 60, 60));
        
        // Preview
        window.draw(previewBorder);
        previewSprite.setTexture(qualiaTexture.getTexture());
        previewSprite.setOrigin(sf::Vector2f((float)previewWidth / 2, (float)previewHeight / 2));
        previewSprite.setPosition(sf::Vector2f((float)previewX + (float)previewWidth / 2, (float)previewY + (float)previewHeight / 2 + menuHeight));
        window.draw(previewSprite);

        // // Draw dot at center of preview for alignment reference
        // sf::CircleShape centerDot(3);
        // centerDot.setFillColor(sf::Color::Red);
        // centerDot.setPosition(sf::Vector2f((float)previewX + (float)previewHeight / 2 - 3, (float)previewY + (float)previewWidth / 2 - 3));
        // window.draw(centerDot);

        // Show dirty rectangles on preview
        if (connected && settings.preferences.showDirtyRects) {
            std::vector<qualia::DirtyRect> dirtyRects = sender.getLastDirtyRects();
            for (const auto& rect : dirtyRects) {
                // Need to unrotate
                int x = rect.x;
                int y = rect.y;
                int w = rect.w;
                int h = rect.h;
                if (settings.preferences.rotate180) {
                    x = qualia::DISPLAY_HEIGHT - rect.y + 1 - rect.h;
                    y = rect.x;
                    w = rect.h;
                    h = rect.w;
                } else {
                    x = rect.y;
                    y = qualia::DISPLAY_WIDTH - rect.x + 1 - rect.w;
                    w = rect.h;
                    h = rect.w;
                }
                sf::RectangleShape r(sf::Vector2f((float)w, (float)h));
                r.setPosition(sf::Vector2f((float)(previewX + x), (float)(previewY + y + menuHeight)));
                r.setFillColor(sf::Color(255, 0, 0, 100));
                window.draw(r);
            }
        }

        // Menu bar
        window.draw(menuBar);
        ipInput.draw(window);
        connectBtn.draw(window);
        skinDropdown.draw(window);
        refreshBtn.draw(window);
        frameLockCB.draw(window);
        flashModeCB.draw(window);
        flashModeInfo.draw(window);
        frameLockInfo.draw(window);
        if (flashModeInfo.isHovered()) {
            flashDriveInput.draw(window);
            flashBtn.draw(window);
        }
        settingsInfo.draw(window);
        if (settingsInfo.isHovered()) {
            startupSettingCB.draw(window);
            closeToTraySettingCB.draw(window);
        }
        window.draw(statusIndicator);
        window.draw(statusIndicatorBorder);  
        
        // Status bar
        window.draw(statusText);
        if (frameLockCB.isChecked()) {
            realtimeCB.draw(window);
        }
        if (settings.preferences.flashMode) {
            previewCompositeCB.draw(window);
        }
        dirtyRectCB.draw(window);
        
        window.display();
        
        // Clear flash export status after a few seconds
        static sf::Clock flashStatusClock;
        if (!flashExportStatus.empty()) {
            if (flashStatusClock.getElapsedTime().asSeconds() > 3.0f) {
                flashExportStatus.clear();
            }
        } else {
            flashStatusClock.restart();
        }
    }

    settings.save();
    
    // Clean shutdown
    if (connectThread.joinable()) {
        connectThread.join();
    }
    if (connected) {
        sender.stop();
        connection.disconnect();
    }
    
    
    return 0;
}