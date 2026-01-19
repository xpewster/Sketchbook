#pragma once
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <mutex>
#include "rc.h"
#include "log.hpp"
#include "tcp.hpp"

#define WM_TRAYICON (WM_USER + 1)
#define WM_TRAY_RETRY (WM_USER + 2)

// Registered message for explorer restart notification
static UINT WM_TASKBARCREATED = 0;


enum TrayMenuID {
    MENU_OPEN = 1,
    MENU_CONNECT = 2,
    MENU_REFRESH_SKIN = 3,
    MENU_RESET_BOARD = 4,
    MENU_CLOSE = 5,
    MENU_SKIN_BASE = 100  // Skin submenu items start at 100
};

class TrayManager {
private:
    NOTIFYICONDATA nid;
    HMENU hMenu;
    HMENU hSkinMenu;
    HWND trayHwnd;          // Hidden window for receiving tray messages
    HWND mainHwnd;          // SFML window handle
    std::thread messageThread;
    
    std::atomic<bool> shouldRestore;
    std::atomic<bool> shouldExit;
    std::atomic<bool> shouldConnect;
    std::atomic<bool> shouldDisconnect;
    std::atomic<bool> shouldRefreshSkin;
    std::atomic<bool> shouldResetBoard;
    std::atomic<int> selectedSkinIndex;  // -1 means no selection
    std::atomic<bool> running;
    
    std::atomic<ConnectionState> connectionState;
    
    std::vector<std::wstring> skinNames;
    std::mutex skinMutex;
    int currentSkinIndex;

    std::atomic<bool> iconAdded;
    int retryCount;
    static constexpr int MAX_RETRY_COUNT = 30;
    static constexpr int RETRY_DELAY_MS = 2000;

    // Window procedure for the hidden tray window
    static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        TrayManager* pThis = reinterpret_cast<TrayManager*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        
        if (pThis) {
            return pThis->HandleMessage(hWnd, uMsg, wParam, lParam);
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    
    LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_TRAYICON:
                switch (lParam) {
                    case WM_LBUTTONDOWN:
                        shouldRestore = true;
                        break;
                        
                    case WM_RBUTTONUP:
                    case WM_CONTEXTMENU: {
                        POINT pt;
                        GetCursorPos(&pt);
                        
                        // Update menu state before showing
                        UpdateMenuState();
                        
                        // Required for menu to work properly
                        SetForegroundWindow(hWnd);
                        
                        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                pt.x, pt.y, 0, hWnd, NULL);
                        
                        HandleMenuCommand(cmd);
                        break;
                    }
                }
                break;
                
            case WM_DESTROY:
                KillTimer(hWnd, 1);
                Shell_NotifyIcon(NIM_DELETE, &nid);
                PostQuitMessage(0);
                break;
            
            case WM_TIMER:
                if (wParam == 1) {
                    // Retry adding tray icon
                    TryAddTrayIcon();
                }
                break;
            
            default:
                // Handle TaskbarCreated message (explorer restart)
                if (uMsg == WM_TASKBARCREATED && WM_TASKBARCREATED != 0) {
                    LOG_INFO << "Taskbar created message received, re-adding tray icon.\n";
                    iconAdded = false;
                    retryCount = 0;
                    TryAddTrayIcon();
                }
                break;
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    
    void HandleMenuCommand(int cmd) {
        if (cmd == MENU_OPEN) {
            shouldRestore = true;
        } else if (cmd == MENU_CONNECT) {
            ConnectionState state = connectionState.load();
            if (state == ConnectionState::Disconnected) {
                shouldConnect = true;
            } else if (state == ConnectionState::Connected) {
                shouldDisconnect = true;
            }
            // Do nothing if Connecting
        } else if (cmd == MENU_REFRESH_SKIN) {
            shouldRefreshSkin = true;
        } else if (cmd == MENU_RESET_BOARD) {
            shouldResetBoard = true;
        } else if (cmd == MENU_CLOSE) {
            shouldExit = true;
        } else if (cmd >= MENU_SKIN_BASE) {
            // Skin selection
            int skinIndex = cmd - MENU_SKIN_BASE;
            std::lock_guard<std::mutex> lock(skinMutex);
            if (skinIndex >= 0 && skinIndex < static_cast<int>(skinNames.size())) {
                selectedSkinIndex = skinIndex;
            }
        }
    }
    
