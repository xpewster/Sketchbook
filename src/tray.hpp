#pragma once
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include "rc.h"
#include "log.hpp"

#define WM_TRAYICON (WM_USER + 1)

class TrayManager {
private:
    NOTIFYICONDATA nid;
    HMENU hMenu;
    HWND trayHwnd;          // Hidden window for receiving tray messages
    HWND mainHwnd;          // SFML window handle
    std::thread messageThread;
    
    std::atomic<bool> shouldRestore;
    std::atomic<bool> shouldExit;
    std::atomic<bool> running;
    
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
                    case WM_LBUTTONDBLCLK:
                        shouldRestore = true;
                        break;
                        
                    case WM_RBUTTONUP:
                    case WM_CONTEXTMENU: {
                        POINT pt;
                        GetCursorPos(&pt);
                        
                        // Required for menu to work properly
                        SetForegroundWindow(hWnd);
                        
                        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                pt.x, pt.y, 0, hWnd, NULL);
                        
                        if (cmd == 1) {  // Open
                            shouldRestore = true;
                        } else if (cmd == 2) {  // Exit
                            shouldExit = true;
                        }
                        break;
                    }
                }
                break;
                
            case WM_DESTROY:
                Shell_NotifyIcon(NIM_DELETE, &nid);
                PostQuitMessage(0);
                break;
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
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
        // nid.uVersion = NOTIFYICON_VERSION_4; 
        
        // Try to load custom icon from resources
        // nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        nid.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1), 
                                     IMAGE_ICON, 64, 64, LR_SHARED);
        if (!nid.hIcon) {
            LOG_ERROR << "Failed to load tray icon from resources, error: " << GetLastError() << ".\n";
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        }
        
        lstrcpy(nid.szTip, L"Sketchbook");
        
        // Create context menu
        hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_STRING, 1, L"Open");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, 2, L"Close");
        
        running = true;
        
        // Windows message loop
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // Cleanup
        Shell_NotifyIcon(NIM_DELETE, &nid);
        DestroyMenu(hMenu);
        DestroyWindow(trayHwnd);
        running = false;
    }
    
public:
    TrayManager(HWND sfmlWindow) 
        : mainHwnd(sfmlWindow), trayHwnd(NULL), hMenu(NULL),
          shouldRestore(false), shouldExit(false), running(false) {
        
        ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
        
        // Start the message loop thread
        messageThread = std::thread(&TrayManager::MessageLoop, this);
        
        // Wait for thread to initialize
        while (!running && !shouldExit) {
            Sleep(10);
        }

        Shell_NotifyIcon(NIM_ADD, &nid);
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
};