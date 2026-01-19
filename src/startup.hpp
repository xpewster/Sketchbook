#pragma once

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <taskschd.h>
#include <comdef.h>
#include <string>
#include <optional>

#include <shlobj.h>
#include <objbase.h>
#include <filesystem>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")
#pragma comment(lib, "secur32.lib")

// Manages adding/removing the application from Windows startup via Task Scheduler
// This approach supports UAC elevation (runs with highest privileges)
class StartupManager {
public:
    static constexpr wchar_t STARTUP_ARG[] = L"--from-startup";

    // Check if the program was launched from startup by looking for the specific command line argument
    static bool WasLaunchedFromStartup() {
        return wcsstr(GetCommandLineW(), STARTUP_ARG) != nullptr;
    }

private:
    static constexpr wchar_t TASK_FOLDER[] = L"\\Sketchbook";

    std::optional<bool> cachedIsInStartup_;
    std::wstring taskName_;

    // Helper to get current executable path
    static std::wstring GetExecutablePath() {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
            return L"";
        }
        return exePath;
    }

    // Helper to get working directory from exe path
    static std::wstring GetWorkingDirectory(const std::wstring& exePath) {
        size_t pos = exePath.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            return exePath.substr(0, pos);
        }
        return L"";
    }

    // Helper to get current username in DOMAIN\User format
    static std::wstring GetCurrentUsername() {
        wchar_t username[256];
        DWORD size = 256;
        if (GetUserNameW(username, &size)) {
            // Try to get domain\user format
            wchar_t fullname[512];
            DWORD fullsize = 512;
            if (GetUserNameExW(NameSamCompatible, fullname, &fullsize)) {
                return fullname;
            }
            return username;
        }
        return L"";
    }

    // Helper to get or create the task folder
    static ITaskFolder* GetOrCreateTaskFolder(ITaskService* pService) {
        ITaskFolder* pRootFolder = nullptr;
        ITaskFolder* pFolder = nullptr;

        HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
        if (FAILED(hr)) return nullptr;

        // Try to get existing folder
        hr = pService->GetFolder(_bstr_t(TASK_FOLDER), &pFolder);
        if (SUCCEEDED(hr)) {
            pRootFolder->Release();
            return pFolder;
        }

        // Create the folder
        hr = pRootFolder->CreateFolder(_bstr_t(TASK_FOLDER), _variant_t(), &pFolder);
        pRootFolder->Release();

        if (FAILED(hr)) return nullptr;
        return pFolder;
    }

