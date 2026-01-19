#pragma once

#include <winsock2.h>
#include <atomic>

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
        cancelConnect_ = false;
        
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) return false;
        
        // Set non-blocking mode for connect
        u_long mode = 1;
        ioctlsocket(sock_, FIONBIO, &mode);
        
        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(tcp_port);
        
        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
        
        // Start non-blocking connect
        int result = ::connect(sock_, (sockaddr*)&serverAddr, sizeof(serverAddr));
        if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
        
        // Poll for connection with small intervals so we can check cancel flag
        const int pollIntervalMs = 100;
        const int totalTimeoutMs = 10000;
        int elapsed = 0;
        
        while (elapsed < totalTimeoutMs) {
            if (cancelConnect_) {
                closesocket(sock_);
                sock_ = INVALID_SOCKET;
                return false;
            }
            
            fd_set writeSet, errorSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(sock_, &writeSet);
            FD_SET(sock_, &errorSet);
            
            timeval tv = { 0, pollIntervalMs * 1000 };
            result = select(0, nullptr, &writeSet, &errorSet, &tv);
            
            if (result > 0) {
                if (FD_ISSET(sock_, &errorSet)) {
                    // Connection failed
                    closesocket(sock_);
                    sock_ = INVALID_SOCKET;
                    return false;
                }
                if (FD_ISSET(sock_, &writeSet)) {
                    // Connected! Set back to blocking mode
                    mode = 0;
                    ioctlsocket(sock_, FIONBIO, &mode);
                    
                    // Set socket options
                    int flag = 1;
                    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
                    int bufSize = 256 * 1024;
                    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize));
                    
                    return true;
                }
            }
            
            elapsed += pollIntervalMs;
        }
        
        // Timeout
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    void cancelConnection() {
        cancelConnect_ = true;
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
    std::atomic<bool> cancelConnect_{false};
};