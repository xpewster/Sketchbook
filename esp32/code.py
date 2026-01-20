from displayio import release_displays
release_displays()

import displayio
import busio
import board
import dotclockframebuffer
from framebufferio import FramebufferDisplay
import time
import os
import wifi
import socketpool
import microcontroller

try:
    import gifio
except ImportError:
    gifio = None

from config import parse_config
from flash import FlashModeManager
import network
from network import MODE_FULL_STREAMING, MODE_FLASH

# ==============================
# Mode Persistence (NVM)
# ==============================

# NVM layout:
# Byte 0: Mode magic marker (0xAB = valid mode stored)
# Byte 1: Mode value (MODE_FULL_STREAMING or MODE_FLASH)
NVM_MODE_MAGIC = 0xAB
NVM_MODE_OFFSET = 0
NVM_MODE_VALUE_OFFSET = 1


def load_saved_mode():
    """Load the saved mode from NVM. Returns MODE_FULL_STREAMING if none saved."""
    try:
        if microcontroller.nvm[NVM_MODE_OFFSET] == NVM_MODE_MAGIC:
            mode = microcontroller.nvm[NVM_MODE_VALUE_OFFSET]
            if mode in (MODE_FULL_STREAMING, MODE_FLASH):
                print(f"Loaded saved mode: {'flash' if mode == MODE_FLASH else 'streaming'}")
                return mode
    except Exception as e:
        print(f"Failed to load saved mode: {e}")
    return MODE_FULL_STREAMING


def save_mode(mode):
    """Save the current mode to NVM."""
    try:
        microcontroller.nvm[NVM_MODE_OFFSET] = NVM_MODE_MAGIC
        microcontroller.nvm[NVM_MODE_VALUE_OFFSET] = mode
        print(f"Saved mode: {'flash' if mode == MODE_FLASH else 'streaming'}")
    except Exception as e:
        print(f"Failed to save mode: {e}")


# ==============================
# TFT Setup
# ==============================

tft_pins = dict(board.TFT_PINS)
tft_timings = {
    "frequency": 16000000,
    "width": 240,
    "height": 960,
    "overscan_left": 120,
    "hsync_pulse_width": 8,
    "hsync_back_porch": 20,
    "hsync_front_porch": 20,
    "hsync_idle_low": False,
    "vsync_pulse_width": 8,
    "vsync_back_porch": 20,
    "vsync_front_porch": 20,
    "vsync_idle_low": False,
    "pclk_active_high": True,
    "pclk_idle_high": False,
    "de_idle_high": False,
}

init_sequence_hd371 = bytes((
    b'\xff\x05w\x01\x00\x00\x13'
    b'\xef\x01\x08'
    b'\xff\x05w\x01\x00\x00\x10'
    b'\xc0\x02w\x00'
    b'\xc1\x02\x11\x0c'
    b'\xc2\x02\x07\x02'
    b'\xcc\x010'
    b'\xb0\x10\x06\xcf\x14\x0c\x0f\x03\x00\n\x07\x1b\x03\x12\x10%6\x1e'
    b'\xb1\x10\x0c\xd4\x18\x0c\x0e\x06\x03\x06\x08#\x06\x12\x100/\x1f'
    b'\xff\x05w\x01\x00\x00\x11'
    b'\xb0\x01s'
    b'\xb1\x01|'
    b'\xb2\x01\x83'
    b'\xb3\x01\x80'
    b'\xb5\x01I'
    b'\xb7\x01\x87'
    b'\xb8\x013'
    b'\xb9\x02\x10\x1f'
    b'\xbb\x01\x03'
    b'\xc1\x01\x08'
    b'\xc2\x01\x08'
    b'\xd0\x01\x88'
    b'\xe0\x06\x00\x00\x02\x00\x00\x0c'
    b'\xe1\x0b\x05\x96\x07\x96\x06\x96\x08\x96\x00DD'
    b'\xe2\x0c\x00\x00\x03\x03\x00\x00\x02\x00\x00\x00\x02\x00'
    b'\xe3\x04\x00\x0033'
    b'\xe4\x02DD'
    b'\xe5\x10\r\xd4(\x8c\x0f\xd6(\x8c\t\xd0(\x8c\x0b\xd2(\x8c'
    b'\xe6\x04\x00\x0033'
    b'\xe7\x02DD'
    b'\xe8\x10\x0e\xd5(\x8c\x10\xd7(\x8c\n\xd1(\x8c\x0c\xd3(\x8c'
    b'\xeb\x06\x00\x01\xe4\xe4D\x00'
    b'\xed\x10\xf3\xc1\xba\x0ffwDUUDwf\xf0\xab\x1c?'
    b'\xef\x06\x10\r\x04\x08?\x1f'
    b'\xff\x05w\x01\x00\x00\x13'
    b'\xe8\x02\x00\x0e'
    b'\x11\x80x'
    b'\xe8\x82\x00\x0c\n'
    b'\xe8\x02@\x00'
    b'\xff\x05w\x01\x00\x00\x00'
    b'6\x01\x00'
    b':\x01f'
    b')\x80\x14'
    b'\xff\x05w\x01\x00\x00\x10'
    b'\xe5\x02\x00\x00'
))

