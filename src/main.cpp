#define UNICODE

#include <winsock2.h>

#include "system_stats.h"
#include "image.hpp"
#include "dirty_rects.hpp"

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
#include "skins/skin.h"
#include "skins/debug_skin.cpp"
#include "skins/anime_skin.cpp"
#include "settings.hpp"
#include "weather.hpp"
#include "train.hpp"

class TcpConnection {
public:
    TcpConnection() : sock_(INVALID_SOCKET) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    
    ~TcpConnection() {
        disconnect();
        WSACleanup();
    }
    
    bool connect(const std::string& host, const int tcp_port) {
        disconnect();
        
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) return false;
        
        // Disable Nagle for lower latency
        int flag = 1;
        setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
        
        // Send buffer
        int bufSize = 256 * 1024;
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize));
        
        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(tcp_port);
        
        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
        
        if (::connect(sock_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
        
        return true;
    }
    
    void disconnect() {
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
    }
    
    bool isConnected() const { return sock_ != INVALID_SOCKET; }
    
    // Send arbitrary byte buffer
    bool sendPacket(const uint8_t* data, size_t size) {
        if (!isConnected()) return false;
        
        const char* ptr = reinterpret_cast<const char*>(data);
        int remaining = static_cast<int>(size);
        
        while (remaining > 0) {
            int sent = send(sock_, ptr, remaining, 0);
            if (sent == SOCKET_ERROR) {
                disconnect();
                return false;
            }
            ptr += sent;
            remaining -= sent;
        }
        return true;
    }
    
    bool sendFrame(const uint16_t* data, size_t pixelCount) {
        return sendPacket(reinterpret_cast<const uint8_t*>(data), pixelCount * 2);
    }
    
private:
    SOCKET sock_;
};

// Threaded frame sender
class FrameSender {
public:
    FrameSender(int fpsWindow = 10) 
        : running_(false), frameReady_(false), sendError_(false), fpsWindow_(fpsWindow) {}
    
    ~FrameSender() {
        stop();
    }
    
    void start(TcpConnection* conn) {
        connection_ = conn;
        running_ = true;
        sendError_ = false;
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
            // Copy frame data to send buffer
            pendingFrame_ = frame;
            frameReady_ = true;
        }
        cv_.notify_one();
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
                // Copy frame while holding lock
                qualia::Image frameToSend = std::move(pendingFrame_);
                frameReady_ = false;
                lock.unlock();
                
                // Find dirty rectangles
                auto rects = dirtyTracker_.findDirtyRects(frameToSend);
                
                // Build packet with dirty rect protocol
                std::vector<uint8_t> packet = dirtyTracker_.buildPacket(frameToSend, rects);
                
                // Update stats
                {
                    std::lock_guard<std::mutex> slock(statsMutex_);
                    auto stats = dirtyTracker_.getLastStats(rects);
                    lastCompressionRatio_ = stats.compressionRatio;
                    lastRectCount_ = stats.rectCount;
                    lastPacketSize_ = packet.size();
                    lastDirtyRects_ = rects; // Store for debugging
                }
                
                // Send packet
                if (connection_->sendPacket(packet.data(), packet.size())) {
                    recordFrameSent();
                } else {
                    sendError_ = true;
                }
            }
        }
    }
    
    void recordFrameSent() {
        std::lock_guard<std::mutex> lock(fpsMutex_);
        frameTimestamps_.push_back(std::chrono::steady_clock::now());
    }
    
    TcpConnection* connection_ = nullptr;
    std::thread sendThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    qualia::Image pendingFrame_;
    std::atomic<bool> running_;
    std::atomic<bool> frameReady_;
    std::atomic<bool> sendError_;
    
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

