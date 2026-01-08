# code.py - Qualia display streaming via WiFi
# Runs TCP server, receives RGB565 frames from PC
# Falls back to animation when not connected

from displayio import release_displays
release_displays()

import displayio
import busio
import board
import dotclockframebuffer
from framebufferio import FramebufferDisplay
import struct
import time
import os
import gc
import wifi
import socketpool
import gifio

# TFT setup
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

# ==============================
# Display dimensions
# ==============================
FRAME_WIDTH = 240
FRAME_HEIGHT = 960
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * 2  # RGB565 = 2 bytes per pixel

# Create framebuffer and display
fb = dotclockframebuffer.DotClockFramebuffer(**tft_pins, **tft_timings)
display = FramebufferDisplay(fb, auto_refresh=False)

# Create the display group
group = displayio.Group()
display.root_group = group

print(f"Display: {FRAME_WIDTH}x{FRAME_HEIGHT}")
print(f"Frame size: {FRAME_BYTES} bytes")

# # Streaming buffers (double buffered)
# stream_bitmaps = [
#     displayio.Bitmap(FRAME_WIDTH, FRAME_HEIGHT, 65535),
#     displayio.Bitmap(FRAME_WIDTH, FRAME_HEIGHT, 65535)
# ]
# stream_buffers = [memoryview(bmp).cast('B') for bmp in stream_bitmaps]
# print(f"Bitmap buffers allocated: {stream_bitmaps[0].bits_per_value // 8} bytes per pixel, {FRAME_WIDTH}x{FRAME_HEIGHT} total {len(stream_buffers[0])} bytes each")
# stream_tilegrids = [
#     displayio.TileGrid(
#         bmp,
#         pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
#     )
#     for bmp in stream_bitmaps
# ]
# back_buffer_idx = 0

# ==============================
# Streaming buffer (single buffer for dirty rects)
# ==============================
# Using single buffer since we update in-place with dirty rects
stream_bitmap = displayio.Bitmap(FRAME_WIDTH, FRAME_HEIGHT, 65535)
stream_buffer = memoryview(stream_bitmap).cast('H')  # Cast to 16-bit for easier pixel access
stream_buffer_bytes = memoryview(stream_bitmap).cast('B')  # Byte view for recv_into

stream_tilegrid = displayio.TileGrid(
    stream_bitmap,
    pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
)

print(f"Single bitmap buffer allocated")

# Small buffer for receiving header data
header_buffer = bytearray(256)  # Enough for 32 rects * 8 bytes + 2 byte header

# Flags for direct buffer modification
last_dirty_dims = (0, 0, 0, 0)

# TCP server settings
TCP_PORT = 8765

# ------------------------------
# Animation Loading
# ------------------------------

def load_bmp_to_bitmap(filename):
    """Load a BMP file into a new Bitmap object."""
    with open(filename, 'rb') as f:
        if f.read(2) != b'BM':
            raise ValueError("Not a BMP file")
        
        f.seek(10)
        pixel_offset = struct.unpack('<I', f.read(4))[0]
        
        f.seek(18)
        width = struct.unpack('<I', f.read(4))[0]
        height = struct.unpack('<I', f.read(4))[0]
        
        f.seek(28)
        bits_per_pixel = struct.unpack('<H', f.read(2))[0]
        
        if bits_per_pixel != 24:
            raise ValueError(f"Only 24-bit BMP supported, got {bits_per_pixel}")
        
        frame_bitmap = displayio.Bitmap(width, height, 65535)
        row_size = (width * 3 + 3) & ~3
        
        f.seek(pixel_offset)
        
        for y in range(height - 1, -1, -1):
            row_data = f.read(row_size)
            for x in range(width):
                b = row_data[x * 3]
                g = row_data[x * 3 + 1]
                r = row_data[x * 3 + 2]
                rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                frame_bitmap[x, y] = rgb565
        
        return frame_bitmap


def load_animation():
    """Load all animation frames and create TileGrids for each."""
    tilegrids = []
    anim_dir = "/anim"
    
    try:
        files = sorted(os.listdir(anim_dir))
    except OSError:
        print("No /anim directory found")
        return tilegrids
    
    first_frame = True
    for filename in files:
        if filename.lower().endswith('.bmp'):
            filepath = f"{anim_dir}/{filename}"
            print(f"Loading {filename}...", end=" ")
            gc.collect()
            try:
                frame_bitmap = load_bmp_to_bitmap(filepath)
                tg = displayio.TileGrid(
                    frame_bitmap,
                    pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
                )
                tilegrids.append(tg)
                print(f"OK ({gc.mem_free()} bytes free)")

                if first_frame:
                    print("Displaying first frame...")
                    group.append(tg)  # Show first frame immediately
                    display.auto_refresh = True
                    first_frame = False
                else:
                    display.auto_refresh = False
                    group[0] = tg
                    display.auto_refresh = True
            except MemoryError:
                print("OUT OF MEMORY")
                break
            except Exception as e:
                print(f"Error: {e}")
    
    return tilegrids


