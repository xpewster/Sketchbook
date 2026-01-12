# rgb565.py - RGB565 image loading utilities

import displayio
import struct


def load_r565_image(filepath):
    """Load raw RGB565 image file. Returns (bitmap, width, height) or None."""
    try:
        with open(filepath, 'rb') as f:
            # Read header: width (2 bytes), height (2 bytes), little-endian
            header = f.read(4)
            if len(header) < 4:
                return None
            width, height = struct.unpack('<HH', header)
            
            # Create bitmap
            bitmap = displayio.Bitmap(width, height, 65535)
            
            # Read pixels
            for y in range(height):
                for x in range(width):
                    pixel_data = f.read(2)
                    if len(pixel_data) < 2:
                        break
                    pixel = struct.unpack('<H', pixel_data)[0]
                    bitmap[x, y] = pixel
            
            return bitmap, width, height
    except OSError as e:
        print(f"Failed to load {filepath}: {e}")
        return None