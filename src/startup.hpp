#pragma once

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <string>
#include <filesystem>
#include <optional>

// Manages adding/removing the application shortcut to the Windows startup folder
class StartupManager {
    private:
        std::optional<bool> cachedIsInStartup_;

    protected:
        std::wstring shortcutName_;  // Name of the shortcut in the startup folder

    public:

    StartupManager(const std::wstring& shortcutName) : shortcutName_(shortcutName) {
        cachedIsInStartup_ = IsInStartup();
    }

    // Add the current executable to Windows startup
    // startMinimized: if true, the application will start minimized when launched from startup
    inline bool AddToStartup(bool startMinimized = false) {
        if (cachedIsInStartup_.has_value() && cachedIsInStartup_.value()) {
            return true; // Already in startup
        }

        HRESULT hr = CoInitialize(nullptr);
        bool comInitialized = SUCCEEDED(hr);
        
        // Get path to startup folder
        wchar_t startupPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupPath))) {
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        // Get current executable path
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        std::wstring shortcutPath = std::wstring(startupPath) + L"\\" + shortcutName_ + L".lnk";
        
        // Create shortcut
        IShellLinkW* pShellLink = nullptr;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, 
                            IID_IShellLinkW, (void**)&pShellLink);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        pShellLink->SetPath(exePath);
        pShellLink->SetDescription(shortcutName_.c_str());
        
        // Set window show command (normal or minimized)
        if (startMinimized) {
            pShellLink->SetShowCmd(SW_SHOWMINIMIZED);
        } else {
            pShellLink->SetShowCmd(SW_SHOWNORMAL);
        }
        
        // Get working directory (executable's directory)
        std::wstring workingDir = exePath;
        size_t pos = workingDir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            workingDir = workingDir.substr(0, pos);
            pShellLink->SetWorkingDirectory(workingDir.c_str());
        }
        
        IPersistFile* pPersistFile = nullptr;
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        
        bool success = false;
        if (SUCCEEDED(hr)) {
            success = SUCCEEDED(pPersistFile->Save(shortcutPath.c_str(), TRUE));
            pPersistFile->Release();
        }
        
        pShellLink->Release();
        if (comInitialized) CoUninitialize();

        if (success) {
            cachedIsInStartup_ = true;
        }
        
        return success;
    }

    // Update an existing startup shortcut to change the minimized setting
    // Returns false if shortcut doesn't exist or update failed
    inline bool UpdateStartupSettings(bool startMinimized) {
        if (!IsInStartup()) {
            return false; // Shortcut doesn't exist
        }

        HRESULT hr = CoInitialize(nullptr);
        bool comInitialized = SUCCEEDED(hr);
        
        // Get path to startup folder
        wchar_t startupPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupPath))) {
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        std::wstring shortcutPath = std::wstring(startupPath) + L"\\" + shortcutName_ + L".lnk";
        
        // Create shell link interface
        IShellLinkW* pShellLink = nullptr;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, 
                            IID_IShellLinkW, (void**)&pShellLink);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        // Load existing shortcut
        IPersistFile* pPersistFile = nullptr;
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        if (FAILED(hr)) {
            pShellLink->Release();
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        hr = pPersistFile->Load(shortcutPath.c_str(), STGM_READWRITE);
        if (FAILED(hr)) {
            pPersistFile->Release();
            pShellLink->Release();
            if (comInitialized) CoUninitialize();
            return false;
        }
        
        // Update the show command
        if (startMinimized) {
            pShellLink->SetShowCmd(SW_SHOWMINNOACTIVE);
        } else {
            pShellLink->SetShowCmd(SW_SHOWNORMAL);
        }
        
        // Save the updated shortcut
        bool success = SUCCEEDED(pPersistFile->Save(shortcutPath.c_str(), TRUE));
        
        pPersistFile->Release();
        pShellLink->Release();
        if (comInitialized) CoUninitialize();
        
        return success;
    }

    // Remove the shortcut from Windows startup
    inline bool RemoveFromStartup() {
        if (cachedIsInStartup_.has_value() && !cachedIsInStartup_.value()) {
            return true; // Already not in startup
        }
        // Get path to startup folder
        wchar_t startupPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupPath))) {
            return false;
        }
        
        std::wstring shortcutPath = std::wstring(startupPath) + L"\\" + shortcutName_ + L".lnk";
        
        std::error_code ec;
        bool removed = std::filesystem::remove(shortcutPath, ec);
        if (removed) {
            cachedIsInStartup_ = false;
        }
        return removed;
    }

    // Check if shortcut exists in startup folder
    inline bool IsInStartup(bool refreshCache = false) {
        if (!refreshCache && cachedIsInStartup_.has_value()) {
            return cachedIsInStartup_.value();
        }
        wchar_t startupPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupPath))) {
            return false;
        }
        
        std::wstring shortcutPath = std::wstring(startupPath) + L"\\" + shortcutName_ + L".lnk";
        bool exists = std::filesystem::exists(shortcutPath);
        cachedIsInStartup_ = exists;
        return exists;
    }

    // Get the current show command setting from the startup shortcut
    // Returns std::nullopt if shortcut doesn't exist or couldn't be read
    // Otherwise returns the SW_ constant (e.g., SW_SHOWNORMAL, SW_SHOWMINNOACTIVE)
    inline std::optional<int> GetStartupShowCommand() {
        if (!IsInStartup()) {
            return std::nullopt; // Shortcut doesn't exist
        }

        HRESULT hr = CoInitialize(nullptr);
        bool comInitialized = SUCCEEDED(hr);
        
        // Get path to startup folder
        wchar_t startupPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupPath))) {
            if (comInitialized) CoUninitialize();
            return std::nullopt;
        }
        
        std::wstring shortcutPath = std::wstring(startupPath) + L"\\" + shortcutName_ + L".lnk";
        
        // Create shell link interface
        IShellLinkW* pShellLink = nullptr;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, 
                            IID_IShellLinkW, (void**)&pShellLink);
        if (FAILED(hr)) {
            if (comInitialized) CoUninitialize();
            return std::nullopt;
        }
        
        // Load existing shortcut
        IPersistFile* pPersistFile = nullptr;
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        if (FAILED(hr)) {
            pShellLink->Release();
            if (comInitialized) CoUninitialize();
            return std::nullopt;
        }
        
        hr = pPersistFile->Load(shortcutPath.c_str(), STGM_READ);
        if (FAILED(hr)) {
            pPersistFile->Release();
            pShellLink->Release();
            if (comInitialized) CoUninitialize();
            return std::nullopt;
        }
        
        // Get the show command
        int showCmd = 0;
        hr = pShellLink->GetShowCmd(&showCmd);
        
        pPersistFile->Release();
        pShellLink->Release();
        if (comInitialized) CoUninitialize();
        
        if (FAILED(hr)) {
            return std::nullopt;
        }
        
        return showCmd;
    }

    // Helper function to check if the startup shortcut is set to start minimized
    inline bool IsStartupMinimized() {
        auto showCmd = GetStartupShowCommand();
        if (!showCmd.has_value()) {
            return false;
        }
        return showCmd.value() == SW_SHOWMINNOACTIVE;
    }
};
