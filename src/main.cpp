// qualia_streamer.cpp
// SFML GUI application for streaming to Qualia display over WiFi
//
// Build with CMake (see CMakeLists.txt)
#define UNICODE

#include <winsock2.h>

#include "system_stats.h"
#include "image.hpp"

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

#include "ui/text_box.cpp"
#include "ui/button.cpp"

const int TCP_PORT = 8765;

// TCP Connection class
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
    
    bool connect(const std::string& host) {
        disconnect();
        
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) return false;
        
        // Disable Nagle for lower latency
        int flag = 1;
        setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
        
        // Increase send buffer
        int bufSize = 256 * 1024;
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize));
        
        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(TCP_PORT);
        
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
    
    bool sendFrame(const uint16_t* data, size_t pixelCount) {
        if (!isConnected()) return false;
        
        const char* ptr = reinterpret_cast<const char*>(data);
        int remaining = static_cast<int>(pixelCount * 2);
        
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
    
private:
    SOCKET sock_;
};

// Threaded frame sender
class FrameSender {
public:
    FrameSender() : running_(false), frameReady_(false), sendError_(false) {}
    
    ~FrameSender() {
        stop();
    }
    
    void start(TcpConnection* conn) {
        connection_ = conn;
        running_ = true;
        sendError_ = false;
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
            sendBuffer_ = frame.pixels;
            frameReady_ = true;
        }
        cv_.notify_one();
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
                // Copy buffer while holding lock
                std::vector<uint16_t> toSend = std::move(sendBuffer_);
                frameReady_ = false;
                lock.unlock();
                
                // Send without holding lock
                if (!connection_->sendFrame(toSend.data(), toSend.size())) {
                    sendError_ = true;
                }
            }
        }
    }
    
    TcpConnection* connection_ = nullptr;
    std::thread sendThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<uint16_t> sendBuffer_;
    std::atomic<bool> running_;
    std::atomic<bool> frameReady_;
    std::atomic<bool> sendError_;
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

// Draw the content that will be sent to Qualia
void drawQualiaContent(sf::RenderTexture& texture, SystemStats& stats, sf::Font& font) {
    texture.clear(sf::Color::Black);
    
    int y = 20;
    
    // Title
    sf::Text title(font, "SYSTEM MONITOR", 28);
    title.setPosition(sf::Vector2f(20, (float)y));
    title.setFillColor(sf::Color(100, 200, 255));
    texture.draw(title);
    y += 50;
    
    // CPU section
    sf::Text cpuLabel(font, "CPU", 18);
    cpuLabel.setPosition(sf::Vector2f(20, (float)y));
    cpuLabel.setFillColor(sf::Color::White);
    texture.draw(cpuLabel);
    y += 30;
    
    // CPU bar background
    sf::RectangleShape cpuBarBg(sf::Vector2f((float)(qualia::DISPLAY_WIDTH - 40), 25));
    cpuBarBg.setPosition(sf::Vector2f(20, (float)y));
    cpuBarBg.setFillColor(sf::Color(0, 100, 0));
    texture.draw(cpuBarBg);
    
    // CPU bar fill
    float cpuBarWidth = (qualia::DISPLAY_WIDTH - 40) * stats.cpuPercent / 100.0f;
    sf::RectangleShape cpuBarFill(sf::Vector2f(cpuBarWidth, 25));
    cpuBarFill.setPosition(sf::Vector2f(20, (float)y));
    cpuBarFill.setFillColor(sf::Color::Green);
    texture.draw(cpuBarFill);
    
    // CPU percentage text
    char cpuText[32];
    snprintf(cpuText, sizeof(cpuText), "%.1f%%", stats.cpuPercent);
    sf::Text cpuPct(font, cpuText, 18);
    cpuPct.setPosition(sf::Vector2f((float)(qualia::DISPLAY_WIDTH - 70), (float)(y + 2)));
    cpuPct.setFillColor(sf::Color::White);
    texture.draw(cpuPct);

    // CPU temperature text
    char tempText[32];
    snprintf(tempText, sizeof(tempText), "Temp: %.1f C", stats.cpuTempC);
    sf::Text tempLabel(font, tempText, 14);
    tempLabel.setPosition(sf::Vector2f(20, (float)(y + 30)));
    tempLabel.setFillColor(sf::Color(150, 150, 150));
    texture.draw(tempLabel);
    y += 45;
    
    // Memory section
    sf::Text memLabel(font, "MEMORY", 18);
    memLabel.setPosition(sf::Vector2f(20, (float)y));
    memLabel.setFillColor(sf::Color::White);
    texture.draw(memLabel);
    y += 30;
    
    // Memory bar background
    sf::RectangleShape memBarBg(sf::Vector2f((float)(qualia::DISPLAY_WIDTH - 40), 25));
    memBarBg.setPosition(sf::Vector2f(20, (float)y));
    memBarBg.setFillColor(sf::Color(0, 0, 100));
    texture.draw(memBarBg);
    
    // Memory bar fill
    float memBarWidth = (qualia::DISPLAY_WIDTH - 40) * stats.memPercent / 100.0f;
    sf::RectangleShape memBarFill(sf::Vector2f(memBarWidth, 25));
    memBarFill.setPosition(sf::Vector2f(20, (float)y));
    memBarFill.setFillColor(sf::Color::Blue);
    texture.draw(memBarFill);
    
    // Memory percentage text
    char memText[32];
    snprintf(memText, sizeof(memText), "%.1f%%", stats.memPercent);
    sf::Text memPct(font, memText, 18);
    memPct.setPosition(sf::Vector2f((float)(qualia::DISPLAY_WIDTH - 70), (float)(y + 2)));
    memPct.setFillColor(sf::Color::White);
    texture.draw(memPct);
    y += 35;
    
    // Memory usage text
    char memUsage[64];
    snprintf(memUsage, sizeof(memUsage), "%llu / %llu MB",
             (unsigned long long)stats.memUsedMB,
             (unsigned long long)stats.memTotalMB);
    sf::Text memUsageText(font, memUsage, 14);
    memUsageText.setPosition(sf::Vector2f(20, (float)y));
    memUsageText.setFillColor(sf::Color(150, 150, 150));
    texture.draw(memUsageText);
    
    texture.display();
}