board.I2C().deinit()
i2c = busio.I2C(board.SCL, board.SDA, frequency=400_000)
tft_io_expander = dict(board.TFT_IO_EXPANDER)
dotclockframebuffer.ioexpander_send_init_sequence(i2c, init_sequence_hd371, **tft_io_expander)
i2c.deinit()

# Display dimensions
FRAME_WIDTH = 240
FRAME_HEIGHT = 960
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * 2

# Create framebuffer and display
fb = dotclockframebuffer.DotClockFramebuffer(**tft_pins, **tft_timings)
display = FramebufferDisplay(fb, auto_refresh=False)

# Display group
group = displayio.Group()
display.root_group = group

print(f"Display: {FRAME_WIDTH}x{FRAME_HEIGHT}")

# ==============================
# Streaming Mode Buffers
# ==============================

stream_bitmap = displayio.Bitmap(FRAME_WIDTH, FRAME_HEIGHT, 65535)
stream_buffer = memoryview(stream_bitmap).cast('H')
stream_buffer_bytes = memoryview(stream_bitmap).cast('B')

stream_tilegrid = displayio.TileGrid(
    stream_bitmap,
    pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
)

header_buffer = bytearray(256)

# Initialize network handler buffers
network.init_buffers(header_buffer, stream_buffer_bytes, stream_bitmap, FRAME_WIDTH, FRAME_HEIGHT)

# ==============================
# Load Saved Mode
# ==============================

current_mode = load_saved_mode()
print(f"Current mode: {'flash' if current_mode == MODE_FLASH else 'streaming'}")

# ==============================
# Idle/Loading GIF
# ==============================

gif = None
gif_tilegrid = None


def load_gif(path):
    """Try to load a GIF file. Returns (gif, tilegrid) or (None, None)."""
    if gifio is None:
        return None, None
    try:
        g = gifio.OnDiskGif(path)
        tg = displayio.TileGrid(
            g.bitmap,
            pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED)
        )
        return g, tg
    except Exception:
        return None, None


def load_appropriate_gif(mode):
    """Load the appropriate loading GIF based on mode."""
    if mode == MODE_FLASH:
        # Try skin-specific loading GIF for flash mode
        g, tg = load_gif("/flash_assets/loading.gif")
        if g:
            print("Loaded flash mode loading GIF")
            return g, tg
    
    # Fall back to default idle GIF for streaming mode
    g, tg = load_gif("/default/sketchbook.gif")
    if g:
        print("Loaded default idle GIF")
        return g, tg
    
    print("No idle GIF available")
    return None, None


# Load the appropriate GIF based on saved mode
gif, gif_tilegrid = load_appropriate_gif(current_mode)

# Show loading GIF immediately (first frame) before loading flash assets
if gif_tilegrid:
    group.append(gif_tilegrid)
    display.refresh()
    print("Showing loading screen...")

# ==============================
# Flash Mode Initialization
# ==============================

flash_mgr = None
flash_assets_loaded = False


def try_load_flash_assets():
    """Attempt to load flash assets. Returns True if successful."""
    global flash_mgr, flash_assets_loaded
    
    if flash_assets_loaded:
        return True
    
    config = parse_config("/flash_assets/config.txt")
    if config:
        print("Flash config found")
        flash_mgr = FlashModeManager(config, FRAME_WIDTH, FRAME_HEIGHT)
        if flash_mgr.load_assets():
            flash_assets_loaded = True
            print("Flash assets loaded successfully")
            return True
        else:
            print("Flash assets failed to load")
            flash_mgr = None
    else:
        print("No flash config found")
    
    return False


# If saved mode is flash, try to pre-load flash assets
if current_mode == MODE_FLASH:
    try_load_flash_assets()

# ==============================
# WiFi Setup
# ==============================

print("Connecting to WiFi...")
wifi_connected = False
try:
    wifi.radio.connect(
        os.getenv("CIRCUITPY_WIFI_SSID"),
        os.getenv("CIRCUITPY_WIFI_PASSWORD")
    )
    print(f"Connected! IP: {wifi.radio.ipv4_address}")
    wifi_connected = True
except Exception as e:
    print(f"WiFi connection failed: {e}")

pool = None
server_socket = None
TCP_PORT = 8765

if wifi_connected:
    pool = socketpool.SocketPool(wifi.radio)
    server_socket = pool.socket(pool.AF_INET, pool.SOCK_STREAM)
    server_socket.setsockopt(pool.SOL_SOCKET, pool.SO_REUSEADDR, 1)
    server_socket.bind(("0.0.0.0", TCP_PORT))
    server_socket.listen(1)
    server_socket.setblocking(False)
    print(f"TCP server listening on port {TCP_PORT}")