    void UpdateMenuState() {
        // Update Connect/Disconnect item
        ConnectionState state = connectionState.load();
        
        MENUITEMINFO mii = { 0 };
        mii.cbSize = sizeof(MENUITEMINFO);
        mii.fMask = MIIM_STRING | MIIM_STATE;
        
        switch (state) {
            case ConnectionState::Disconnected:
                mii.dwTypeData = const_cast<LPWSTR>(L"Connect");
                mii.fState = MFS_ENABLED;
                break;
            case ConnectionState::Connecting:
                mii.dwTypeData = const_cast<LPWSTR>(L"Connecting...");
                mii.fState = MFS_GRAYED;
                break;
            case ConnectionState::Connected:
                mii.dwTypeData = const_cast<LPWSTR>(L"Disconnect");
                mii.fState = MFS_ENABLED;
                break;
        }
        
        SetMenuItemInfo(hMenu, MENU_CONNECT, FALSE, &mii);
        
        // Update skin submenu checkmarks
        std::lock_guard<std::mutex> lock(skinMutex);
        for (int i = 0; i < static_cast<int>(skinNames.size()); i++) {
            CheckMenuItem(hSkinMenu, MENU_SKIN_BASE + i, 
                         (i == currentSkinIndex) ? MF_CHECKED : MF_UNCHECKED);
        }
    }
    
    void RebuildSkinMenu() {
        // Clear existing items
        while (DeleteMenu(hSkinMenu, 0, MF_BYPOSITION)) {}
        
        std::lock_guard<std::mutex> lock(skinMutex);
        for (int i = 0; i < static_cast<int>(skinNames.size()); i++) {
            UINT flags = MF_STRING;
            if (i == currentSkinIndex) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(hSkinMenu, flags, MENU_SKIN_BASE + i, skinNames[i].c_str());
        }
        
        if (skinNames.empty()) {
            AppendMenuW(hSkinMenu, MF_STRING | MF_GRAYED, 0, L"(No skins)");
        }
    }

    // Attempt to add tray icon, with retry on failure
    void TryAddTrayIcon() {
        if (iconAdded) {
            KillTimer(trayHwnd, 1);
            return;
        }
        
        if (Shell_NotifyIcon(NIM_ADD, &nid)) {
            LOG_INFO << "Tray icon added successfully.\n";
            iconAdded = true;
            KillTimer(trayHwnd, 1);
        } else {
            retryCount++;
            if (retryCount < MAX_RETRY_COUNT) {
                LOG_INFO << "Failed to add tray icon (attempt " << retryCount 
                         << "), will retry in " << RETRY_DELAY_MS << "ms. Error: " << GetLastError() << ".\n";
                SetTimer(trayHwnd, 1, RETRY_DELAY_MS, NULL);
            } else {
                LOG_ERROR << "Failed to add tray icon after " << MAX_RETRY_COUNT 
                          << " attempts, error: " << GetLastError() << ".\n";
                KillTimer(trayHwnd, 1);
            }
        }
    }
    
    // Thread function that runs the Windows message loop
    void MessageLoop() {
        // Create a hidden window class
        HINSTANCE hInstance = GetModuleHandle(NULL);
        
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = TrayWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"SketchbookTrayClass";
        
        if (!RegisterClassEx(&wc)) {
            return;
        }
        
        // Create hidden window for receiving tray messages
        trayHwnd = CreateWindowEx(
            WS_EX_TOOLWINDOW,
            L"SketchbookTrayClass",
            L"Sketchbook Tray",
            WS_POPUP,
            0, 0, 0, 0,
            NULL, NULL, hInstance, NULL
        );
        
        if (!trayHwnd) {
            return;
        }
        
        // Store 'this' pointer so we can access it in the window procedure
        SetWindowLongPtr(trayHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        
        // Initialize tray icon
        ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = trayHwnd;  // Use the hidden window, not the SFML window
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        
        // Try to load custom icon from resources
        nid.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1), 
                                     IMAGE_ICON, 64, 64, LR_SHARED);
        if (!nid.hIcon) {
            LOG_ERROR << "Failed to load tray icon from resources, error: " << GetLastError() << ".\n";
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        }
        
        lstrcpy(nid.szTip, L"Sketchbook");
        
        // Create skin submenu
        hSkinMenu = CreatePopupMenu();
        AppendMenuW(hSkinMenu, MF_STRING | MF_GRAYED, 0, L"(No skins)");
        
        // Create context menu
        hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, MENU_OPEN, L"Open");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, MENU_CONNECT, L"Connect");
        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSkinMenu, L"Change skin");
        AppendMenuW(hMenu, MF_STRING, MENU_REFRESH_SKIN, L"Refresh skin");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, MENU_RESET_BOARD, L"Reset board");
        AppendMenuW(hMenu, MF_STRING, MENU_CLOSE, L"Close");
        
        // Register for TaskbarCreated message (sent when explorer restarts)
        WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
        
        // Try to add the tray icon (with retry if shell isn't ready)
        TryAddTrayIcon();
        
        running = true;
        
        // Windows message loop
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // Cleanup
        Shell_NotifyIcon(NIM_DELETE, &nid);
        DestroyMenu(hSkinMenu);
        DestroyMenu(hMenu);
        DestroyWindow(trayHwnd);
        running = false;
    }
    
