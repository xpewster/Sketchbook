# network.py - Network and protocol handling

import struct

# Protocol constants
MSG_FULL_FRAME = 0x00
MSG_DIRTY_RECTS = 0x01
MSG_NO_CHANGE = 0x02
MSG_FLASH_DATA = 0x03
MSG_RESET = 0x04
MSG_SET_MODE = 0x05  # Mode selection message

# Mode constants
MODE_FULL_STREAMING = 0x00
MODE_FLASH = 0x01

# Shared buffers (to be set by main code)
header_buffer = None
stream_buffer_bytes = None
stream_bitmap = None
last_dirty_dims = (0, 0, 0, 0)


def init_buffers(hdr_buf, stream_bytes, stream_bmp, frame_w, frame_h):
    """Initialize shared buffers."""
    global header_buffer, stream_buffer_bytes, stream_bitmap
    global FRAME_WIDTH, FRAME_HEIGHT, FRAME_BYTES
    header_buffer = hdr_buf
    stream_buffer_bytes = stream_bytes
    stream_bitmap = stream_bmp
    FRAME_WIDTH = frame_w
    FRAME_HEIGHT = frame_h
    FRAME_BYTES = frame_w * frame_h * 2


def recv_exact(client, buffer, count):
    """Receive exactly count bytes into buffer."""
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


def send_ack(client):
    """Send ACK byte. Returns True on success."""
    try:
        client.send(b'\x00')
        return True
    except OSError:
        return False


def receive_full_frame(client):
    """Receive a full frame directly into bitmap buffer."""
    global last_dirty_dims
    total = 0
    while total < FRAME_BYTES:
        try:
            n = client.recv_into(stream_buffer_bytes[total:])
            if n == 0:
                return False
            total += n
        except OSError as e:
            print(f"Receive error: {e}")
            return False
    last_dirty_dims = (0, 0, FRAME_WIDTH, FRAME_HEIGHT)
    return True


def receive_dirty_rects(client, rect_count, target_buffer_bytes=None, target_bitmap=None, skip_transparent=False):
    """Receive dirty rectangles and update bitmap.
    
    If skip_transparent is True, pixels matching 0xF81F (magenta) are not written,
    allowing layers below to show through.
    """
    global last_dirty_dims
    
    if target_buffer_bytes is None:
        target_buffer_bytes = stream_buffer_bytes
    if target_bitmap is None:
        target_bitmap = stream_bitmap
    
    header_size = rect_count * 8
    if not recv_exact(client, header_buffer, header_size):
        return False

    min_x = FRAME_WIDTH
    min_y = FRAME_HEIGHT
    max_x = 0
    max_y = 0
    
    # Temporary buffer for receiving pixel data when filtering
    # TRANSPARENT_COLOR = 0xF81F (magenta)
    temp_row_buffer = bytearray(FRAME_WIDTH * 2) if skip_transparent else None
    
    for i in range(rect_count):
        offset = i * 8
        x = header_buffer[offset] | (header_buffer[offset + 1] << 8)
        y = header_buffer[offset + 2] | (header_buffer[offset + 3] << 8)
        w = header_buffer[offset + 4] | (header_buffer[offset + 5] << 8)
        h = header_buffer[offset + 6] | (header_buffer[offset + 7] << 8)

        min_x = min(min_x, x)
        min_y = min(min_y, y)
        max_x = max(max_x, x + w)
        max_y = max(max_y, y + h)

        for row in range(h):
            row_start = (y + row) * FRAME_WIDTH + x
            row_bytes = w * 2
            byte_offset = row_start * 2

            if skip_transparent:
                # Receive into temp buffer, then filter
                if not recv_exact(client, temp_row_buffer[:row_bytes], row_bytes):
                    return False
                # Copy non-transparent pixels only
                for px in range(w):
                    px_offset = px * 2
                    # Read pixel as little-endian uint16
                    pixel = temp_row_buffer[px_offset] | (temp_row_buffer[px_offset + 1] << 8)
                    # Skip magenta (0xF81F) - transparent color key
                    if pixel != 0xF81F:
                        target_buffer_bytes[byte_offset + px_offset] = temp_row_buffer[px_offset]
                        target_buffer_bytes[byte_offset + px_offset + 1] = temp_row_buffer[px_offset + 1]
            else:
                # Direct receive into target buffer
                if not recv_exact(client, target_buffer_bytes[byte_offset:byte_offset + row_bytes], row_bytes):
                    return False
            
    last_dirty_dims = (min_x, min_y, max_x, max_y)
    return True