# ==============================
# Mode Switching
# ==============================

def switch_mode(new_mode):
    """Switch to a new mode. Returns True if mode actually changed."""
    global current_mode, gif, gif_tilegrid, flash_mgr, flash_assets_loaded
    
    if new_mode == current_mode:
        return False
    
    print(f"Switching mode: {'flash' if current_mode == MODE_FLASH else 'streaming'} -> {'flash' if new_mode == MODE_FLASH else 'streaming'}")
    
    # Save new mode
    current_mode = new_mode
    save_mode(new_mode)
    
    # If switching to flash mode, ensure assets are loaded
    if new_mode == MODE_FLASH and not flash_assets_loaded:
        if not try_load_flash_assets():
            print("Warning: Flash mode selected but assets failed to load, falling back to streaming")
            current_mode = MODE_FULL_STREAMING
            save_mode(MODE_FULL_STREAMING)
            return True
    
    return True


def setup_connected_display():
    """Set up the display group for connected state based on current mode."""
    global group
    
    display.auto_refresh = False
    
    # Clear display group
    while len(group) > 0:
        group.pop()
    
    if current_mode == MODE_FLASH and flash_assets_loaded and flash_mgr:
        # Build flash mode display
        flash_mgr.build_display_group(group)
        print("Display configured for flash mode")
    else:
        # Show streaming bitmap
        group.append(stream_tilegrid)
        print("Display configured for streaming mode")


def setup_disconnected_display():
    """Set up the display group for disconnected state (show loading GIF)."""
    global group, gif, gif_tilegrid
    
    display.auto_refresh = False
    
    # Clear display group
    while len(group) > 0:
        group.pop()
    
    # Reload appropriate GIF if mode changed
    gif, gif_tilegrid = load_appropriate_gif(current_mode)
    
    if gif_tilegrid:
        group.append(gif_tilegrid)


# ==============================
# Main Loop
# ==============================

print("Starting main loop...")

last_frame_time = time.monotonic()
FRAME_DELAY = 0.10
connected = False
client_socket = None

frame_count = 0
fps_start = time.monotonic()
last_successful_frame_time = 0
disconnected_mode = True

while True:
    # Accept new connection
    if wifi_connected and not connected:
        try:
            client_socket, addr = server_socket.accept()
            client_socket.setblocking(True)
            client_socket.settimeout(5.0)
            connected = True
            disconnected_mode = False
            print(f"Client connected from {addr}")
            
            # Set up display for connected mode
            setup_connected_display()
            
            frame_count = 0
            fps_start = time.monotonic()
        except OSError:
            pass
    
    # Handle connected client
    if connected:
        try:
            if current_mode == MODE_FLASH and flash_assets_loaded and flash_mgr:
                result = network.handle_frame_flash(client_socket, flash_mgr, group)
            else:
                result = network.handle_frame_streaming(client_socket)
            
            # Check for mode change
            if isinstance(result, tuple) and result[0] == 'mode_change':
                new_mode = result[1]
                if switch_mode(new_mode):
                    # Mode changed, reconfigure display
                    setup_connected_display()
                continue
            
            if result:
                # Refresh display
                if not (current_mode == MODE_FLASH and flash_assets_loaded):
                    min_x, min_y, max_x, max_y = network.get_last_dirty_dims()
                    stream_bitmap.dirty(min_x, min_y, max_x, max_y)
                
                display.refresh()
                frame_count += 1
                
                # Report FPS
                now = time.monotonic()
                if now - fps_start >= 5.0:
                    fps = frame_count / (now - fps_start)
                    print(f"FPS: {fps:.1f}")
                    frame_count = 0
                    fps_start = now
                last_successful_frame_time = time.monotonic()
            else:
                print("Client disconnected")
                client_socket.close()
                client_socket = None
                connected = False
                    
        except Exception as e:
            print(f"Error handling client: {e}")
            try:
                client_socket.close()
            except:
                pass
            client_socket = None
            connected = False
            
            # Switch back to loading GIF on error
            setup_disconnected_display()
    
    # Idle animation when not connected - always show loading GIF
    if not connected:
        now = time.monotonic()
        if not disconnected_mode:
            # Only show idle GIF if after sufficient time has passed since last successful frame
            if last_successful_frame_time > 0 and now - last_successful_frame_time < 30.0:
                # Keep display responsive
                flash_mgr.update_bobbing()
                flash_mgr.advance_animations()
                display.refresh()
            else:
                # Switch back to loading GIF
                setup_disconnected_display()
                disconnected_mode = True
        else:
            if now - last_frame_time >= FRAME_DELAY:
                last_frame_time = now
                
                if gif:
                    gif.next_frame()
                    display.refresh()
    
    time.sleep(0.001)