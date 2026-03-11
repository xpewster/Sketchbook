#pragma once
// storage.h - USB Mass Storage + FAT filesystem
// Always-on USB MSC exposing internal flash FAT partition.
// When PC mounts the drive, ESP32 VFS is temporarily unavailable.

#include <stdbool.h>

// Mount point for the FAT partition (used in file paths)
#define FLASH_MOUNT_POINT "/flash"
#define FLASH_ASSETS_DIR  FLASH_MOUNT_POINT "/flash_assets"
#define FLASH_DEFAULT_ASSETS_DIR FLASH_MOUNT_POINT "/default"
#define FLASH_CONFIG_FILE FLASH_ASSETS_DIR "/config.txt"
#define FLASH_DEFAULT_LOADING_GIF FLASH_DEFAULT_ASSETS_DIR "/loading.gif"
#define FLASH_SCHEDULE_FILE      FLASH_MOUNT_POINT "/schedule.txt"

// Initialize FAT partition + USB MSC.
// After this call, files are accessible at FLASH_MOUNT_POINT.
// USB MSC runs in the background - PC can mount the drive anytime.
bool storage_init();

// Check if the filesystem is currently accessible (not mounted by USB host).
bool storage_available();

// Block until filesystem becomes available (with timeout_ms).
// Returns true if available, false on timeout.
bool storage_wait_available(int timeout_ms);