def receive_flash_data(client, flash_mgr):
    """Receive flash mode data packet (simplified protocol)."""
    global last_dirty_dims
    
    # Read fixed header (15 bytes after msg_type)
    # weather_index(1) + flags(1) + cpu%(2) + cpuT(2) + mem%(2) + weatherT(2) + train0_mins(2) + train1_mins(2) + rect_count(1) = 15 bytes
    if not recv_exact(client, header_buffer, 15):
        print("Failed to receive full flash frame header")
        return None
    
    weather_index = header_buffer[0]
    flags = header_buffer[1]
    cpu_percent = (header_buffer[2] | (header_buffer[3] << 8)) / 10.0
    cpu_temp = (header_buffer[4] | (header_buffer[5] << 8)) / 10.0
    mem_percent = (header_buffer[6] | (header_buffer[7] << 8)) / 10.0
    weather_temp = struct.unpack('<h', bytes(header_buffer[8:10]))[0] / 10.0
    train0_mins = (header_buffer[10] | (header_buffer[11] << 8)) / 10.0
    train1_mins = (header_buffer[12] | (header_buffer[13] << 8)) / 10.0
    rect_count = header_buffer[14]
    
    # Receive dirty rects into flash manager's stream bitmap
    # Magenta (0xF81F) pixels become transparent via ColorConverter.make_transparent()
    if rect_count > 0:
        target_bytes = memoryview(flash_mgr.stream_bitmap).cast('B')
        if not receive_dirty_rects(client, rect_count, target_bytes, flash_mgr.stream_bitmap):
            print("Failed to receive flash frame dirty rects")
            return None
    
    return {
        'weather_index': weather_index,
        'flags': flags,
        'cpu_percent': cpu_percent,
        'cpu_temp': cpu_temp,
        'mem_percent': mem_percent,
        'weather_temp': weather_temp,
        'train0_mins': train0_mins,
        'train1_mins': train1_mins,
        'rect_count': rect_count
    }


def handle_common_message(client, msg_type):
    """Handle message types common to both streaming and flash modes.
    
    Returns:
        True: Message handled successfully
        False: Error or disconnect
        ('mode_change', mode): Mode change requested
        None: Message type not handled (caller should handle it)
    """
    if msg_type == MSG_FULL_FRAME:
        if not receive_full_frame(client):
            return False
        if not send_ack(client):
            return False
        return True
    
    elif msg_type == MSG_DIRTY_RECTS:
        if not recv_exact(client, header_buffer, 1):
            return False
        rect_count = header_buffer[0]
        if rect_count > 0:
            if not receive_dirty_rects(client, rect_count):
                return False
        if not send_ack(client):
            return False
        return True
    
    elif msg_type == MSG_NO_CHANGE:
        if not send_ack(client):
            return False
        return True
    
    elif msg_type == MSG_SET_MODE:
        if not recv_exact(client, header_buffer, 1):
            return False
        mode = header_buffer[0]
        if not send_ack(client):
            return False
        return ('mode_change', mode)
    
    elif msg_type == MSG_RESET:
        send_ack(client)  # Best effort
        import microcontroller
        microcontroller.reset()
        return False  # Won't reach here
    
    # Unknown/unhandled message type
    return None


def handle_frame_streaming(client):
    """Handle streaming mode frame. Returns True on success, 'mode_change' tuple on mode switch."""
    if not recv_exact(client, header_buffer, 1):
        return False
    
    msg_type = header_buffer[0]
    
    result = handle_common_message(client, msg_type)
    if result is not None:
        return result
    
    print(f"Unknown message type: {msg_type}")
    return False


def handle_frame_flash(client, flash_mgr, group):
    """Handle flash mode frame. Returns True on success, 'mode_change' tuple on mode switch."""
    global last_dirty_dims
    
    if not recv_exact(client, header_buffer, 1):
        print("Failed to receive flash frame header message type")
        return False
    
    msg_type = header_buffer[0]
    
    # Handle flash-specific message
    if msg_type == MSG_FLASH_DATA:
        data = receive_flash_data(client, flash_mgr)
        if data is None:
            print("Failed to receive flash frame data")
            return False
        
        # Update character state
        flash_mgr.update_character_state(group, data['flags'])
        
        # Update weather icon
        if data['flags'] & 0x04:
            flash_mgr.update_weather_icon(group, data['weather_index'])
        else:
            flash_mgr.update_weather_icon(group, -1)
        
        # Update bobbing
        flash_mgr.update_bobbing()
        
        # Advance animations
        flash_mgr.advance_animations()
        
        # Mark dirty region for stream layer
        if data['rect_count'] > 0:
            min_x, min_y, max_x, max_y = last_dirty_dims
            flash_mgr.stream_bitmap.dirty(min_x, min_y, max_x, max_y)
        
        if not send_ack(client):
            print("Failed to send ACK for flash frame")
            return False
        
        return True
    
    # Try common message handler
    result = handle_common_message(client, msg_type)
    if result is not None:
        return result
    
    print(f"Unknown message type: {msg_type}")
    return False


def get_last_dirty_dims():
    """Get the last dirty dimensions."""
    return last_dirty_dims