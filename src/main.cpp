#define UNICODE

#include <winsock2.h>
#include <locale.h>

#include "log.hpp"
#include "system_stats.h"
#include "image.hpp"
#include "dirty_rects.hpp"
#include "tcp.hpp"
#include "tray.hpp"
#include "utils/rgb565.h"
#include "utils/util.h"
#include "limit_instance.h"
#include "startup.hpp"

#include <SFML/Graphics.hpp>
#include <Shlobj.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <format>
#include <optional>

#include "ui/text_box.cpp"
#include "ui/button.cpp"
#include "ui/dropdown.cpp"
#include "ui/checkbox.cpp"
#include "ui/info.cpp"
#include "skins/skin.h"
#include "skins/debug_skin.cpp"
#include "skins/anime_skin.cpp"
#include "skins/flash_exporter.hpp"
#include "skins/anime_flash_exporter.cpp"
#include "settings.hpp"
#include "weather.hpp"
#include "train.hpp"
#include "frame.hpp"
#include "framelock.hpp"

// Transparent color key for flash mode (magenta = 0xF81F)
const sf::Color FLASH_TRANSPARENT_COLOR(248, 0, 248);
constexpr uint16_t FLASH_TRANSPARENT_RGB565 = 0xF81F;


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    PSTR lpCmdLine, int nCmdShow)
{
    CLimitSingleInstance lsi(TEXT("Global\\7c516a5a-76a0-4f12-8619-41570c33082c"));
    if(lsi.IsAnotherInstanceRunning()) {
        LOG_ERROR << "Another instance of Sketchbook is already running. Exiting.\n";
        return 0;
    }
    if (!IsUserAnAdmin()) {
        LOG_ERROR << "This application must be run as administrator.\n";
        return 0;
    }
    setlocale(LC_ALL, "");

    LOG_INFO << "Starting Sketchbook...\n";

    Settings settings;
    if (!settings.load()) {
        LOG_ERROR << "Failed to load settings.toml\n";
        return 0;
    }

    if (settings.weather.apiKey == "YOUR_API_KEY_HERE" || settings.weather.apiKey.empty()) {
        LOG_WARN << "Please set your OpenWeatherMap API key in settings.toml\n";
    }

    LOG_INFO << "Successfully loaded settings\n";

    // Initialize startup manager
    StartupManager startupManager(L"Sketchbook");
    // bool isStartupMinimized = startupManager.IsInStartup() && startupManager.IsStartupMinimized();
    // if (settings.preferences.startMinimized != isStartupMinimized) {
    //     LOG_WARN << "Startup minimized setting mismatch. Using windows setting " << isStartupMinimized << "\n";
    //     settings.preferences.startMinimized = isStartupMinimized;
    // }
    if (startupManager.IsInStartup() && startupManager.IsStartupMinimized()) {
        LOG_WARN << "STARTUP SHORTCUT IS SET TO START MINIMIZED. THIS WILL CAUSE UI ISSUES\n";
    }

    // Initialize main window (lazy creation to avoid window flash on startup)
    const int menuHeight = 40;
    const int previewScale = 1;
    const int previewWidth = qualia::DISPLAY_HEIGHT / previewScale; // Use DISPLAY_HEIGHT since the preview will be rotated 90 degrees
    const int previewHeight = qualia::DISPLAY_WIDTH / previewScale; // Use DISPLAY_WIDTH since the preview will be rotated 90 degrees
    const int windowWidth = previewWidth + 40; 
    const int windowHeight = menuHeight + previewHeight + 50;

    std::optional<sf::RenderWindow> window;
    HWND hwnd = nullptr;
    
    // Initialize tray manager
    TrayManager trayManager(hwnd);
    LOG_INFO << "Initialized system tray manager\n";
    
    // Create the window when needed
    auto createWindow = [&]() {
        if (!window.has_value()) {
            window.emplace(sf::VideoMode(sf::Vector2u(windowWidth, windowHeight)), "Sketchbook", sf::Style::Titlebar | sf::Style::Close);
            window->setFramerateLimit(30);
            hwnd = window->getNativeHandle();
            trayManager.UpdateMainWindowHandle(hwnd);
        }
    };
    
    // Only create window immediately if not starting minimized
    if (!settings.preferences.startMinimized) {
        LOG_INFO << "Creating main window...\n";
        createWindow();
        LOG_INFO << "Main window created\n";
    } else {
        LOG_INFO << "Starting minimized on startup\n"; // Window creation deferred
    }

    // Load font
    sf::Font font;
    if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
        LOG_ERROR << "Failed to load default font\n";
        return 0;
    }

    std::unordered_map<std::string, Skin*> skins;
    std::vector<std::unique_ptr<AnimeSkin>> animeSkins; // Storage

    std::string skinName = settings.preferences.selectedSkin;
    LOG_INFO << "Selected skin: " << skinName << "\n";

    DebugSkin debugSkin = DebugSkin(std::string("Debug"), qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH);
    debugSkin.initialize("");
    skins["Debug"] = &debugSkin;

    // Dynamically load skins from skins/ folder
    namespace fs = std::filesystem;
    std::string skinsPath = "skins/";

    if (fs::exists(skinsPath) && fs::is_directory(skinsPath)) {
        for (const auto& entry : fs::directory_iterator(skinsPath)) {
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();
                std::string skinXmlPath = entry.path().string() + "/skin.xml";
                
                if (fs::exists(skinXmlPath)) {
                    auto skin = std::make_unique<AnimeSkin>(folderName, qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH);
                    skins[folderName] = skin.get();
                    animeSkins.push_back(std::move(skin));
                    LOG_INFO << "Loaded skin: " << folderName << "\n";
                } else {
                    LOG_WARN << "Skipping folder '" << folderName << "' - no skin.xml found\n";
                }
            }
        }
    } else {
        LOG_WARN << "Skins directory not found: " << skinsPath << "\n";
    }
    if (settings.preferences.flashMode) {
        LOG_INFO << "Flash enabled for drive " << settings.network.espDrive << "\n";
    }
    
    // Create render texture at Qualia's native resolution
    sf::RenderTexture qualiaTexture(sf::Vector2u(qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH)); // Swapped dimensions for 90 degree rotation
    
    // Secondary texture for frame lock with real-time preview (renders the locked frame for sending)
    sf::RenderTexture lockedTexture(sf::Vector2u(qualia::DISPLAY_HEIGHT, qualia::DISPLAY_WIDTH));
    
    // RGB565 image buffer for sending
    qualia::Image frameBuffer(qualia::DISPLAY_WIDTH, qualia::DISPLAY_HEIGHT);
    
    // TCP connection and sender thread
    TcpConnection connection;
    FrameSender sender;
    bool connected = false;
    
    // Frame lock controller
    FrameLockController frameLock(20.0);  // Target 20 FPS
    
    // System monitor
    SystemMonitor monitor;

    // Weather monitor
    WeatherMonitor weatherMonitor(settings.weather.apiKey, settings.weather.latitude, settings.weather.longitude, settings.weather.units);
    
    // Train monitor
    TrainMonitor trainMonitor(settings.train.apiBase, settings.train.apiKey, settings.train.stopId0, settings.train.stopId1);

    // Status
    std::string statusMsg = "Disconnected";
    
    // UI elements
    TextInput ipInput(10, 8, 120, 24, settings.network.espIP, font);
    Button connectBtn(140, 8, 90, 24, "Connect", font);
    connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
    std::vector<std::string> skinOptions;
    for (const auto& pair : skins) {
        skinOptions.push_back(pair.first);
    }
    int defaultSkinIndex = getSkinIndex(skinOptions, skinName);
    if (defaultSkinIndex == -1) {
        LOG_WARN << "Selected skin '" << skinName << "' not found. Defaulting to first available skin.\n";
        defaultSkinIndex = 0;
        skinName = skinOptions[0];
    }
    skins[skinName]->initialize(skinsPath + skinName + "/skin.xml"); // Initialize the selected skin
    trayManager.SetSkinList(skinOptions, defaultSkinIndex); // Only happens once for now
    DropdownSelector skinDropdown(240, 8, 120, 24, skinOptions, font, defaultSkinIndex);
    Button refreshBtn(370, 8, 24, 24, "", font);
    sf::Texture refreshIconTexture;
    refreshBtn.setColor(sf::Color(252, 186, 3), sf::Color(252, 205, 76));
    refreshBtn.setIcon("resources/Refresh.png", 0, 0, 24, 24);
    Checkbox frameLockCB(400, 8, 12, "Frame lock", font, 4, -2, settings.preferences.frameLock);
    InfoIcon frameLockInfo(485, 8, 15, "resources/Info.png", "When enabled, the sender thread will wait for the remote device to finish processing each frame before progressing the animation. This prevents frame drops at the expense of slower animation.", font);
    Checkbox flashModeCB(400, 24, 12, "Flash mode", font, 4, -2, settings.preferences.flashMode);
    InfoIcon flashModeInfo(485, 22, 15, "resources/Info.png", "When enabled, the program will only send raw data and selected image streaming to the remote device. The rest of the image will have to be flashed to the remote device along with any relevant config and developed there. The button below initiates the flash sequence.", font);
    Checkbox dirtyRectCB((float)(windowWidth - 150), (float)(windowHeight - 22), 12, "Show dirty rects", font, 4, -2, settings.preferences.showDirtyRects);
    dirtyRectCB.setLabelColor(sf::Color::White);
    TextInput flashDriveInput(400, 176, 40, 24, settings.network.espDrive, font);
    Button flashBtn(450, 176, 90, 24, "MemFlash", font);
    flashBtn.setColor(sf::Color(0, 64, 255), sf::Color(54, 99, 235));
    flashBtn.setLabelColor(sf::Color::White);
    flashModeInfo.setExtraHeight(30);
    flashModeInfo.enableHoverOverBox(true);
    Checkbox realtimeCB((float)(windowWidth - 280), (float)(windowHeight - 22), 12, "Real-time preview", font, 4, -2, settings.preferences.frameLockRealTimePreview);
    realtimeCB.setLabelColor(sf::Color::White);
    float previewCompositeCBX0 = (float)(windowWidth - 410);
    float previewCompositeCBX1 = (float)(windowWidth - 280);
    Checkbox previewCompositeCB(previewCompositeCBX0, (float)(windowHeight - 22), 12, "Preview composite", font, 4, -2, true);
    previewCompositeCB.setLabelColor(sf::Color::White);
    InfoIcon settingsInfo((float)(windowWidth - 50), 10, 15, "resources/Settings.png", "Settings", font, InfoBoxDirection::Left);
    settingsInfo.setExtraHeight(130);
    settingsInfo.enableHoverOverBox(true);
    Checkbox startupSettingCB((float)(windowWidth - 200), 64, 12, "Start with Windows", font, 4, -2, startupManager.IsInStartup());
    Checkbox startMinimizedSettingCB((float)(windowWidth - 200), 84, 12, "Start minimized", font, 4, -2, settings.preferences.startMinimized);
    Checkbox closeToTraySettingCB((float)(windowWidth - 200), 104, 12, "Close to tray", font, 4, -2, settings.preferences.closeToTray);
    Checkbox autoConnectSettingCB((float)(windowWidth - 200), 124, 12, "AutoConnect", font, 4, -2, settings.preferences.autoConnect);
    Button resetBoardSettingsBtn((float)(windowWidth - 200), 144, 90, 24, "Reset board", font);
    resetBoardSettingsBtn.setColor(sf::Color(235, 180, 52), sf::Color(245, 205, 86));
    
    // Status indicator
    sf::CircleShape statusIndicator(8);
    statusIndicator.setPosition(sf::Vector2f((float)(windowWidth - 28), (float)(menuHeight / 2 - 8)));
    statusIndicator.setFillColor(sf::Color::Red);
    sf::CircleShape statusIndicatorBorder(8);
    statusIndicatorBorder.setPosition(sf::Vector2f((float)(windowWidth - 28), (float)(menuHeight / 2 - 8)));
    statusIndicatorBorder.setOutlineColor(sf::Color::Black);
    statusIndicatorBorder.setOutlineThickness(1);
    statusIndicatorBorder.setFillColor(sf::Color::Transparent);
    
    // Preview sprite
    sf::Sprite previewSprite(qualiaTexture.getTexture());
    int previewX = 20;
    int previewY = 20;
    
    // Preview border
    sf::RectangleShape previewBorder(sf::Vector2f((float)(previewWidth + 4), (float)(previewHeight + 4)));
    previewBorder.setPosition(sf::Vector2f((float)(previewX - 2), (float)(previewY - 2) + menuHeight));
    previewBorder.setFillColor(sf::Color(80, 80, 80));
    
    // Menu bar background
    sf::RectangleShape menuBar(sf::Vector2f((float)windowWidth, (float)menuHeight));
    menuBar.setFillColor(sf::Color(214, 207, 182));
    
    // Status text
    sf::Text statusText(font, statusMsg, 14);
    statusText.setPosition(sf::Vector2f(10, (float)(windowHeight - 25)));
    statusText.setFillColor(sf::Color::White);
    
    // Timing
    sf::Clock sendClock;
    const float sendInterval = 0.05f;  // ~20 FPS target
    
    // Wall clock for animation
    auto startTime = std::chrono::steady_clock::now();
    
    // Flash export status
    std::string flashExportStatus;
    
    // Async connection state
    ConnectionState connectionState = ConnectionState::Disconnected;
    std::thread connectThread;
    std::atomic<bool> connectResult{false};
    std::atomic<bool> connectFinished{false};
    std::string connectingIP;
    sf::Clock ellipsisClock;
    sf::Clock logFlushClock;
    sf::Clock lastConnectAttemptClock;
    bool firstConnectionAttempt = true; // Allow immediate first attempt without waiting
    bool pendingModeSync = false; // Track if we need to send mode selection after connection
    bool pausedAutoConnect = false; // Stops instant reconnects after manual disconnection when AutoConnect is enabled

    auto attemptConnection = [&]() {
        connectionState = ConnectionState::Connecting;
        connectingIP = ipInput.value;
        connectResult = false;
        connectFinished = false;
        ellipsisClock.restart();
        connectBtn.setLabel("Cancel");
        connectBtn.setColor(sf::Color(255, 200, 100), sf::Color(255, 220, 150));
        statusIndicator.setFillColor(sf::Color::Yellow);
        
        // Launch connection thread
        if (connectThread.joinable()) {
            connectThread.join();
        }
        connectThread = std::thread([&connection, &connectResult, &connectFinished, ip = connectingIP, port = settings.network.espPort]() {
            connectResult = connection.connect(ip, port);
            LOG_INFO << "Connection attempt to " << ip << ":" << port << (connectResult ? " succeeded" : " failed") << "\n";
            connectFinished = true;
        });

        if (pausedAutoConnect) {
            pausedAutoConnect = false; // Unpause for next attempts
            return;
        }
    };

    auto disconnect = [&](bool userInitiated = false) {
        sender.stop();
        connection.disconnect();
        connected = false;
        connectionState = ConnectionState::Disconnected;
        statusMsg = "Disconnected";
        connectBtn.setLabel("Connect");
        connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
        statusIndicator.setFillColor(sf::Color::Red);
        if (userInitiated && settings.preferences.autoConnect) {
            statusMsg = "Disconnected. AutoConnect paused.";
            settings.preferences.autoConnect = false;
            pausedAutoConnect = true;
        }
    };

    auto selectSkin = [&](const std::string& newSkinName) {
        LOG_INFO << "Skin changed from " << settings.preferences.selectedSkin << " to: " << newSkinName << " (" << (skins[newSkinName]->initialized ? "initialized" : "not initialized") << ")\n";
        settings.preferences.selectedSkin = newSkinName;
        int currentSkinIndex = getSkinIndex(skinOptions, newSkinName);
        
        if (!skins[newSkinName]->initialized) {
            LOG_INFO << "First time initializing skin: " << newSkinName << "\n";
            skins[newSkinName]->initialize(("skins/" + newSkinName + "/skin.xml").c_str());
        }

        if (skins[newSkinName]->hasFlashConfig()) {
            LOG_INFO << "New skin supports flash mode. Defaultly enabling flash mode for new skin.\n";
            flashModeCB.setChecked(true, true);
            settings.preferences.flashMode = true;
        }
    };

    // Helper to send mode selection to remote
    auto syncModeToDevice = [&]() -> bool {
        if (!connected) {
            LOG_WARN << "Cannot sync mode - not connected\n";
            return false;
        }
        
        LOG_INFO << "Syncing mode to device: " << (settings.preferences.flashMode ? "flash" : "streaming") << "\n";
        
        if (sender.sendModeSelection(settings.preferences.flashMode)) {
            LOG_INFO << "Mode selection sent and acknowledged\n";
            sender.invalidateDirtyTracker(); // Invalidate dirty tracker to force full redraw in new mode
            return true;
        } else {
            LOG_ERROR << "Failed to send mode selection\n";
            return false;
        }
    };

    bool running = true;
    while (running) {

        // Check if user wants to restore from tray - create window if needed
        if (trayManager.ShouldRestore()) {
            if (!window.has_value()) {
                LOG_INFO << "Restoring from tray for the first time - creating main window\n";
                createWindow();
                LOG_INFO << "Main window created\n";
            }
            trayManager.RestoreFromTray();
        }
        
        // Check if user wants to exit from tray menu
        if (trayManager.ShouldExit()) {
            running = false;
            LOG_INFO << "Exiting from system tray menu\n";
            if (window.has_value()) {
                LOG_INFO << "Closing main window\n";
                window->close();
            }
            break;
        }
        
        // Check if window was closed
        if (window.has_value() && !window->isOpen()) {
            LOG_INFO << "Window closed by user\n";
            running = false;
            break;
        }

        bool debugFlag = false;

        bool windowInitiatedReset = false;
        bool windowInitiatedSkinRefresh = false;
        if (window.has_value()) {

            // Handle events
            bool mousePressed = false;
            sf::Vector2i mousePos = sf::Mouse::getPosition(*window);
            
            
            while (const std::optional<sf::Event> event = window->pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    if (settings.preferences.closeToTray) {
                        trayManager.ShowNotification("Minimized to tray", "You've put away Sketchbook for now. It will continue running in the background.", NIIF_USER);
                        trayManager.MinimizeToTray();
                    } else {
                        window->close();
                    }
                }
                if (const auto* buttonPressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                    if (buttonPressed->button == sf::Mouse::Button::Left) {
                        mousePressed = true;
                    }
                }
                ipInput.handleEvent(*event, mousePos, *window);
                skinDropdown.handleEvent(*event, mousePos, *window);
                skinName = skinDropdown.getSelectedValue();
                if (skinName != settings.preferences.selectedSkin) {
                    selectSkin(skinName);
                    trayManager.SetCurrentSkinIndex(getSkinIndex(skinOptions, skinName));
                }
                dirtyRectCB.handleEvent(*event, mousePos, *window);
                settings.preferences.showDirtyRects = dirtyRectCB.isChecked();
                frameLockCB.handleEvent(*event, mousePos, *window);
                settings.preferences.frameLock = frameLockCB.isChecked();
                flashModeCB.handleEvent(*event, mousePos, *window);
                settings.preferences.flashMode = flashModeCB.isChecked();
                frameLockInfo.handleEvent(*event, mousePos, *window);
                flashModeInfo.handleEvent(*event, mousePos, *window);
                if (flashModeInfo.isHovered()) {
                    flashDriveInput.handleEvent(*event, mousePos, *window);
                    settings.network.espDrive = flashDriveInput.value;
                }
                if (frameLockCB.isChecked()) {
                    realtimeCB.handleEvent(*event, mousePos, *window);
                    settings.preferences.frameLockRealTimePreview = realtimeCB.isChecked();
                }
                if (settings.preferences.flashMode) {
                    previewCompositeCB.setPosition(frameLockCB.isChecked() ? previewCompositeCBX0 : previewCompositeCBX1, (float)(windowHeight - 22));
                    previewCompositeCB.handleEvent(*event, mousePos, *window);
                    skins[skinName]->getFlashConfig().previewComposite = previewCompositeCB.isChecked();
                }
                settingsInfo.handleEvent(*event, mousePos, *window);
                if (settingsInfo.isHovered()) {
                    startupSettingCB.handleEvent(*event, mousePos, *window);
                    startMinimizedSettingCB.handleEvent(*event, mousePos, *window);
                    settings.preferences.startMinimized = startMinimizedSettingCB.isChecked();
                    // if (startMinimizedSettingCB.wasJustUpdated() && startupManager.IsInStartup()) {
                    //     LOG_INFO << "Startup already exists. Updating startup shortcut to " << (settings.preferences.startMinimized ? "start minimized" : "not start minimized") << ".\n";
                    //     if (startupManager.UpdateStartupSettings(settings.preferences.startMinimized)) {
                    //         LOG_INFO << "Updated startup shortcut successfully.\n";
                    //     } else {
                    //         LOG_ERROR << "Failed to update startup shortcut.\n";
                    //     }
                    // }
                    closeToTraySettingCB.handleEvent(*event, mousePos, *window);
                    settings.preferences.closeToTray = closeToTraySettingCB.isChecked();
                    autoConnectSettingCB.handleEvent(*event, mousePos, *window);
                    settings.preferences.autoConnect = autoConnectSettingCB.isChecked();
                    if (startupSettingCB.wasJustUpdated()) {
                        if (startupSettingCB.isChecked()) {
                            if (startupManager.IsInStartup(true)) {
                                LOG_INFO << "Already in Windows startup.\n";
                            } else if (startupManager.AddToStartup(settings.preferences.startMinimized)) {
                                LOG_INFO << "Added to Windows startup successfully.\n";
                            } else {
                                LOG_ERROR << "Failed to add to Windows startup.\n";
                                startupSettingCB.setChecked(false);
                            }
                        } else {
                            if (!startupManager.IsInStartup(true)) {
                                LOG_INFO << "Already not in Windows startup.\n";
                            } else if (startupManager.RemoveFromStartup()) {
                                LOG_INFO << "Removed from Windows startup successfully.\n";
                            } else {
                                LOG_ERROR << "Failed to remove from Windows startup.\n";
                                startupSettingCB.setChecked(true);
                            }
                        }
                    }
                }
            }
            ipInput.update(mousePos, *window);
            skinDropdown.update(mousePos, *window);
            flashDriveInput.update(mousePos, *window);

            // Handle flash export button
            if (flashBtn.update(mousePos, mousePressed, *window)) {
                flash::AnimeSkinFlashExporter exporter(settings.network.espDrive);
                
                // Check if flashable first
                if (!exporter.isFlashable()) {
                    flashExportStatus = "Drive not flashable (no FLASHABLE marker)";
                } else {
                    // Clear old files first
                    exporter.clearAssetDirectory();
                    
                    // Use same rotation as frame streaming
                    flash::ExportRotation rotation = settings.preferences.rotate180 
                        ? flash::ExportRotation::RotNeg90 
                        : flash::ExportRotation::Rot90;
                    auto result = exporter.exportSkin(skins[skinName], rotation);
                    if (result.success) {
                        flashExportStatus = "Flash export OK: " + std::to_string(result.exportedFiles.size()) + " files";
                    } else {
                        flashExportStatus = "Flash export failed: " + result.error;
                    }
                }
            }

            
            if (refreshBtn.update(mousePos, mousePressed, *window) || trayManager.ShouldRefreshSkin()) {
                windowInitiatedSkinRefresh = true; // Defer action until outside window loop to allow it to work if window hasn't been created yet
            }
            if (resetBoardSettingsBtn.update(mousePos, mousePressed, *window) || trayManager.ShouldResetBoard()) {
                windowInitiatedReset = true; // Defer action until outside window loop to allow it to work if window hasn't been created yet
            }
            
            // Connect button
            if (connectBtn.update(mousePos, mousePressed, *window)) {
                if (connected) {
                    disconnect(true);
                } else if (connectionState == ConnectionState::Disconnected) {
                    attemptConnection();
                    lastConnectAttemptClock.restart();
                } else if (connectionState == ConnectionState::Connecting) {
                    // Cancel connection attempt
                    if (connectThread.joinable()) {
                        connectThread.join();
                    }
                    connection.disconnect();
                    connectionState = ConnectionState::Disconnected;
                    statusMsg = "Connection cancelled";
                    connectBtn.setLabel("Connect");
                    connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
                    statusIndicator.setFillColor(sf::Color::Red);
                }
            }
        }
        
        // Check for send errors from background thread
        if (connected && sender.hadError()) {
            LOG_INFO << "Sender thread reported an error. Disconnecting...\n";
            sender.stop();
            connection.disconnect();
            connected = false;
            connectionState = ConnectionState::Disconnected;
            statusMsg = "Connection lost";
            connectBtn.setLabel("Connect");
            connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
            statusIndicator.setFillColor(sf::Color::Red);
            sender.clearError();
            trayManager.ShowNotification("Connection lost", "Sketchbook has lost connection with it's pencil!", NIIF_USER);
        }

        if (settings.preferences.autoConnect && connectionState == ConnectionState::Disconnected && (firstConnectionAttempt || lastConnectAttemptClock.getElapsedTime().asSeconds() > 5.0f)) {
            LOG_INFO << "AutoConnecting...\n";
            attemptConnection();
            firstConnectionAttempt = false;
        }
        if (trayManager.ShouldConnect()) {
            attemptConnection();
        }
        if (trayManager.ShouldDisconnect()) {
            disconnect(true);
        }
        int traySkinIndex = trayManager.GetSelectedSkinIndex();
        if (traySkinIndex != -1 && skins.size() > static_cast<size_t>(traySkinIndex)) {
            std::string traySelectedSkin = skinOptions[traySkinIndex];
            if (traySelectedSkin != skinName) {
                LOG_INFO << "Skin change from system tray menu: " << traySelectedSkin << "\n";
                selectSkin(traySelectedSkin);
                skinDropdown.setSelectedIndex(traySkinIndex);
                trayManager.SetCurrentSkinIndex(traySkinIndex);
            }
        }
        if (windowInitiatedSkinRefresh || trayManager.ShouldRefreshSkin()) {
            // Force refresh skin parameters
            for (auto& pair : skins) {
                if (pair.second->initialized) {
                    pair.second->initialize(pair.second->xmlFilePath);
                }
            }
        }
        if (windowInitiatedReset || trayManager.ShouldResetBoard()) {
            if (connected) {
                LOG_INFO << "Resetting board...\n";
                if (sender.sendReset()) {
                    LOG_INFO << "Reset command sent successfully.\n";
                } else {
                    LOG_ERROR << "Failed to send reset command.\n";
                    statusMsg = "Failed to send reset command";
                }
            } else {
                LOG_WARN << "Cannot reset board - not connected\n";
                trayManager.ShowNotification("Cannot reset board", "Sketchbook must be connected to the remote board to reset it.", NIIF_WARNING);
                statusMsg = "Not connected - cannot reset board";
            }
        }
        
        // Check async connection result
        if (connectionState == ConnectionState::Connecting) {
            // Animated ellipsis
            int ellipsisHz = 6; // How many times per second to update the ellipsis
            int dots = (int)(ellipsisClock.getElapsedTime().asSeconds() * ellipsisHz) % 4;
            std::string ellipsis(dots, '.');
            statusMsg = "Connecting" + ellipsis;
            
            if (connectFinished) {
                if (connectThread.joinable()) {
                    connectThread.join();
                }
                if (connectResult) {
                    settings.network.espIP = connectingIP;
                    connected = true;
                    connectionState = ConnectionState::Connected;
                    statusMsg = "Connected to " + connectingIP;
                    connectBtn.setLabel("Disconnect");
                    connectBtn.setColor(sf::Color(255, 100, 100), sf::Color(255, 150, 150));
                    statusIndicator.setFillColor(sf::Color::Green);
                    sender.start(&connection);
                    frameLock.reset();  // Reset frame lock timing on new connection
                    pendingModeSync = true; // We want to sync mode selection after connecting
                } else {
                    connectionState = ConnectionState::Disconnected;
                    statusMsg = "Connection timed out";
                    connectBtn.setLabel("Connect");
                    connectBtn.setColor(sf::Color(100, 255, 100), sf::Color(150, 255, 150));
                    statusIndicator.setFillColor(sf::Color::Red);
                }
            }
        }
        trayManager.SetConnectionState(connectionState);

        if (connected) {
            firstConnectionAttempt = true; // Allow immediate reconnect after a successful connection
        }

        // Handle pending mode sync
        if (pendingModeSync && connected) {
            if (!syncModeToDevice()) {
                flashExportStatus = "Failed to sync mode to device";
            }
            pendingModeSync = false;
        }

        // Disable flash mode checkbox if skin doesn't support it
        if (skins[skinName]->initialized && !skins[skinName]->hasFlashConfig()) {
            if (settings.preferences.flashMode) {
                flashModeCB.setChecked(false, true);
                flashModeCB.setDisabled(true);
                LOG_INFO << "Skin does not support flash mode. Disabling flash mode.\n";
            }
            settings.preferences.flashMode = false;
        } else {
            flashModeCB.setDisabled(false);
        }

        // Handle flash mode checkbox
        if (flashModeCB.wasJustUpdated()) {
            if (connected) {
                if (!syncModeToDevice()) {
                    // Revert checkbox on failure
                    flashModeCB.setChecked(!settings.preferences.flashMode);
                    settings.preferences.flashMode = !settings.preferences.flashMode;
                    flashExportStatus = "Failed to sync mode to device";
                }
            } else {
                LOG_INFO << "Flash mode changed to " << (flashModeCB.isChecked() ? "enabled" : "disabled") << " but not connected, so deferring sync\n";
            }
        }

        // Update frame lock controller
        frameLock.update();
        
        // Check if sender consumed a frame (for frame lock)
        if (connected && settings.preferences.frameLock && sender.checkAndClearFrameConsumed()) {
            frameLock.onFrameConsumed();
        }
        
        // Get system stats
        SystemStats stats = monitor.getStats();
        WeatherData weather = weatherMonitor.getWeather();
        TrainData train = trainMonitor.getTrain();
        
        // Calculate animation time
        double wallAnimTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        
        // Determine which layers to skip for flash mode
        FlashLayer flashedLayers = FlashLayer::None;
        bool isFlashModeActive = settings.preferences.flashMode;
        if (isFlashModeActive) {
            flashedLayers = skins[skinName]->getFlashConfig().enabledLayers;
        }
        
        // Draw to texture based on mode
        if (connected && settings.preferences.frameLock) {
            double lockedAnimTime = frameLock.getLockedTime();
            
            if (settings.preferences.frameLockRealTimePreview) {
                // Real-time preview: draw with wall time for display
                // previewComposite controls what we SEE, not what we SEND
                if (isFlashModeActive && !skins[skinName]->getFlashConfig().previewComposite) {
                    skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, wallAnimTime, 
                                                   flashedLayers, FLASH_TRANSPARENT_COLOR);
                } else {
                    skins[skinName]->draw(qualiaTexture, stats, weather, train, wallAnimTime);
                }
                
                // Draw with locked time for sending - ALWAYS use drawForFlash when flash mode active
                if (sendClock.getElapsedTime().asSeconds() >= sendInterval && sender.isReadyForFrame()) {
                    if (isFlashModeActive) {
                        skins[skinName]->drawForFlash(lockedTexture, stats, weather, train, lockedAnimTime,
                                                       flashedLayers, FLASH_TRANSPARENT_COLOR);
                    } else {
                        skins[skinName]->draw(lockedTexture, stats, weather, train, lockedAnimTime);
                    }
                    sendClock.restart();
                    if (settings.preferences.rotate180) {
                        textureToRGB565RotNeg90(lockedTexture, frameBuffer);
                    } else {
                        textureToRGB565Rot90(lockedTexture, frameBuffer);
                    }
                    
                    if (isFlashModeActive) {
                        auto flashStats = flash::buildFlashStats(stats, weather, train, skins[skinName]);
                        sender.queueFlashUpdate(flashStats, frameBuffer);
                    } else {
                        sender.queueFrame(frameBuffer);
                    }
                }
            } else {
                // Standard frame lock: previewComposite controls preview, always send with drawForFlash
                if (isFlashModeActive && !skins[skinName]->getFlashConfig().previewComposite) {
                    skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, lockedAnimTime,
                                                   flashedLayers, FLASH_TRANSPARENT_COLOR);
                } else {
                    skins[skinName]->draw(qualiaTexture, stats, weather, train, lockedAnimTime);
                }
                
                if (sendClock.getElapsedTime().asSeconds() >= sendInterval && sender.isReadyForFrame()) {
                    // For sending, ALWAYS use drawForFlash when flash mode is active
                    if (isFlashModeActive) {
                        // Re-render with drawForFlash for sending (preview might have used draw())
                        skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, lockedAnimTime,
                                                       flashedLayers, FLASH_TRANSPARENT_COLOR);
                    }
                    sendClock.restart();
                    if (settings.preferences.rotate180) {
                        textureToRGB565RotNeg90(qualiaTexture, frameBuffer);
                    } else {
                        textureToRGB565Rot90(qualiaTexture, frameBuffer);
                    }
                    
                    if (isFlashModeActive) {
                        auto flashStats = flash::buildFlashStats(stats, weather, train, skins[skinName]);
                        sender.queueFlashUpdate(flashStats, frameBuffer);
                        // Re-render with composite for preview if needed
                        if (skins[skinName]->getFlashConfig().previewComposite) {
                            skins[skinName]->draw(qualiaTexture, stats, weather, train, lockedAnimTime);
                        }
                    } else {
                        sender.queueFrame(frameBuffer);
                    }
                }
            }
            
            // Status message
            float ratio = sender.getCompressionRatio();
            int rects = sender.getLastRectCount();
            size_t packetKB = sender.getLastPacketSize() / 1024;
            std::string lockStatus = frameLock.isFrozen() ? " [FROZEN]" : "";
            std::string flashStatus = isFlashModeActive ? " [FLASH]" : "";
            statusMsg = std::format("Connected | FPS: {:.1f} | {:.0f}% dirty ({} rects, {}KB){}{}", 
                                   sender.getFPS(), ratio * 100.0f, rects, packetKB, lockStatus, flashStatus);
        } else {
            // No frame lock: previewComposite controls preview, always send with drawForFlash
            if (isFlashModeActive && !skins[skinName]->getFlashConfig().previewComposite) {
                skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, wallAnimTime,
                                               flashedLayers, FLASH_TRANSPARENT_COLOR);
            } else {
                skins[skinName]->draw(qualiaTexture, stats, weather, train, wallAnimTime);
            }
            
            if (connected && sendClock.getElapsedTime().asSeconds() >= sendInterval) {
                // For sending, ALWAYS use drawForFlash when flash mode is active
                if (isFlashModeActive) {
                    skins[skinName]->drawForFlash(qualiaTexture, stats, weather, train, wallAnimTime,
                                                   flashedLayers, FLASH_TRANSPARENT_COLOR);
                }
                sendClock.restart();
                if (settings.preferences.rotate180) {
                    textureToRGB565RotNeg90(qualiaTexture, frameBuffer);
                } else {
                    textureToRGB565Rot90(qualiaTexture, frameBuffer);
                }
                
                if (isFlashModeActive) {
                    auto flashStats = flash::buildFlashStats(stats, weather, train, skins[skinName]);
                    sender.queueFlashUpdate(flashStats, frameBuffer);
                    // Re-render with composite for preview if needed
                    if (skins[skinName]->getFlashConfig().previewComposite) {
                        skins[skinName]->draw(qualiaTexture, stats, weather, train, wallAnimTime);
                    }
                } else {
                    sender.queueFrame(frameBuffer);
                }
                
                float ratio = sender.getCompressionRatio();
                int rects = sender.getLastRectCount();
                size_t packetKB = sender.getLastPacketSize() / 1024;
                std::string flashStatus = isFlashModeActive ? " [FLASH]" : "";
                statusMsg = std::format("Connected | FPS: {:.1f} | {:.0f}% dirty ({} rects, {}KB){}", 
                                       sender.getFPS(), ratio * 100.0f, rects, packetKB, flashStatus);
            }
        }
        
        // Update status text
        if (!flashExportStatus.empty()) {
            statusMsg = flashExportStatus;
        }
        statusText.setString(statusMsg);
        
        // Draw window
        if (window.has_value()) {
            window->clear(sf::Color(60, 60, 60));
            
            // Preview
            window->draw(previewBorder);
            previewSprite.setTexture(qualiaTexture.getTexture());
            previewSprite.setOrigin(sf::Vector2f((float)previewWidth / 2, (float)previewHeight / 2));
            previewSprite.setPosition(sf::Vector2f((float)previewX + (float)previewWidth / 2, (float)previewY + (float)previewHeight / 2 + menuHeight));
            window->draw(previewSprite);

            // // Draw dot at center of preview for alignment reference
            // sf::CircleShape centerDot(3);
            // centerDot.setFillColor(sf::Color::Red);
            // centerDot.setPosition(sf::Vector2f((float)previewX + (float)previewHeight / 2 - 3, (float)previewY + (float)previewWidth / 2 - 3));
            // window.draw(centerDot);

            // Show dirty rectangles on preview
            if (connected && settings.preferences.showDirtyRects) {
                std::vector<qualia::DirtyRect> dirtyRects = sender.getLastDirtyRects();
                for (const auto& rect : dirtyRects) {
                    // Need to unrotate
                    int x = rect.x;
                    int y = rect.y;
                    int w = rect.w;
                    int h = rect.h;
                    if (settings.preferences.rotate180) {
                        x = qualia::DISPLAY_HEIGHT - rect.y + 1 - rect.h;
                        y = rect.x;
                        w = rect.h;
                        h = rect.w;
                    } else {
                        x = rect.y;
                        y = qualia::DISPLAY_WIDTH - rect.x + 1 - rect.w;
                        w = rect.h;
                        h = rect.w;
                    }
                    sf::RectangleShape r(sf::Vector2f((float)w, (float)h));
                    r.setPosition(sf::Vector2f((float)(previewX + x), (float)(previewY + y + menuHeight)));
                    r.setFillColor(sf::Color(255, 0, 0, 100));
                    window->draw(r);
                }
            }

            // Menu bar
            window->draw(menuBar);
            ipInput.draw(*window);
            connectBtn.draw(*window);
            skinDropdown.draw(*window);
            refreshBtn.draw(*window);
            frameLockCB.draw(*window);
            flashModeCB.draw(*window);
            flashModeInfo.draw(*window);
            frameLockInfo.draw(*window);
            if (flashModeInfo.isHovered()) {
                flashDriveInput.draw(*window);
                flashBtn.draw(*window);
            }
            settingsInfo.draw(*window);
            if (settingsInfo.isHovered()) {
                startupSettingCB.draw(*window);
                startMinimizedSettingCB.draw(*window);
                closeToTraySettingCB.draw(*window);
                autoConnectSettingCB.draw(*window);
                resetBoardSettingsBtn.draw(*window);
            }
            window->draw(statusIndicator);
            window->draw(statusIndicatorBorder);  
            
            // Status bar
            window->draw(statusText);
            if (frameLockCB.isChecked()) {
                realtimeCB.draw(*window);
            }
            if (settings.preferences.flashMode) {
                previewCompositeCB.draw(*window);
            }
            dirtyRectCB.draw(*window);
            
            window->display();
        }
        
        // Clear flash export status after a few seconds
        static sf::Clock flashStatusClock;
        if (!flashExportStatus.empty()) {
            if (flashStatusClock.getElapsedTime().asSeconds() > 3.0f) {
                flashExportStatus.clear();
            }
        } else {
            flashStatusClock.restart();
        }

        if (!window.has_value()) {
            // Sleep briefly to avoid busy loop when window is not open. When the window is open, this is handled by the framerate limit
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    }

    if (pausedAutoConnect) {
        settings.preferences.autoConnect = true; // Re-enable AutoConnect if we paused it for manual disconnect
    }
    settings.save();
    
    // Clean shutdown
    if (connectThread.joinable()) {
        connectThread.join();
    }
    if (connected) {
        sender.stop();
        connection.disconnect();
    }
    
    
    return 0;
}