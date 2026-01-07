// main.cpp
// System monitor for Qualia display (Windows) - Console version

#include "image.hpp"
#include "serial_port.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cmath>

#include <windows.h>
#include <pdh.h>

// Simple bitmap font (5x7 pixels per character)
namespace font {
    constexpr int CHAR_WIDTH = 6;
    constexpr int CHAR_HEIGHT = 8;
    
    const uint8_t DATA[][5] = {
        {0x00, 0x00, 0x00, 0x00, 0x00}, // space
        {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
        {0x00, 0x07, 0x00, 0x07, 0x00}, // "
        {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
        {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
        {0x23, 0x13, 0x08, 0x64, 0x62}, // %
        {0x36, 0x49, 0x55, 0x22, 0x50}, // &
        {0x00, 0x05, 0x03, 0x00, 0x00}, // '
        {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
        {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
        {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
        {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
        {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
        {0x08, 0x08, 0x08, 0x08, 0x08}, // -
        {0x00, 0x60, 0x60, 0x00, 0x00}, // .
        {0x20, 0x10, 0x08, 0x04, 0x02}, // /
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
        {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
        {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
        {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
        {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
        {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
        {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
        {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
        {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
        {0x00, 0x36, 0x36, 0x00, 0x00}, // :
        {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
        {0x00, 0x08, 0x14, 0x22, 0x41}, // <
        {0x14, 0x14, 0x14, 0x14, 0x14}, // =
        {0x41, 0x22, 0x14, 0x08, 0x00}, // >
        {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
        {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
        {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
        {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
        {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
        {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
        {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
        {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
        {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
        {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
        {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
        {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
        {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
        {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
        {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
        {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
        {0x46, 0x49, 0x49, 0x49, 0x31}, // S
        {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
        {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
        {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
        {0x63, 0x14, 0x08, 0x14, 0x63}, // X
        {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
        {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
        {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
        {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
        {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
        {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
        {0x40, 0x40, 0x40, 0x40, 0x40}, // _
        {0x00, 0x01, 0x02, 0x04, 0x00}, // `
        {0x20, 0x54, 0x54, 0x54, 0x78}, // a
        {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
        {0x38, 0x44, 0x44, 0x44, 0x20}, // c
        {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
        {0x38, 0x54, 0x54, 0x54, 0x18}, // e
        {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
        {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
        {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
        {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
        {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
        {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
        {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
        {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
        {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
        {0x38, 0x44, 0x44, 0x44, 0x38}, // o
        {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
        {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
        {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
        {0x48, 0x54, 0x54, 0x54, 0x20}, // s
        {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
        {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
        {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
        {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
        {0x44, 0x28, 0x10, 0x28, 0x44}, // x
        {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
        {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
        {0x00, 0x08, 0x36, 0x41, 0x00}, // {
        {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
        {0x00, 0x41, 0x36, 0x08, 0x00}, // }
        {0x08, 0x08, 0x2A, 0x1C, 0x08}, // ~
    };
    
    void drawChar(qualia::Image& img, int x, int y, char c, qualia::Pixel color) {
        if (c < 32 || c > 126) c = '?';
        int idx = c - 32;
        
        for (int col = 0; col < 5; col++) {
            uint8_t colData = DATA[idx][col];
            for (int row = 0; row < 7; row++) {
                if (colData & (1 << row)) {
                    img.setPixel(x + col, y + row, color);
                }
            }
        }
    }
    
    void drawString(qualia::Image& img, int x, int y, const std::string& str, qualia::Pixel color) {
        for (size_t i = 0; i < str.length(); i++) {
            drawChar(img, x + i * CHAR_WIDTH, y, str[i], color);
        }
    }
    
    void drawStringLarge(qualia::Image& img, int x, int y, const std::string& str, qualia::Pixel color, int scale = 2) {
        for (size_t i = 0; i < str.length(); i++) {
            char c = str[i];
            if (c < 32 || c > 126) c = '?';
            int idx = c - 32;
            
            for (int col = 0; col < 5; col++) {
                uint8_t colData = DATA[idx][col];
                for (int row = 0; row < 7; row++) {
                    if (colData & (1 << row)) {
                        img.fillRect(x + (i * 6 + col) * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
    }
}

// System stats gathering
struct SystemStats {
    float cpuPercent = 0;
    float memPercent = 0;
    uint64_t memUsedMB = 0;
    uint64_t memTotalMB = 0;
};

class SystemMonitor {
public:
    SystemMonitor() {
        PdhOpenQuery(nullptr, 0, &cpuQuery_);
        PdhAddEnglishCounterA(cpuQuery_, "\\Processor(_Total)\\% Processor Time", 0, &cpuCounter_);
        PdhCollectQueryData(cpuQuery_);
    }
    
    ~SystemMonitor() {
        PdhCloseQuery(cpuQuery_);
    }
    
    SystemStats getStats() {
        SystemStats stats;
        
        PdhCollectQueryData(cpuQuery_);
        PDH_FMT_COUNTERVALUE value;
        PdhGetFormattedCounterValue(cpuCounter_, PDH_FMT_DOUBLE, nullptr, &value);
        stats.cpuPercent = static_cast<float>(value.doubleValue);
        
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(memInfo);
        GlobalMemoryStatusEx(&memInfo);
        stats.memPercent = static_cast<float>(memInfo.dwMemoryLoad);
        stats.memTotalMB = memInfo.ullTotalPhys / (1024 * 1024);
        stats.memUsedMB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
        
        return stats;
    }

private:
    PDH_HQUERY cpuQuery_;
    PDH_HCOUNTER cpuCounter_;
};

// Colors
namespace colors {
    constexpr auto BLACK = qualia::rgb565(0, 0, 0);
    constexpr auto WHITE = qualia::rgb565(255, 255, 255);
    constexpr auto GRAY = qualia::rgb565(100, 100, 100);
    constexpr auto DARK_GRAY = qualia::rgb565(40, 40, 40);
    constexpr auto GREEN = qualia::rgb565(0, 255, 0);
    constexpr auto DARK_GREEN = qualia::rgb565(0, 100, 0);
    constexpr auto BLUE = qualia::rgb565(50, 150, 255);
    constexpr auto DARK_BLUE = qualia::rgb565(20, 60, 100);
    constexpr auto RED = qualia::rgb565(255, 50, 50);
    constexpr auto YELLOW = qualia::rgb565(255, 255, 0);
    constexpr auto CYAN = qualia::rgb565(0, 255, 255);
}

// Draw a horizontal bar graph
void drawBar(qualia::Image& img, int x, int y, int width, int height, 
             float percent, qualia::Pixel fgColor, qualia::Pixel bgColor) {
    // Background
    img.fillRect(x, y, width, height, bgColor);
    
    // Foreground (filled portion)
    int fillWidth = static_cast<int>(width * percent / 100.0f);
    img.fillRect(x, y, fillWidth, height, fgColor);
    
    // Border
    img.drawRect(x, y, width, height, colors::GRAY);
}

int main(int argc, char* argv[]) {
    std::string port;
    
    if (argc > 1) {
        port = argv[1];
    } else {
        std::cout << "Usage: " << argv[0] << " COMx\n";
        std::cout << "Example: " << argv[0] << " COM5\n";
        return 1;
    }
    
    std::cout << "Connecting to Qualia on " << port << "...\n";
    
    SerialDisplay display;
    if (!display.connect(port)) {
        std::cerr << "Failed to connect to Qualia\n";
        return 1;
    }
    
    std::cout << "Connected! Starting system monitor...\n";
    std::cout << "Press Ctrl+C to exit.\n";
    
    qualia::Image frame(qualia::DISPLAY_WIDTH, qualia::DISPLAY_HEIGHT);
    SystemMonitor monitor;
    
    int frameCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    size_t totalBytes = 0;
    
    try {
        while (true) {
            auto frameStart = std::chrono::steady_clock::now();
            
            SystemStats stats = monitor.getStats();
            
            // Clear frame
            frame.clear(colors::BLACK);
            
            int y = 20;
            
            // Title
            font::drawStringLarge(frame, 20, y, "SYSTEM MONITOR", colors::CYAN, 3);
            y += 40;
            
            // Separator line
            frame.fillRect(10, y, qualia::DISPLAY_WIDTH - 20, 2, colors::DARK_GRAY);
            y += 20;
            
            // CPU section
            font::drawStringLarge(frame, 20, y, "CPU", colors::WHITE, 2);
            y += 25;
            
            drawBar(frame, 20, y, qualia::DISPLAY_WIDTH - 40, 30, 
                    stats.cpuPercent, colors::GREEN, colors::DARK_GREEN);
            
            char text[64];
            snprintf(text, sizeof(text), "%.1f%%", stats.cpuPercent);
            font::drawStringLarge(frame, qualia::DISPLAY_WIDTH - 80, y + 5, text, colors::WHITE, 2);
            y += 50;
            
            // Memory section
            font::drawStringLarge(frame, 20, y, "MEMORY", colors::WHITE, 2);
            y += 25;
            
            drawBar(frame, 20, y, qualia::DISPLAY_WIDTH - 40, 30, 
                    stats.memPercent, colors::BLUE, colors::DARK_BLUE);
            
            snprintf(text, sizeof(text), "%.1f%%", stats.memPercent);
            font::drawStringLarge(frame, qualia::DISPLAY_WIDTH - 80, y + 5, text, colors::WHITE, 2);
            y += 40;
            
            snprintf(text, sizeof(text), "%llu / %llu MB", 
                     (unsigned long long)stats.memUsedMB, 
                     (unsigned long long)stats.memTotalMB);
            font::drawString(frame, 20, y, text, colors::GRAY);
            y += 30;
            
            // Separator
            frame.fillRect(10, y, qualia::DISPLAY_WIDTH - 20, 2, colors::DARK_GRAY);
            y += 20;
            
            // Send to display
            if (!display.sendFrame(frame)) {
                std::cerr << "Failed to send frame, reconnecting...\n";
                display.disconnect();
                while (!display.connect(port)) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                frameCount = 0;
                totalBytes = 0;
                startTime = std::chrono::steady_clock::now();
                continue;
            }
            
            totalBytes += frame.dataSize();
            frameCount++;
            
            auto frameEnd = std::chrono::steady_clock::now();
            auto frameTime = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
            auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - startTime).count();
            
            if (totalTime > 0) {
                float fps = frameCount * 1000.0f / totalTime;
                float kbps = (totalBytes / 1024.0f) / (totalTime / 1000.0f);
                
                // Print stats every 10 frames
                if (frameCount % 10 == 0) {
                    std::cout << "Frame " << frameCount 
                              << " | " << frameTime << "ms"
                              << " | FPS: " << fps 
                              << " | " << kbps << " KB/s\n";
                }
            }
            
            // No sleep - run as fast as possible to measure max throughput
        }
    } catch (...) {
        std::cout << "\nExiting...\n";
    }
    
    display.disconnect();
    return 0;
}