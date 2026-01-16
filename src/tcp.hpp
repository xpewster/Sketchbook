#pragma once

#include <winsock2.h>

#include "image.hpp"
#include "dirty_rects.hpp"

#include <ws2tcpip.h>

// Connection state for async connection
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected
};

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
    
    // Wait for ACK byte from remote (with timeout)
    // Returns true if ACK received, false on timeout or error
    bool waitForAck(int timeoutMs = 5000) {
        if (!isConnected()) return false;
        
        // Set receive timeout
        DWORD timeout = timeoutMs;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char ack;
        int result = recv(sock_, &ack, 1, 0);
        
        if (result == 1) {
            return true;  // ACK received
        } else if (result == 0) {
            // Connection closed
            disconnect();
            return false;
        } else {
            // Error or timeout
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                return false;  // Timeout - don't disconnect
            }
            disconnect();
            return false;
        }
    }
    
private:
    SOCKET sock_;
};