# Load animation at startup
# print(f"Free memory before load: {gc.mem_free()}")
# print("Loading animation frames...")
# animation_tilegrids = load_animation()
# print(f"Loaded {len(animation_tilegrids)} frames")
# print(f"Free memory after load: {gc.mem_free()}")
# gc.collect()
print("Loading gif...")
gif = gifio.OnDiskGif("/test/sketchbook.gif")
gif_tilegrid = displayio.TileGrid(gif.bitmap, pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED))
group.append(gif_tilegrid)

# ------------------------------
# WiFi Setup
# ------------------------------

print("Connecting to WiFi...")
try:
    wifi.radio.connect(
        os.getenv("CIRCUITPY_WIFI_SSID"),
        os.getenv("CIRCUITPY_WIFI_PASSWORD")
    )
    print(f"Connected! IP: {wifi.radio.ipv4_address}")
except Exception as e:
    print(f"WiFi connection failed: {e}")
    wifi_connected = False
else:
    wifi_connected = True

pool = None
server_socket = None

if wifi_connected:
    pool = socketpool.SocketPool(wifi.radio)
    
    # Create TCP server
    server_socket = pool.socket(pool.AF_INET, pool.SOCK_STREAM)
    server_socket.setsockopt(pool.SOL_SOCKET, pool.SO_REUSEADDR, 1)
    # server_socket.setsockopt(pool.SOL_SOCKET, pool.TCP_NODELAY, 1)
    server_socket.bind(("0.0.0.0", TCP_PORT))
    server_socket.listen(1)
    server_socket.setblocking(False)
    
    print(f"TCP server listening on port {TCP_PORT}")

# ==============================
# Protocol Constants (must match PC side)
# ==============================
MSG_FULL_FRAME = 0x00
MSG_DIRTY_RECTS = 0x01
MSG_NO_CHANGE = 0x02

# ------------------------------
# Frame Receive  
# ------------------------------

def recv_exact(client, buffer, count):
    """Receive exactly count bytes into buffer. Returns True on success."""
    mv = memoryview(buffer)[:count]
    total = 0
    while total < count:
        try:
            n = client.recv_into(mv[total:])
            if n == 0:
                return False
            total += n
        except OSError as e:
            print(f"Recv error: {e}")
            return False
    return True

def receive_full_frame(client):
    """Receive a full frame directly into bitmap buffer."""
    global last_dirty_dims
    total = 0
    while total < FRAME_BYTES:
        try:
            n = client.recv_into(stream_buffer_bytes[total:])
            if n == 0:
                print("Connection closed")
                return False
            total += n
        except OSError as e:
            print(f"Receive error: {e}")
            return False
    last_dirty_dims = (0, 0, FRAME_WIDTH, FRAME_HEIGHT)
    return True


def receive_dirty_rects(client, rect_count):
    """Receive dirty rectangles and update bitmap."""
    global last_dirty_dims
    # Read all rect headers first
    header_size = rect_count * 8
    if not recv_exact(client, header_buffer, header_size):
        return False

    min_x = FRAME_WIDTH
    min_y = FRAME_HEIGHT
    max_x = 0
    max_y = 0
    
    # Parse rectangles and receive pixel data for each
    for i in range(rect_count):
        offset = i * 8
        x = header_buffer[offset] | (header_buffer[offset + 1] << 8)
        y = header_buffer[offset + 2] | (header_buffer[offset + 3] << 8)
        w = header_buffer[offset + 4] | (header_buffer[offset + 5] << 8)
        h = header_buffer[offset + 6] | (header_buffer[offset + 7] << 8)

        # Update dirty bounds
        min_x = min(min_x, x)
        min_y = min(min_y, y)
        max_x = max(max_x, x + w)
        max_y = max(max_y, y + h)

        # Receive pixels for this rect row by row into bitmap
        for row in range(h):
            row_start = (y + row) * FRAME_WIDTH + x
            row_bytes = w * 2
            
            # Create view into bitmap for this row segment
            # We need to receive into the byte buffer at the right position
            byte_offset = row_start * 2

            if not recv_exact(client, stream_buffer_bytes[byte_offset:byte_offset + row_bytes], row_bytes):
                print("Failed to receive pixels for rect")
                return False
            
    last_dirty_dims = (min_x, min_y, max_x, max_y)
    
    return True