int main() {
    if (!IsUserAnAdmin()) {
        std::cout << "This application must be run as administrator.\n";
        return 1;
    }

    const int menuHeight = 40;
    const int previewScale = 1;
    const int previewWidth = qualia::DISPLAY_WIDTH / previewScale;
    const int previewHeight = qualia::DISPLAY_HEIGHT / previewScale;
    const int windowWidth = previewHeight + 40; // Use previewHeight since the preview will be rotated 90 degrees
    const int windowHeight = menuHeight + previewWidth + 50; // Use previewWidth since the preview will be rotated 90 degrees
    
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(windowWidth, windowHeight)), "Qualia Streamer", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(30);
    
    // Load font
    sf::Font font;
    if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
        std::cerr << "Failed to load font\n";
        return 1;
    }
    
    // Create render texture at Qualia's native resolution
    sf::RenderTexture qualiaTexture(sf::Vector2u(qualia::DISPLAY_WIDTH, qualia::DISPLAY_HEIGHT));
    
    // RGB565 image buffer for sending
    qualia::Image frameBuffer(qualia::DISPLAY_WIDTH, qualia::DISPLAY_HEIGHT);
    
    // TCP connection and sender thread
    TcpConnection connection;
    FrameSender sender;
    bool connected = false;
    
    // System monitor
    SystemMonitor monitor;
    
    // Status
    std::string statusMsg = "Disconnected";
    
    // UI elements
    TextInput ipInput(10, 8, 120, 24, "10.0.0.34", font);
    Button connectBtn(140, 8, 90, 24, "Connect", font);
    connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
    
    // Status indicator
    sf::CircleShape statusIndicator(8);
    statusIndicator.setPosition(sf::Vector2f((float)(windowWidth - 28), (float)(menuHeight / 2 - 8)));
    statusIndicator.setFillColor(sf::Color::Red);
    
    // Preview sprite
    sf::Sprite previewSprite(qualiaTexture.getTexture());
    int previewX = 20;
    int previewY = 20;
    
    // Preview border
    sf::RectangleShape previewBorder(sf::Vector2f((float)(previewHeight + 4), (float)(previewWidth + 4)));
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
    const float sendInterval = 0.1f;
    
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
                if (connection.connect(ipInput.value)) {
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
        drawQualiaContent(qualiaTexture, stats, font);
        
        // Queue frame for sending (non-blocking)
        if (connected && sendClock.getElapsedTime().asSeconds() >= sendInterval) {
            sendClock.restart();
            textureToRGB565(qualiaTexture, frameBuffer);
            sender.queueFrame(frameBuffer);
        }
        
        // Update status text
        statusText.setString(statusMsg);
        
        // Draw window
        window.clear(sf::Color(60, 60, 60));
        
        // Menu bar
        window.draw(menuBar);
        ipInput.draw(window);
        connectBtn.draw(window);
        window.draw(statusIndicator);
        
        // Preview (rotated 90 degrees)
        window.draw(previewBorder);
        previewSprite.setTexture(qualiaTexture.getTexture());
        previewSprite.setOrigin(sf::Vector2f((float)previewWidth / 2, (float)previewHeight / 2));
        previewSprite.setPosition(sf::Vector2f((float)previewX + (float)previewHeight / 2, (float)previewY + (float)previewWidth / 2 + menuHeight));
        previewSprite.setScale(sf::Vector2f((float)previewWidth / qualia::DISPLAY_WIDTH, 
                               (float)previewHeight / qualia::DISPLAY_HEIGHT));
        previewSprite.setRotation(sf::degrees(-90));
        window.draw(previewSprite);

        // // Draw dot at center of preview for alignment reference
        // sf::CircleShape centerDot(3);
        // centerDot.setFillColor(sf::Color::Red);
        // centerDot.setPosition(sf::Vector2f((float)previewX + (float)previewHeight / 2 - 3, (float)previewY + (float)previewWidth / 2 - 3));
        // window.draw(centerDot);
        
        // Status bar
        window.draw(statusText);
        
        window.display();
    }
    
    // Clean shutdown
    if (connected) {
        sender.stop();
        connection.disconnect();
    }
    
    return 0;
}