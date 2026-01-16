#ifndef SYSTEM_STATS_H
#define SYSTEM_STATS_H

#include <cstdint>
#include <string>
#include <iostream>
#include <chrono>

#include <windows.h>
#include <pdh.h>

#include "ICPUEx.h"
#include "IPlatform.h"
#include "IDeviceManager.h"

#include "log.hpp"


typedef IPlatform& (__stdcall* GetPlatformFunc)();

// System monitoring
struct SystemStats {
    float cpuPercent = 0;
    float memPercent = 0;
    uint64_t memUsedMB = 0;
    uint64_t memTotalMB = 0;
    float cpuTempC = 0;
};

class SystemMonitor {
public:
    SystemMonitor() {
        // PDH setup
        PdhOpenQuery(nullptr, 0, &cpuQuery_);
        PdhAddEnglishCounterA(cpuQuery_, "\\Processor(_Total)\\% Processor Time", 0, &cpuCounter_);
        PdhCollectQueryData(cpuQuery_);
        lastCollectTime_ = std::chrono::steady_clock::now();
        
        // Ryzen Master SDK setup
        initRyzenSDK();

        LOG_INFO << "SystemMonitor initialized successfully\n";
    }
    
    ~SystemMonitor() {
        PdhCloseQuery(cpuQuery_);
        if (hPlatformDLL_) FreeLibrary(hPlatformDLL_);
    }
    
    SystemStats getStats() {
        SystemStats stats;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCollectTime_).count();
        
        if (elapsed >= 1000) {
            PdhCollectQueryData(cpuQuery_);
            
            PDH_FMT_COUNTERVALUE value;
            PdhGetFormattedCounterValue(cpuCounter_, PDH_FMT_DOUBLE, nullptr, &value);
            cachedCpuPercent_ = static_cast<float>(value.doubleValue);
            
            // Get CPU temperature from Ryzen SDK
            if (cpuInterface_) {
                CPUParameters cpuData;
                if (cpuInterface_->GetCPUParameters(cpuData) == 0) {
                    const double* coreTemps = cpuData.stFreqData.dCurrentTemp;
                    float maxTemp = 0;
                    for (unsigned int i = 0; i < cpuData.stFreqData.uLength && nullptr != cpuData.stFreqData.dCurrentTemp; i++) {
                        if (coreTemps[i] > maxTemp) {
                            maxTemp = static_cast<float>(coreTemps[i]);
                        }
                    }
                    cachedCpuTemp_ = maxTemp;
                } else {
                    LOG_ERROR << "Failed to get CPU parameters from Ryzen SDK\n";
                }
            }
            // LOG_INFO << "Updated CPU stats: " << cachedCpuPercent_ << "%, " << cachedCpuTemp_ << "C\n";
            
            lastCollectTime_ = now;
        }
        
        stats.cpuPercent = cachedCpuPercent_;
        stats.cpuTempC = cachedCpuTemp_;
        
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(memInfo);
        GlobalMemoryStatusEx(&memInfo);
        stats.memPercent = static_cast<float>(memInfo.dwMemoryLoad);
        stats.memTotalMB = memInfo.ullTotalPhys / (1024 * 1024);
        stats.memUsedMB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
        
        return stats;
    }

private:
    void initRyzenSDK() {
        // Get SDK installation path from registry
        std::wstring installPath;
        LPCWSTR path = {};
        DWORD temp = 0;
        if (!g_GetRegistryValue(HKEY_LOCAL_MACHINE, 
                              L"Software\\AMD\\RyzenMasterMonitoringSDK", 
                              L"InstallationPath", installPath, temp)) {
            LOG_ERROR << "Unexpected Error E1001. Please reinstall AMDRyzenMasterMonitoringSDK\n";
            return; // SDK not installed
        } else {
            LOG_INFO << "Found Ryzen Master SDK at: " << installPath << "\n";
        }
        
        std::wstring binPath = installPath + L"bin";
        SetDllDirectoryW(binPath.c_str());

        std::wstring dllPath = installPath + L"bin\\Platform.dll";
        path = dllPath.c_str();
        hPlatformDLL_ = LoadLibraryEx(path, NULL, 0);
        if (!hPlatformDLL_) {
            DWORD error = GetLastError();
            LOG_ERROR << "LoadLibrary failed with error code: " << error << "\n";
            LOG_ERROR << "Failed to load Platform.dll from: " << path << "\n";
            LOG_ERROR << "Unexpected Error E1004. Please reinstall AMDRyzenMasterMonitoringSDK\n";
            return;
        }

        SetDllDirectoryW(NULL); 
        
        GetPlatformFunc getPlatform = (GetPlatformFunc)GetProcAddress(hPlatformDLL_, "GetPlatform");
        if (!getPlatform) {
            LOG_ERROR << "Platform not found\n";
            return;
        }
        
        IPlatform& platform = getPlatform();
        if (!platform.Init()) {
            LOG_ERROR << "Platform init failed\n";
            return;
        }
        
        IDeviceManager& deviceMgr = platform.GetIDeviceManager();
        cpuInterface_ = (ICPUEx*)deviceMgr.GetDevice(dtCPU, 0);

        const auto name = (PWCHAR)cpuInterface_->GetName();
        LOG_INFO << "Using CPU: " << name << "\n";

        cpuInterface_->GetCorePark(uCorePark);
    }
    
    bool g_GetRegistryValue(HKEY hRootKey, LPCWSTR keyPath, const wchar_t* valueName, std::wstring& ulValue, bool bIsDWORD)
    {
        if (!valueName || (wcslen(valueName) == 0)) return false;
        HKEY hKey = NULL;
        const DWORD MAX_STRING_LEN = 256;
        DWORD dwLength = MAX_STRING_LEN;

        HRESULT hr = RegOpenKey(hRootKey, keyPath, &hKey);
        if (hr != ERROR_SUCCESS) return false;
        wchar_t buff[MAX_STRING_LEN] = { '\n' };

        if (bIsDWORD)
        {
            dwLength = sizeof(DWORD);
            DWORD dwValue = 0;
            hr = RegQueryValueEx(hKey, valueName, 0, NULL, reinterpret_cast<LPBYTE>(&dwValue), &dwLength);
            ulValue = std::to_wstring(dwValue);
        }
        else
        {
            hr = RegQueryValueEx(hKey, valueName, 0, NULL, reinterpret_cast<LPBYTE>(&buff), &dwLength);
            ulValue = std::wstring(buff);
        }
        RegCloseKey(hKey);

        return hr == ERROR_SUCCESS;
    }

    PDH_HQUERY cpuQuery_;
    PDH_HCOUNTER cpuCounter_;
    std::chrono::steady_clock::time_point lastCollectTime_;
    float cachedCpuPercent_ = 0;
    float cachedCpuTemp_ = 0;
    unsigned int uCorePark = 0;
    
    HMODULE hPlatformDLL_ = nullptr;
    ICPUEx* cpuInterface_ = nullptr;
};

#endif // SYSTEM_STATS_H