public:
    StartupManager(const std::wstring& taskName) : taskName_(taskName) {
        cachedIsInStartup_ = IsInStartup();
    }

    // Add the current executable to Windows startup via Task Scheduler
    // Creates a task that runs at user logon with highest privileges (admin)
    inline bool AddToStartup() {
        if (cachedIsInStartup_.has_value() && cachedIsInStartup_.value()) {
            return true;
        }

        std::wstring exePath = GetExecutablePath();
        if (exePath.empty()) {
            return false;
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

        bool success = false;
        ITaskService* pService = nullptr;
        ITaskFolder* pFolder = nullptr;
        ITaskDefinition* pTask = nullptr;
        IRegistrationInfo* pRegInfo = nullptr;
        IPrincipal* pPrincipal = nullptr;
        ITaskSettings* pSettings = nullptr;
        ITriggerCollection* pTriggerCollection = nullptr;
        ITrigger* pTrigger = nullptr;
        ILogonTrigger* pLogonTrigger = nullptr;
        IActionCollection* pActionCollection = nullptr;
        IAction* pAction = nullptr;
        IExecAction* pExecAction = nullptr;
        IRegisteredTask* pRegisteredTask = nullptr;

        // Create Task Service
        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITaskService, (void**)&pService);
        if (FAILED(hr)) goto cleanup;

        // Connect to task service
        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) goto cleanup;

        // Get or create folder
        pFolder = GetOrCreateTaskFolder(pService);
        if (!pFolder) goto cleanup;

        // Delete existing task if present (to update it)
        pFolder->DeleteTask(_bstr_t(taskName_.c_str()), 0);

        // Create new task definition
        hr = pService->NewTask(0, &pTask);
        if (FAILED(hr)) goto cleanup;

        // Set registration info
        hr = pTask->get_RegistrationInfo(&pRegInfo);
        if (SUCCEEDED(hr)) {
            pRegInfo->put_Description(_bstr_t((taskName_ + L" startup task").c_str()));
            pRegInfo->put_Author(_bstr_t(L""));
            pRegInfo->Release();
            pRegInfo = nullptr;
        }

        // Set principal (run with highest privileges)
        hr = pTask->get_Principal(&pPrincipal);
        if (FAILED(hr)) goto cleanup;

        hr = pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        if (FAILED(hr)) goto cleanup;

        hr = pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        if (FAILED(hr)) goto cleanup;

        // Configure settings
        hr = pTask->get_Settings(&pSettings);
        if (SUCCEEDED(hr)) {
            pSettings->put_StartWhenAvailable(VARIANT_TRUE);
            pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // No time limit
            pSettings->put_AllowHardTerminate(VARIANT_FALSE);
            pSettings->Release();
            pSettings = nullptr;
        }

        // Create logon trigger
        hr = pTask->get_Triggers(&pTriggerCollection);
        if (FAILED(hr)) goto cleanup;

        hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
        if (FAILED(hr)) goto cleanup;

        hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
        if (FAILED(hr)) goto cleanup;

        hr = pLogonTrigger->put_Id(_bstr_t(L"LogonTriggerId"));
        if (FAILED(hr)) goto cleanup;

        // Set the user for the trigger - this ensures the task runs in the user's interactive session
        {
            std::wstring currentUser = GetCurrentUsername();
            if (!currentUser.empty()) {
                hr = pLogonTrigger->put_UserId(_bstr_t(currentUser.c_str()));
                if (FAILED(hr)) goto cleanup;
            }
        }

        // Create exec action
        hr = pTask->get_Actions(&pActionCollection);
        if (FAILED(hr)) goto cleanup;

        hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
        if (FAILED(hr)) goto cleanup;

        hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
        if (FAILED(hr)) goto cleanup;

        hr = pExecAction->put_Path(_bstr_t(exePath.c_str()));
        if (FAILED(hr)) goto cleanup;

        hr = pExecAction->put_Arguments(_bstr_t(STARTUP_ARG));
        if (FAILED(hr)) goto cleanup;

        hr = pExecAction->put_WorkingDirectory(_bstr_t(GetWorkingDirectory(exePath).c_str()));
        if (FAILED(hr)) goto cleanup;

        // Register the task
        hr = pFolder->RegisterTaskDefinition(
            _bstr_t(taskName_.c_str()),
            pTask,
            TASK_CREATE_OR_UPDATE,
            _variant_t(),
            _variant_t(),
            TASK_LOGON_INTERACTIVE_TOKEN,
            _variant_t(L""),
            &pRegisteredTask
        );

        success = SUCCEEDED(hr);

    cleanup:
        if (pRegisteredTask) pRegisteredTask->Release();
        if (pExecAction) pExecAction->Release();
        if (pAction) pAction->Release();
        if (pActionCollection) pActionCollection->Release();
        if (pLogonTrigger) pLogonTrigger->Release();
        if (pTrigger) pTrigger->Release();
        if (pTriggerCollection) pTriggerCollection->Release();
        if (pSettings) pSettings->Release();
        if (pPrincipal) pPrincipal->Release();
        if (pRegInfo) pRegInfo->Release();
        if (pTask) pTask->Release();
        if (pFolder) pFolder->Release();
        if (pService) pService->Release();
        if (comInitialized && hr != RPC_E_CHANGED_MODE) CoUninitialize();

        if (success) {
            cachedIsInStartup_ = true;
        }

        return success;
    }

    // Remove the task from Windows Task Scheduler
    inline bool RemoveFromStartup() {
        if (cachedIsInStartup_.has_value() && !cachedIsInStartup_.value()) {
            return true;
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

        bool success = false;
        ITaskService* pService = nullptr;
        ITaskFolder* pFolder = nullptr;

        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITaskService, (void**)&pService);
        if (FAILED(hr)) goto cleanup;

        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) goto cleanup;

        hr = pService->GetFolder(_bstr_t(TASK_FOLDER), &pFolder);
        if (FAILED(hr)) goto cleanup;

        hr = pFolder->DeleteTask(_bstr_t(taskName_.c_str()), 0);
        success = SUCCEEDED(hr);

    cleanup:
        if (pFolder) pFolder->Release();
        if (pService) pService->Release();
        if (comInitialized && hr != RPC_E_CHANGED_MODE) CoUninitialize();

        if (success) {
            cachedIsInStartup_ = false;
        }

        return success;
    }

    // Check if task exists in Task Scheduler
    inline bool IsInStartup(bool refreshCache = false) {
        if (!refreshCache && cachedIsInStartup_.has_value()) {
            return cachedIsInStartup_.value();
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

        bool exists = false;
        ITaskService* pService = nullptr;
        ITaskFolder* pFolder = nullptr;
        IRegisteredTask* pTask = nullptr;

        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITaskService, (void**)&pService);
        if (FAILED(hr)) goto cleanup;

        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) goto cleanup;

        hr = pService->GetFolder(_bstr_t(TASK_FOLDER), &pFolder);
        if (FAILED(hr)) goto cleanup;

        hr = pFolder->GetTask(_bstr_t(taskName_.c_str()), &pTask);
        exists = SUCCEEDED(hr) && pTask != nullptr;

    cleanup:
        if (pTask) pTask->Release();
        if (pFolder) pFolder->Release();
        if (pService) pService->Release();
        if (comInitialized && hr != RPC_E_CHANGED_MODE) CoUninitialize();

        cachedIsInStartup_ = exists;
        return exists;
    }
};

// Manages adding/removing the application shortcut to the Windows startup folder
class LegacyStartupManager {
    private:
        std::optional<bool> cachedIsInStartup_;

    protected:
        std::wstring shortcutName_;  // Name of the shortcut in the startup folder

    public:

    LegacyStartupManager(const std::wstring& shortcutName) : shortcutName_(shortcutName) {
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
