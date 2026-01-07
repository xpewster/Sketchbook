# Makefile for Qualia Display Monitor (Windows MSVC)
#
# Usage:
#   Open "Developer Command Prompt for VS" or "x64 Native Tools Command Prompt"
#   Run: nmake
#
# Or with Visual Studio Build Tools:
#   nmake /f Makefile

CC = cl
CFLAGS = /EHsc /O2 /std:c++17 /W3 /DWIN32 /D_WINDOWS
LDFLAGS = /link pdh.lib ws2_32.lib

TARGET = wifi_throughput_test.exe
SOURCES = src/wifi_throughput_test.cpp
HEADERS = src/qualia_display.hpp

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) /Fe:$(TARGET) $(LDFLAGS)
clean:
	del /Q $(TARGET) *.obj 2>nul

.PHONY: all clean