int main() {
    if (!IsUserAnAdmin()) {
        std::cout << "This application must be run as administrator.\n";
        return 1;
    }

    std::cout << "Starting Sketchbook...\n";

    Settings settings;
    if (!settings.load()) {
        std::cerr << "Failed to load settings.toml\n";
        return 1;
    }

    if (settings.weather.apiKey == "YOUR_API_KEY_HERE" || settings.weather.apiKey.empty()) {
        std::cerr << "Please set your OpenWeatherMap API key in settings.toml\n";
        return 1;
    }

    std::cout << "Successfully loaded settings.\n";

    const int menuHeight = 40;
    const int previewScale = 1;
    const int previewWidth = qualia::DISPLAY_HEIGHT / previewScale; // Use DISPLAY_HEIGHT since the preview will be rotated 90 degrees
    const int previewHeight = qualia::DISPLAY_WIDTH / previewScale; // Use DISPLAY_WIDTH since the preview will be rotated 90 degrees
    const int windowWidth = previewWidth + 40; 
    const int windowHeight = menuHeight + previewHeight + 50;
    
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(windowWidth, windowHeight)), "Sketchbook", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(30);
    
    // Load font
    sf::Font font;
    if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
        std::cerr << "Failed to load font\n";
        return 1;
    }

    std::unordered_map<std::string, Skin*> skins;
    std::string skinName = "Debug";
    DebugSkin debugSkin = DebugSkin(std::string("Debug"), qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH);
    debugSkin.initialize("");
    skins["Debug"] = &debugSkin;
    AnimeSkin animeSkin = AnimeSkin(std::string("Sketchbook"), qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH);
    animeSkin.initialize("skins/sketchbook/skin.xml");
    skins["Sketchbook"] = &animeSkin;
    
    // Create render texture at Qualia's native resolution
    sf::RenderTexture qualiaTexture(sf::Vector2u(qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH)); // Swapped dimensions for 90 degree rotation
    
    // RGB565 image buffer for sending
    qualia::Image frameBuffer(qualia::DISPLAY_WIDTH, qualia::DISPLAY_HEIGHT);
    
    // TCP connection and sender thread
    TcpConnection connection;
    FrameSender sender;
    bool connected = false;
    
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
    DropdownSelector skinDropdown(240, 8, 120, 24, skinOptions, font);
    
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
    menuBar.setFillColor(sf::Color(180, 180, 180));
    
    // Status text
    sf::Text statusText(font, statusMsg, 14);
    statusText.setPosition(sf::Vector2f(10, (float)(windowHeight - 25)));
    statusText.setFillColor(sf::Color::White);
    
    // Timing
    sf::Clock sendClock;
    const float sendInterval = 0.05f;  // ~20 FPS target
    
    while (window.isOpen()) {
        // Handle events
        bool mousePressed = false;
        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
        
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
            if (const auto* buttonPressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (buttonPressed->button == sf::Mouse::Button::Left) {
                    mousePressed = true;
                }
            }
            ipInput.handleEvent(*event, mousePos, window);
            skinDropdown.handleEvent(*event, mousePos, window);
            skinName = skinDropdown.getSelectedValue();
        }
        
        // Check for send errors from background thread
        if (connected && sender.hadError()) {
            sender.stop();
            connection.disconnect();
            connected = false;
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
                statusMsg = "Disconnected";
                connectBtn.setLabel("Connect");
                connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
                statusIndicator.setFillColor(sf::Color::Red);
            } else {
                statusMsg = "Connecting...";
                if (connection.connect(ipInput.value, settings.network.espPort)) {
                    settings.network.espIP = ipInput.value;
                    connected = true;
                    statusMsg = "Connected to " + ipInput.value;
                    connectBtn.setLabel("Disconnect");
                    connectBtn.setColor(sf::Color(255, 100, 100), sf::Color(255, 150, 150));
                    statusIndicator.setFillColor(sf::Color::Green);
                    sender.start(&connection);
                } else {
                    statusMsg = "Connection failed";
                }
            }
        }
        
        // Get system stats and draw to texture
        SystemStats stats = monitor.getStats();
        WeatherData weather = weatherMonitor.getWeather();
        TrainData train = trainMonitor.getTrain();
        skins[skinName]->draw(qualiaTexture, stats, weather, train);
        
        // Queue frame for sending (non-blocking)
        if (connected && sendClock.getElapsedTime().asSeconds() >= sendInterval) {
            // Rotate texture 90 degrees and convert to RGB565 in one step while copying to frame buffer
            sendClock.restart();
            if (settings.preferences.rotate180) {
                textureToRGB565RotNeg90(qualiaTexture, frameBuffer);
            } else {
                textureToRGB565Rot90(qualiaTexture, frameBuffer);
            }
            sender.queueFrame(frameBuffer);
            
            // Show compression stats
            float ratio = sender.getCompressionRatio();
            int rects = sender.getLastRectCount();
            size_t packetKB = sender.getLastPacketSize() / 1024;
            statusMsg = std::format("Connected | FPS: {:.1f} | {:.0f}% dirty ({} rects, {}KB)", 
                                   sender.getFPS(), ratio * 100.0f, rects, packetKB);
        }
        
        // Update status text
        statusText.setString(statusMsg);
        
        // Draw window
        window.clear(sf::Color(60, 60, 60));
        
        // Preview (rotated 90 degrees)
        window.draw(previewBorder);
        previewSprite.setTexture(qualiaTexture.getTexture());
        previewSprite.setOrigin(sf::Vector2f((float)previewWidth / 2, (float)previewHeight / 2));
        previewSprite.setPosition(sf::Vector2f((float)previewX + (float)previewWidth / 2, (float)previewY + (float)previewHeight / 2 + menuHeight));
        // previewSprite.setScale(sf::Vector2f((float)previewWidth / qualia::DISPLAY_WIDTH, 
        //                        (float)previewHeight / qualia::DISPLAY_HEIGHT));
        // previewSprite.setRotation(sf::degrees(-90));
        window.draw(previewSprite);

        // // Draw dot at center of preview for alignment reference
        // sf::CircleShape centerDot(3);
        // centerDot.setFillColor(sf::Color::Red);
        // centerDot.setPosition(sf::Vector2f((float)previewX + (float)previewHeight / 2 - 3, (float)previewY + (float)previewWidth / 2 - 3));
        // window.draw(centerDot);

        // Menu bar
        window.draw(menuBar);
        ipInput.draw(window);
        connectBtn.draw(window);
        skinDropdown.draw(window);
        window.draw(statusIndicator);
        window.draw(statusIndicatorBorder);

        // Debugging: show dirty rectangles on preview
        if (connected) {
            std::vector<qualia::DirtyRect> dirtyRects = sender.getLastDirtyRects();
            for (const auto& rect : dirtyRects) {
                // Need to unrotate
                int x = rect.x;
                int y = rect.y;
                int w = rect.w;
                int h = rect.h;
                if (settings.preferences.rotate180) {
                    // -90 rotation: (x, y) -> (height-1-y, x)
                    x = qualia::DISPLAY_HEIGHT - rect.y + 1 - rect.h;
                    y = rect.x;
                    w = rect.h;
                    h = rect.w;
                } else {
                    // 90 rotation: (x, y) -> (y, width-1-x)
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
        
        // Status bar
        window.draw(statusText);
        
        window.display();
    }

    settings.save();
    
    // Clean shutdown
    if (connected) {
        sender.stop();
        connection.disconnect();
    }
    
    return 0;
}