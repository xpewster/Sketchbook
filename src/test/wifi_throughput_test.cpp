// wifi_throughput_test.cpp
// Test WiFi throughput for Qualia display
// Connects to Qualia TCP server and sends frames

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <chrono>
#include <cstring>
#include <string>

const int FRAME_WIDTH = 240;
const int FRAME_HEIGHT = 960;
const int FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * 2;
const int TCP_PORT = 8765;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <qualia_ip>\n";
        std::cout << "Example: " << argv[0] << " 192.168.1.100\n";
        return 1;
    }
    
    std::string host = argv[1];
    
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
    
    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }
    
    // Disable Nagle's algorithm for lower latency
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
    
    // Increase send buffer
    int bufSize = FRAME_BYTES * 4;  // Allow buffering several frames
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize));
    
    // Connect
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr);
    
    std::cout << "Connecting to " << host << ":" << TCP_PORT << "...\n";
    
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    
    std::cout << "Connected!\n";
    std::cout << "Press Ctrl+C to stop\n\n";
    
    // Create test frame
    uint16_t* frameData = new uint16_t[FRAME_WIDTH * FRAME_HEIGHT];
    
    int frameCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastReport = startTime;
    
    while (true) {
        auto frameStart = std::chrono::steady_clock::now();
        
        // Generate test pattern
        uint16_t color = (frameCount * 17) & 0xFFFF;
        for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; i++) {
            frameData[i] = color;
        }
        
        // Add moving bar
        int barHeight = (frameCount % 100) * FRAME_HEIGHT / 100;
        for (int y = 0; y < barHeight; y++) {
            for (int x = 0; x < 20; x++) {
                frameData[y * FRAME_WIDTH + x] = 0xFFFF;
            }
        }
        
        // Send frame
        const char* ptr = reinterpret_cast<char*>(frameData);
        int remaining = FRAME_BYTES;
        
        while (remaining > 0) {
            int sent = send(sock, ptr, remaining, 0);
            if (sent == SOCKET_ERROR) {
                std::cerr << "Send failed: " << WSAGetLastError() << "\n";
                goto cleanup;
            }
            ptr += sent;
            remaining -= sent;
        }
        
        frameCount++;
        
        auto frameEnd = std::chrono::steady_clock::now();
        auto frameMs = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
        
        // Report every second
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count();
        
        if (elapsed >= 1000) {
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            float fps = frameCount * 1000.0f / totalMs;
            float kbps = (frameCount * FRAME_BYTES / 1024.0f) / (totalMs / 1000.0f);
            
            std::cout << "Frame " << frameCount
                      << " | " << frameMs << "ms"
                      << " | FPS: " << fps
                      << " | " << kbps << " KB/s\n";
            lastReport = now;
        }
    }
    
cleanup:
    delete[] frameData;
    closesocket(sock);
    WSACleanup();
    return 0;
}