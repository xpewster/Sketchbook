// msc_throughput_test.cpp
// Test mass storage throughput for Qualia display
// Just writes frames to CIRCUITPY drive as fast as possible

#include <windows.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <string>

const int FRAME_WIDTH = 240;
const int FRAME_HEIGHT = 960;
const int FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * 2;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <drive_letter>\n";
        std::cout << "Example: " << argv[0] << " E\n";
        std::cout << "\nWrites frames to CIRCUITPY drive continuously\n";
        return 1;
    }
    
    std::string drivePath = std::string(argv[1]) + ":\\frame.raw";
    
    // Create test frame
    uint16_t* frameData = new uint16_t[FRAME_WIDTH * FRAME_HEIGHT];
    
    std::cout << "Writing frames to " << drivePath << "\n";
    std::cout << "Press Ctrl+C to stop\n\n";
    
    int frameCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastReport = startTime;
    
    while (true) {
        auto writeStart = std::chrono::steady_clock::now();
        
        // Generate test pattern (changes each frame)
        uint16_t color = (frameCount * 17) & 0xFFFF;
        for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; i++) {
            frameData[i] = color;
        }
        // Add frame counter visualization - white bar that moves
        int barHeight = (frameCount % 100) * FRAME_HEIGHT / 100;
        for (int y = 0; y < barHeight; y++) {
            for (int x = 0; x < 20; x++) {
                frameData[y * FRAME_WIDTH + x] = 0xFFFF;  // White bar
            }
        }
        
        // Write frame to file
        std::ofstream file(drivePath, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open " << drivePath << "\n";
            Sleep(1000);
            continue;
        }
        file.write(reinterpret_cast<char*>(frameData), FRAME_BYTES);
        file.close();
        
        auto writeEnd = std::chrono::steady_clock::now();
        auto writeMs = std::chrono::duration_cast<std::chrono::milliseconds>(writeEnd - writeStart).count();
        
        frameCount++;
        
        // Report every second
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count();
        
        if (elapsed >= 1000) {
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            float fps = frameCount * 1000.0f / totalMs;
            float kbps = (frameCount * FRAME_BYTES / 1024.0f) / (totalMs / 1000.0f);
            
            std::cout << "Frame " << frameCount 
                      << " | Write: " << writeMs << "ms"
                      << " | FPS: " << fps
                      << " | " << kbps << " KB/s\n";
            lastReport = now;
        }
    }
    
    delete[] frameData;
    return 0;
}