public:
    TrayManager(HWND sfmlWindow) 
        : mainHwnd(sfmlWindow), trayHwnd(NULL), hMenu(NULL), hSkinMenu(NULL),
          shouldRestore(false), shouldExit(false), shouldConnect(false),
          shouldDisconnect(false), shouldRefreshSkin(false), shouldResetBoard(false), selectedSkinIndex(-1),
          running(false), connectionState(ConnectionState::Disconnected),
          currentSkinIndex(-1), iconAdded(false), retryCount(0) {
        
        ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
        
        // Start the message loop thread
        messageThread = std::thread(&TrayManager::MessageLoop, this);
        
        // Wait for thread to initialize
        while (!running && !shouldExit) {
            Sleep(10);
        }
    }
    
    ~TrayManager() {
        if (trayHwnd) {
            PostMessage(trayHwnd, WM_DESTROY, 0, 0);
        }
        if (messageThread.joinable()) {
            messageThread.join();
        }
    }
    
    void MinimizeToTray() {
        ShowWindow(mainHwnd, SW_HIDE);
        // Shell_NotifyIcon(NIM_ADD, &nid);
    }
    
    void RestoreFromTray() {
        // Shell_NotifyIcon(NIM_DELETE, &nid);
        ShowWindow(mainHwnd, SW_SHOW);
        SetForegroundWindow(mainHwnd);
        shouldRestore = false;
    }
    
    void RemoveFromTray() {
        Shell_NotifyIcon(NIM_DELETE, &nid);
    }
    
    // Check if user clicked to restore from tray
    bool ShouldRestore() {
        return shouldRestore.exchange(false);
    }
    
    // Check if user clicked exit from tray menu
    bool ShouldExit() {
        return shouldExit.load();
    }
    
    // Check if user clicked Connect
    bool ShouldConnect() {
        return shouldConnect.exchange(false);
    }
    
    // Check if user clicked Disconnect
    bool ShouldDisconnect() {
        return shouldDisconnect.exchange(false);
    }
    
    // Check if user clicked Refresh skin
    bool ShouldRefreshSkin() {
        return shouldRefreshSkin.exchange(false);
    }

    bool ShouldResetBoard() {
        return shouldResetBoard.exchange(false);
    }
    
    int GetSelectedSkinIndex() {
        return selectedSkinIndex.exchange(-1);
    }
    
    void SetConnectionState(ConnectionState state) {
        connectionState = state;
    }
    
    // Set available skins
    void SetSkinList(const std::vector<std::string>& skins, int currentIndex = -1) {
        {
            std::lock_guard<std::mutex> lock(skinMutex);
            skinNames.clear();
            for (const auto& skin : skins) {
                // Convert UTF-8 to wide string
                int wideLen = MultiByteToWideChar(CP_UTF8, 0, skin.c_str(), -1, NULL, 0);
                std::wstring wideSkin(wideLen - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, skin.c_str(), -1, &wideSkin[0], wideLen);
                skinNames.push_back(wideSkin);
            }
            currentSkinIndex = currentIndex;
        }
        
        // Rebuild submenu on the message thread
        if (trayHwnd) {
            // Post a custom message or just rebuild directly since we hold the lock
            RebuildSkinMenu();
        }
    }
    
    // Set current skin index (for checkmark)
    void SetCurrentSkinIndex(int index) {
        std::lock_guard<std::mutex> lock(skinMutex);
        currentSkinIndex = index;
    }

    // Used for lazy initialization of TrayManager before we have the SFML window handle
    void UpdateMainWindowHandle(HWND newHwnd) {
        mainHwnd = newHwnd;
    }

    // Display a tray notification balloon
    // iconType: NIIF_INFO, NIIF_WARNING, NIIF_ERROR, or NIIF_USER (uses app icon)
    void ShowNotification(const std::wstring& title, const std::wstring& message, DWORD iconType = NIIF_INFO) {
        if (!trayHwnd) return;
        
        NOTIFYICONDATA nidBalloon = nid;
        nidBalloon.uFlags |= NIF_INFO;
        nidBalloon.dwInfoFlags = iconType;
        
        // Use the app icon for NIIF_USER
        if (iconType == NIIF_USER) {
            nidBalloon.dwInfoFlags |= NIIF_LARGE_ICON;
        }
        
        // Copy title (max 63 chars)
        wcsncpy_s(nidBalloon.szInfoTitle, title.c_str(), _TRUNCATE);
        
        // Copy message (max 255 chars)
        wcsncpy_s(nidBalloon.szInfo, message.c_str(), _TRUNCATE);
        
        Shell_NotifyIcon(NIM_MODIFY, &nidBalloon);
    }
    
    void ShowNotification(const std::string& title, const std::string& message, DWORD iconType = NIIF_INFO) {
        auto toWide = [](const std::string& str) {
            int wideLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
            std::wstring wide(wideLen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wide[0], wideLen);
            return wide;
        };
        ShowNotification(toWide(title), toWide(message), iconType);
    }
};
