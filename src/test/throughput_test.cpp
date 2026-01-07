// throughput_test.cpp
// Blast data to serial port to test raw USB throughput
//
// Build: g++ -o throughput_test.exe throughput_test.cpp -std=c++17

#include <windows.h>
#include <iostream>
#include <chrono>
#include <cstring>

// Overlapped I/O version - pipeline multiple writes
void testOverlapped(HANDLE handle) {
    const size_t CHUNK_SIZE = 16384;  // Larger chunks
    const int NUM_BUFFERS = 4;  // Number of overlapped operations in flight
    
    char* buffers[NUM_BUFFERS];
    OVERLAPPED overlapped[NUM_BUFFERS] = {};
    bool pending[NUM_BUFFERS] = {};
    
    for (int i = 0; i < NUM_BUFFERS; i++) {
        buffers[i] = new char[CHUNK_SIZE];
        memset(buffers[i], 0xAA, CHUNK_SIZE);
        overlapped[i].hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    }
    
    std::cout << "Testing with overlapped I/O (" << NUM_BUFFERS << " buffers, " 
              << CHUNK_SIZE << " bytes each)...\n";
    
    size_t totalBytes = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastReport = startTime;
    int currentBuffer = 0;
    
    while (true) {
        // Wait for previous operation on this buffer to complete
        if (pending[currentBuffer]) {
            DWORD written = 0;
            if (!GetOverlappedResult(handle, &overlapped[currentBuffer], &written, TRUE)) {
                std::cerr << "GetOverlappedResult error: " << GetLastError() << "\n";
                break;
            }
            totalBytes += written;
            pending[currentBuffer] = false;
        }
        
        // Start new write
        ResetEvent(overlapped[currentBuffer].hEvent);
        DWORD written = 0;
        BOOL result = WriteFile(handle, buffers[currentBuffer], CHUNK_SIZE, 
                                &written, &overlapped[currentBuffer]);
        
        if (!result) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                pending[currentBuffer] = true;
            } else {
                std::cerr << "WriteFile error: " << err << "\n";
                break;
            }
        } else {
            totalBytes += written;
        }
        
        currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
        
        // Report
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count();
        
        if (elapsed >= 1000) {
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            float kbps = (totalBytes / 1024.0f) / (totalElapsed / 1000.0f);
            std::cout << "Sent " << totalBytes / 1024 << " KB | " << kbps << " KB/s\n";
            lastReport = now;
        }
    }
    
    for (int i = 0; i < NUM_BUFFERS; i++) {
        CloseHandle(overlapped[i].hEvent);
        delete[] buffers[i];
    }
}

// Synchronous version
void testSynchronous(HANDLE handle) {
    const size_t CHUNK_SIZE = 4096;
    char buffer[CHUNK_SIZE];
    memset(buffer, 0xAA, CHUNK_SIZE);
    
    std::cout << "Testing with synchronous I/O (" << CHUNK_SIZE << " byte chunks)...\n";
    
    size_t totalBytes = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastReport = startTime;
    
    while (true) {
        DWORD written = 0;
        if (!WriteFile(handle, buffer, CHUNK_SIZE, &written, nullptr)) {
            std::cerr << "Write error: " << GetLastError() << "\n";
            break;
        }
        totalBytes += written;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count();
        
        if (elapsed >= 1000) {
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            float kbps = (totalBytes / 1024.0f) / (totalElapsed / 1000.0f);
            std::cout << "Sent " << totalBytes / 1024 << " KB | " << kbps << " KB/s\n";
            lastReport = now;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " COMx [sync|async]\n";
        std::cout << "Example: " << argv[0] << " COM5 async\n";
        return 1;
    }
    
    bool useOverlapped = (argc >= 3 && std::string(argv[2]) == "async");
    
    std::string portName = std::string("\\\\.\\") + argv[1];
    
    HANDLE handle = CreateFileA(
        portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        useOverlapped ? FILE_FLAG_OVERLAPPED : 0,
        nullptr
    );
    
    if (handle == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open " << argv[1] << " (error " << GetLastError() << ")\n";
        return 1;
    }
    
    // Configure serial
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(handle, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    SetCommState(handle, &dcb);
    
    // Increase buffers
    SetupComm(handle, 65536, 65536);
    
    // Clear any pending data
    PurgeComm(handle, PURGE_TXCLEAR | PURGE_RXCLEAR);
    
    std::cout << "Blasting data to " << argv[1] << "...\n";
    std::cout << "Press Ctrl+C to stop\n\n";
    
    if (useOverlapped) {
        testOverlapped(handle);
    } else {
        testSynchronous(handle);
    }
    
    CloseHandle(handle);
    return 0;
}