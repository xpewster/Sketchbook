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

try:
    import gifio
except ImportError:
    gifio = None

from config import parse_config
from flash import FlashModeManager
import network

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
i2c = busio.I2C(board.SCL, board.SDA)
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
# Idle/Loading GIF
# ==============================

gif = None
gif_tilegrid = None

def load_gif(path):
    """Try to load a GIF file. Returns (gif, tilegrid) or (None, None)."""
    try:
        g = gifio.OnDiskGif(path)
        tg = displayio.TileGrid(
            g.bitmap,
            pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED)
        )
        return g, tg
    except Exception:
        return None, None

# Try skin-specific loading GIF first, then default
gif, gif_tilegrid = load_gif("/flash_assets/loading.gif")
if gif:
    print("Loaded skin loading GIF")
else:
    gif, gif_tilegrid = load_gif("/test/sketchbook.gif")
    if gif:
        print("Loaded default idle GIF")
    else:
        print("No idle GIF available")

# Show loading GIF immediately (first frame) before loading flash assets
if gif_tilegrid:
    group.append(gif_tilegrid)
    display.refresh()
    print("Showing loading screen...")

# ==============================
# Flash Mode Initialization
# ==============================

flash_mode_enabled = False
flash_mgr = None

config = parse_config("/flash_assets/config.txt")
if config:
    print("Flash config found")
    flash_mgr = FlashModeManager(config, FRAME_WIDTH, FRAME_HEIGHT)
    if flash_mgr.load_assets():
        flash_mode_enabled = True
        print("Flash mode enabled")
    else:
        print("Flash mode disabled, using streaming mode")
        flash_mgr = None

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
# Main Loop
# ==============================

print("Starting main loop...")

last_frame_time = time.monotonic()
FRAME_DELAY = 0.10
connected = False
client_socket = None

frame_count = 0
fps_start = time.monotonic()

while True:
    # Accept new connection
    if wifi_connected and not connected:
        try:
            client_socket, addr = server_socket.accept()
            client_socket.setblocking(True)
            client_socket.settimeout(5.0)
            connected = True
            print(f"Client connected from {addr}")
            
            # Switch from loading GIF to active mode display
            display.auto_refresh = False
            
            # Clear display group (remove loading GIF)
            while len(group) > 0:
                group.pop()
            
            if flash_mode_enabled and flash_mgr:
                # Build flash mode display
                flash_mgr.build_display_group(group)
            else:
                # Show streaming bitmap
                group.append(stream_tilegrid)
            
            frame_count = 0
            fps_start = time.monotonic()
        except OSError:
            pass
    
    # Handle connected client
    if connected:
        try:
            if flash_mode_enabled:
                success = network.handle_frame_flash(client_socket, flash_mgr, group)
            else:
                success = network.handle_frame_streaming(client_socket)
            
            if success:
                # Refresh display
                if not flash_mode_enabled:
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
            else:
                print("Client disconnected")
                client_socket.close()
                client_socket = None
                connected = False
                
                # Switch back to loading GIF
                display.auto_refresh = False
                while len(group) > 0:
                    group.pop()
                if gif_tilegrid:
                    group.append(gif_tilegrid)
                    
        except Exception as e:
            print(f"Error handling client: {e}")
            try:
                client_socket.close()
            except:
                pass
            client_socket = None
            connected = False
            
            # Switch back to loading GIF on error
            display.auto_refresh = False
            while len(group) > 0:
                group.pop()
            if gif_tilegrid:
                group.append(gif_tilegrid)
    
    # Idle animation when not connected - always show loading GIF
    if not connected:
        now = time.monotonic()
        if now - last_frame_time >= FRAME_DELAY:
            last_frame_time = now
            
            if gif:
                gif.next_frame()
                display.refresh()
    
    time.sleep(0.001)