def handle_frame(client):
    """Handle receiving a frame (full or dirty rects). Returns True on success."""
    # Read message type
    if not recv_exact(client, header_buffer, 1):
        return False
    
    msg_type = header_buffer[0]
    
    if msg_type == MSG_FULL_FRAME:
        print("Receiving full frame...")
        return receive_full_frame(client)
    
    elif msg_type == MSG_DIRTY_RECTS:
        # Read rect count
        if not recv_exact(client, header_buffer, 1):
            return False
        rect_count = header_buffer[0]
        
        if rect_count == 0:
            return True  # No rects to update
        
        return receive_dirty_rects(client, rect_count)
    
    elif msg_type == MSG_NO_CHANGE:
        return True  # Nothing to do
    
    else:
        print(f"Unknown message type: {msg_type}")
        return False


# ------------------------------
# Main Loop
# ------------------------------

print("Starting main loop...")

# frame_index = 0
last_frame_time = time.monotonic()
FRAME_DELAY = 0.10
connected = False
client_socket = None

frame_count = 0
fps_start = time.monotonic()
printed_fps = False

while True:
    # Try to accept new connection
    if wifi_connected and not connected:
        try:
            client_socket, addr = server_socket.accept()
            client_socket.setblocking(True)  # Blocking for simpler receive
            client_socket.settimeout(5.0)    # 5 second timeout
            connected = True
            print(f"Client connected from {addr}")
            
            # Switch to streaming display
            # display.auto_refresh = False
            # if animation_tilegrids and group[0] in animation_tilegrids:
            #     group[0] = stream_tilegrids[back_buffer_idx]
            # display.auto_refresh = True
            display.auto_refresh = False
            if gif_tilegrid and len(group) > 0 and group[0] is gif_tilegrid:
                group[0] = stream_tilegrid
            elif len(group) == 0:
                group.append(stream_tilegrid)
            display.auto_refresh = False
            
            frame_count = 0
            fps_start = time.monotonic()
        except OSError:
            pass  # No connection waiting
    
    # Handle connected client
    if connected:
        try:
            if handle_frame(client_socket):
                # Refresh display

                # Mark buffer as dirty
                min_x, min_y, max_x, max_y = last_dirty_dims
                stream_bitmap.dirty(min_x, min_y, max_x, max_y)
                # print(f"Refreshed dirty area: ({min_x}, {min_y}) - ({max_x}, {max_y})")
                display.refresh()
                
                frame_count += 1
                
                # Report FPS every 5 seconds
                now = time.monotonic()
                if now - fps_start >= 5.0:
                    fps = frame_count / (now - fps_start)
                    print(f"FPS: {fps:.1f}")
                    frame_count = 0
                    fps_start = now
            else:
                # Connection lost or error
                print("Client disconnected")
                client_socket.close()
                client_socket = None
                connected = False
                
                # Switch back to GIF
                if gif_tilegrid:
                    display.auto_refresh = False
                    group[0] = gif_tilegrid
                    display.auto_refresh = True

        except Exception as e:
            print(f"Error handling client: {e}")
            try:
                client_socket.close()
            except:
                pass
            client_socket = None
            connected = False

            raise e  # Re-raise to see in console
    
    # Play animation when not connected
    
    # if not connected and animation_tilegrids:
    #     now = time.monotonic()
    #     if now - last_frame_time >= FRAME_DELAY:
    #         last_frame_time = now
            
    #         next_frame = animation_tilegrids[frame_index]
    #         frame_count += 1
    #         if group[0] is not next_frame:
    #             display.auto_refresh = False
    #             group[0] = next_frame
    #             display.auto_refresh = True
            
    #         frame_index = (frame_index + 1) % len(animation_tilegrids)
    #         if (frame_count % 10) == 0:
    #             print(f"FPS: {frame_count / (now - fps_start):.1f}")
    #             frame_count = 0
    #             fps_start = now

    if not connected and gif_tilegrid:
        now = time.monotonic()
        if now - last_frame_time >= FRAME_DELAY:
            last_frame_time = now

            frame_count += 1
            # display.auto_refresh = False
            gif.next_frame()
            # display.auto_refresh = True
            display.refresh()

            if ((not printed_fps) and ((frame_count % 10) == 0)):
                print(f"FPS: {frame_count / (now - fps_start):.1f}")
                frame_count = 0
                fps_start = now
                printed_fps = True
    
    time.sleep(0.001)