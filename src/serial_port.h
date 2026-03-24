#pragma once

#include <string>
#include <atomic>
#include <thread>

#include "log.hpp"
#include <windows.h>

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort() { close(); }

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

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
            closeHandle();
            return false;
        }

        dcb.BaudRate = baudrate;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;

        if (!SetCommState(handle_, &dcb)) {
            closeHandle();
            return false;
        }

        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 100;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        SetCommTimeouts(handle_, &timeouts);

        return true;
    }

    void close() {
        stopMonitor();
        closeHandle();
    }

    bool isOpen() const {
        return handle_ != INVALID_HANDLE_VALUE;
    }

private:
    void closeHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    bool isHandleValid() {
        DWORD errors;
        return ClearCommError(handle_, &errors, nullptr) != 0;
    }

public:

    // Reset the ESP32 by sending a magic byte over CDC.
    bool resetDevice(const std::string& port) {
        bool wasOpen = isOpen();

        if (!wasOpen && !open(port)) {
            LOG_WARN << "Reset: failed to open " << port << "\n";
            return false;
        }

        uint8_t cmd = 0xFF;
        DWORD written = 0;
        WriteFile(handle_, &cmd, 1, &written, nullptr);
        FlushFileBuffers(handle_);

        // Port will go stale after board resets — close it so monitor can reconnect
        closeHandle();

        LOG_INFO << "Reset signal sent\n";
        return written == 1;
    }

    // Start a background thread that reads and logs serial output to logs/YYYY-MM-DD_esp.log.
    // Automatically reconnects if the board resets or disconnects.
    void startMonitor(const std::string& port) {
        if (monitoring_.load()) return;

        monitoring_ = true;
        monitorThread_ = std::thread([this, port]() {
            auto& espLog = Logger::getInstance("esp");
            char buf[512];
            std::string line;

            while (monitoring_.load()) {
                // Ensure connection
                if (!isOpen()) {
                    if (open(port)) {
                        LOG_INFO << "Serial monitor connected on " << port << "\n";
                    } else {
                        Sleep(500);
                        continue;
                    }
                }

                DWORD bytesRead = 0;
                BOOL ok = ReadFile(handle_, buf, sizeof(buf), &bytesRead, nullptr);

                if (!ok || (bytesRead == 0 && !isHandleValid())) {
                    LOG_WARN << "Serial disconnected, waiting for reconnect...\n";
                    closeHandle();
                    line.clear();
                    continue;
                }

                for (DWORD i = 0; i < bytesRead; i++) {
                    if (buf[i] == '\n') {
                        espLog.write(Level::INFO, line);
                        line.clear();
                    } else if (buf[i] != '\r') {
                        line += buf[i];
                    }
                }
            }
        });
    }

    void stopMonitor() {
        monitoring_ = false;
        if (monitorThread_.joinable()) {
            monitorThread_.join();
        }
    }

    bool isMonitoring() const {
        return monitoring_.load();
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> monitoring_{false};
    std::thread monitorThread_;
};