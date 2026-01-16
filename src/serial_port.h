
#include <cstdint>
#include <string>
#include <iostream>
#include <vector>
#include <chrono>

#include "log.hpp"
#include "image.hpp"
#include <windows.h>

// Protocol constants
constexpr uint8_t SYNC_MARKER[] = {0xAA, 0x55, 0xAA, 0x55};
constexpr uint8_t CMD_FRAME_FULL = 0x01;
constexpr uint8_t CMD_PING = 0x02;
constexpr uint8_t CMD_FRAME_PARTIAL = 0x03;

constexpr int DISPLAY_WIDTH = 240;
constexpr int DISPLAY_HEIGHT = 960;

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort() { close(); }
    
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;
    
    SerialPort(SerialPort&& other) noexcept {
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    
    bool open(const std::string& port, int baudrate = 115200) {
        std::string portName = port;
        if (port.find("\\\\.\\") != 0 && port.find("COM") == 0) {
            portName = "\\\\.\\" + port;
        }
        
        handle_ = CreateFileA(
            portName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        
        if (handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle_, &dcb)) {
            close();
            return false;
        }
        
        dcb.BaudRate = baudrate;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        
        if (!SetCommState(handle_, &dcb)) {
            close();
            return false;
        }
        
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = 100;
        timeouts.ReadTotalTimeoutConstant = 10000;  // 10 seconds
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 10000;
        timeouts.WriteTotalTimeoutMultiplier = 10;
        SetCommTimeouts(handle_, &timeouts);
        
        // Increase buffer sizes for large transfers
        SetupComm(handle_, 65536, 65536);
        
        return true;
    }
    
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }
    
    bool isOpen() const {
        return handle_ != INVALID_HANDLE_VALUE;
    }
    
    size_t write(const void* data, size_t size) {
        DWORD written = 0;
        WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr);
        return written;
    }
    
    // Write all data, blocking until complete
    bool writeAll(const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        size_t remaining = size;
        while (remaining > 0) {
            size_t n = write(ptr, remaining);
            if (n == 0) return false;
            ptr += n;
            remaining -= n;
        }
        return true;
    }
    
    size_t read(void* buffer, size_t size) {
        DWORD bytesRead = 0;
        ReadFile(handle_, buffer, static_cast<DWORD>(size), &bytesRead, nullptr);
        return bytesRead;
    }
    
    size_t bytesAvailable() {
        COMSTAT comStat;
        DWORD errors;
        if (ClearCommError(handle_, &errors, &comStat)) {
            return comStat.cbInQue;
        }
        return 0;
    }
    
    void flush() {
        FlushFileBuffers(handle_);
    }
    
    void purge() {
        // Clear both input and output buffers
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }
    
    // Poll for exact number of bytes with timeout (in milliseconds)
    bool readExactPolling(void* buffer, size_t size, DWORD timeoutMs = 5000) {
        uint8_t* ptr = static_cast<uint8_t*>(buffer);
        size_t remaining = size;
        DWORD startTime = GetTickCount();
        
        while (remaining > 0) {
            DWORD elapsed = GetTickCount() - startTime;
            if (elapsed > timeoutMs) {
                return false;  // Timeout
            }
            
            size_t available = bytesAvailable();
            if (available > 0) {
                size_t toRead = (available < remaining) ? available : remaining;
                size_t n = read(ptr, toRead);
                if (n > 0) {
                    ptr += n;
                    remaining -= n;
                }
            } else {
                Sleep(1);  // Small sleep to avoid busy-waiting
            }
        }
        return true;
    }
    
    bool readExact(void* buffer, size_t size) {
        return readExactPolling(buffer, size, 5000);
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class SerialDisplay {
public:
    SerialDisplay() = default;
    
    bool connect(const std::string& port) {
        if (!serial_.open(port)) {
            return false;
        }
        
        // Give device time to initialize
        Sleep(100);
        
        if (!ping()) {
            serial_.close();
            return false;
        }
        
        return true;
    }
    
    void disconnect() {
        serial_.close();
    }
    
    bool isConnected() const {
        return serial_.isOpen();
    }
    
    bool ping() {
        uint8_t packet[5];
        memcpy(packet, SYNC_MARKER, 4);
        packet[4] = CMD_PING;
        
        serial_.write(packet, sizeof(packet));
        serial_.flush();
        
        char response[2] = {};
        if (!serial_.readExact(response, 2)) {
            LOG_WARN << "Ping: No response\n";
            return false;
        }
        
        if (response[0] != 'O' || response[1] != 'K') {
            LOG_WARN << "Ping: Unexpected response: " 
                      << (int)response[0] << " " << (int)response[1] << "\n";
            return false;
        }
        
        return true;
    }
    
    // Send full frame - fastest method
    // Image MUST be DISPLAY_WIDTH x DISPLAY_HEIGHT
    bool sendFrameFull(const qualia::Image& image) {

        auto startTime = std::chrono::high_resolution_clock::now();

        if (!serial_.isOpen()) {
            LOG_WARN << "Error: Serial port not open\n";
            return false;
        }
        if (image.width != DISPLAY_WIDTH || image.height != DISPLAY_HEIGHT) {
            LOG_WARN << "Error: Image size mismatch. Expected " 
                      << DISPLAY_WIDTH << "x" << DISPLAY_HEIGHT 
                      << ", got " << image.width << "x" << image.height << "\n";
            return false;
        }
        
        // Send header
        uint8_t header[5];
        memcpy(header, SYNC_MARKER, 4);
        header[4] = CMD_FRAME_FULL;
        
        if (serial_.write(header, sizeof(header)) != sizeof(header)) {
            LOG_WARN << "Error: Failed to write header\n";
            return false;
        }
        serial_.flush();

        auto headerEndTime = std::chrono::high_resolution_clock::now();
        
        // Poll for RD response
        char response[2] = {};
        if (!serial_.readExact(response, 2)) {
            LOG_WARN << "Error: Timeout waiting for RD response\n";
            return false;
        }
        if (response[0] != 'R' || response[1] != 'D') {
            LOG_WARN << "Error: Unexpected response (expected RD): " 
                      << (int)(unsigned char)response[0] << " " 
                      << (int)(unsigned char)response[1] << "\n";
            return false;
        }

        auto rdEndTime = std::chrono::high_resolution_clock::now();
        
        // Send pixel data directly
        if (!serial_.writeAll(image.data(), image.dataSize())) {
            LOG_WARN << "Error: Failed to write pixel data\n";
            return false;
        }
        serial_.flush();

        auto dataEndTime = std::chrono::high_resolution_clock::now();
        
        // Poll for OK response (longer timeout for large data processing)
        if (!serial_.readExactPolling(response, 2, 10000)) {
            LOG_WARN << "Error: Timeout waiting for OK response\n";
            return false;
        }
        
        if (response[0] != 'O' || response[1] != 'K') {
            LOG_WARN << "Error: Frame not acknowledged (expected OK): "
                      << (int)(unsigned char)response[0] << " " 
                      << (int)(unsigned char)response[1] << "\n";
            return false;
        }

        auto endTime = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> headerDuration = headerEndTime - startTime;
        std::chrono::duration<double, std::milli> rdDuration = rdEndTime - headerEndTime;
        std::chrono::duration<double, std::milli> dataDuration = dataEndTime - rdEndTime;
        std::chrono::duration<double, std::milli> okDuration = endTime - dataEndTime;

        LOG_INFO << "Frame sent successfully. Timings (ms): "
                  << "Header: " << headerDuration.count() << ", "
                  << "RD Wait: " << rdDuration.count() << ", "
                  << "Data Send: " << dataDuration.count() << ", "
                  << "OK Wait: " << okDuration.count() << "\n";
        
        return true;
    }
    
    // Send partial frame - slower but allows updating regions
    bool sendFramePartial(const qualia::Image& image, uint16_t x = 0, uint16_t y = 0) {
        if (!serial_.isOpen()) return false;
        
        // Build header
        uint8_t header[13];
        memcpy(header, SYNC_MARKER, 4);
        header[4] = CMD_FRAME_PARTIAL;
        
        // Little-endian width, height, x, y
        header[5] = image.width & 0xFF;
        header[6] = (image.width >> 8) & 0xFF;
        header[7] = image.height & 0xFF;
        header[8] = (image.height >> 8) & 0xFF;
        header[9] = x & 0xFF;
        header[10] = (x >> 8) & 0xFF;
        header[11] = y & 0xFF;
        header[12] = (y >> 8) & 0xFF;
        
        serial_.write(header, sizeof(header));
        
        // Wait for ready
        char response[2] = {};
        if (!serial_.readExact(response, 2)) {
            return false;
        }
        if (response[0] != 'R' || response[1] != 'D') {
            return false;
        }
        
        // Send pixel data
        if (!serial_.writeAll(image.data(), image.dataSize())) {
            return false;
        }
        
        // Wait for OK
        if (!serial_.readExact(response, 2)) {
            return false;
        }
        
        return response[0] == 'O' && response[1] == 'K';
    }
    
    // Auto-select method based on size
    bool sendFrame(const qualia::Image& image, uint16_t x = 0, uint16_t y = 0) {
        if (image.width == DISPLAY_WIDTH && image.height == DISPLAY_HEIGHT && x == 0 && y == 0) {
            return sendFrameFull(image);
        } else {
            return sendFramePartial(image, x, y);
        }
    }

private:
     mutable SerialPort serial_;
};