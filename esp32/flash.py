# flash.py - Flash mode layer management

import displayio
import os
import gc
import math
import time

try:
    import gifio
except ImportError:
    gifio = None

from config import get_config, get_config_bool
from rgb565 import load_r565_image

# Flash mode constants
# FLASH_ASSETS_DIR = "/flash_assets"
# TRANSPARENT_COLOR = 0xF81F  # Magenta

# Flag bits
# FLAG_CPU_WARM = 0x01
# FLAG_CPU_HOT = 0x02
# FLAG_WEATHER_AVAIL = 0x04
# FLAG_TRAIN0_AVAIL = 0x08
# FLAG_TRAIN1_AVAIL = 0x10


class FlashModeManager:
    """Manages flash mode layers and rendering."""
    
    def __init__(self, config, frame_width, frame_height):
        self.config = config
        self.frame_width = frame_width
        self.frame_height = frame_height
        self.enabled = False
        
        # Layer groups
        self.bg_tilegrid = None
        self.bg_gif = None
        self.char_tilegrid = None
        self.char_gif = None
        self.char_warm_tilegrid = None
        self.char_warm_gif = None
        self.char_hot_tilegrid = None
        self.char_hot_gif = None
        self.weather_tilegrids = {}  # index -> tilegrid
        self.weather_gifs = {}       # index -> gif object (optional)
        self.stream_bitmap = None
        self.stream_tilegrid = None
        
        # Animation state
        self.char_base_x = 0
        self.char_base_y = 0
        self.weather_base_x = 0
        self.weather_base_y = 0
        self.last_char_state = 0  # 0=normal, 1=warm, 2=hot
        self.last_weather_index = -1
        
        # Bobbing timing
        self.bob_start_time = time.monotonic()
    
    def load_assets(self):
        """Load all configured assets from flash."""
        print("Loading flash assets...")
        gc.collect()
        
        assets_dir = "/flash_assets"
        
        # Check for ENABLED marker
        if not self._file_exists(f"{assets_dir}/ENABLED"):
            print("Flash mode not enabled (no ENABLED marker)")
            return False
        
        # Load background
        if get_config_bool(self.config, 'bg_enabled'):
            bg_file = get_config(self.config, 'bg_file', '')
            if bg_file:
                if bg_file.endswith('.gif'):
                    self.bg_gif = self._load_gif(f"{assets_dir}/{bg_file}")
                    if self.bg_gif:
                        self.bg_tilegrid = displayio.TileGrid(
                            self.bg_gif.bitmap,
                            pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED)
                        )
                else:
                    result = load_r565_image(f"{assets_dir}/{bg_file}")
                    if result:
                        bitmap, w, h = result
                        self.bg_tilegrid = displayio.TileGrid(
                            bitmap,
                            pixel_shader=displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
                        )
                print(f"  Background: {'OK' if self.bg_tilegrid else 'FAILED'}")
        
        # Load character (normal)
        if get_config_bool(self.config, 'char_enabled'):
            char_file = get_config(self.config, 'char_file', '')
            if char_file:
                self._load_character_asset(assets_dir, char_file, 'normal')
            
            # Load warm variant
            if get_config_bool(self.config, 'char_has_warm'):
                warm_file = get_config(self.config, 'char_warm_file', '')
                if warm_file:
                    self._load_character_asset(assets_dir, warm_file, 'warm')
            
            # Load hot variant
            if get_config_bool(self.config, 'char_has_hot'):
                hot_file = get_config(self.config, 'char_hot_file', '')
                if hot_file:
                    self._load_character_asset(assets_dir, hot_file, 'hot')
            
            self.char_base_x = get_config(self.config, 'char_x', 0)
            self.char_base_y = get_config(self.config, 'char_y', 0)
            print(f"  Character: normal={'OK' if self.char_tilegrid else 'NONE'}, "
                  f"warm={'OK' if self.char_warm_tilegrid else 'NONE'}, "
                  f"hot={'OK' if self.char_hot_tilegrid else 'NONE'}")
        
        # Load weather icons
        if get_config_bool(self.config, 'weather_enabled'):
            weather_names = ['sunny', 'cloudy', 'rainy', 'thunderstorm', 'foggy', 'windy', 'night']
            for i, name in enumerate(weather_names):
                # Check config for file (could be .gif or .r565)
                file_key = f'weather_{name}_file'
                filename = get_config(self.config, file_key, f'weather_{name}.r565')
                filepath = f"{assets_dir}/{filename}"
                
                if not self._file_exists(filepath):
                    continue
                
                wx = int(get_config(self.config, 'weather_x', 0))
                wy = int(get_config(self.config, 'weather_y', 0))
                
                if filename.endswith('.gif'):
                    gif = self._load_gif(filepath)
                    if gif:
                        shader = displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED)
                        shader.make_transparent(0xF81F)
                        tg = displayio.TileGrid(gif.bitmap, pixel_shader=shader, x=wx, y=wy)
                        self.weather_tilegrids[i] = tg
                        self.weather_gifs[i] = gif
                else:
                    result = load_r565_image(filepath)
                    if result:
                        bitmap, w, h = result
                        shader = displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
                        shader.make_transparent(0xF81F)
                        tg = displayio.TileGrid(bitmap, pixel_shader=shader, x=wx, y=wy)
                        self.weather_tilegrids[i] = tg
                        
            self.weather_base_x = get_config(self.config, 'weather_x', 0)
            self.weather_base_y = get_config(self.config, 'weather_y', 0)
            print(f"  Weather icons: {len(self.weather_tilegrids)} loaded ({len(self.weather_gifs)} animated)")
        
        # Create streaming layer for non-flashed content
        # Use ColorConverter with transparent magenta (0xF81F) so flashed layers show through
        self.stream_bitmap = displayio.Bitmap(self.frame_width, self.frame_height, 65535)
        # Fill with magenta (transparent) - 0xF81F in little-endian is 0x1F, 0xF8
        # TRANSPARENT_COLOR = 0xF81F
        stream_bytes = memoryview(self.stream_bitmap).cast('B')
        for i in range(0, len(stream_bytes), 2):
            stream_bytes[i] = 0x1F      # Low byte
            stream_bytes[i + 1] = 0xF8  # High byte
        stream_shader = displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
        stream_shader.make_transparent(0xF81F)  # Magenta = transparent
        self.stream_tilegrid = displayio.TileGrid(
            self.stream_bitmap,
            pixel_shader=stream_shader
        )
        
        gc.collect()
        print(f"Flash assets loaded. Free memory: {gc.mem_free()}")
        self.enabled = True
        return True
    
    def _load_character_asset(self, assets_dir, filename, variant):
        """Load a character asset (GIF or RGB565) with magenta transparency."""
        filepath = f"{assets_dir}/{filename}"
        if filename.endswith('.gif'):
            gif = self._load_gif(filepath)
            if gif:
                # Use ColorConverter with magenta transparency
                shader = displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565_SWAPPED)
                shader.make_transparent(0xF81F)  # Magenta = transparent
                tg = displayio.TileGrid(
                    gif.bitmap,
                    pixel_shader=shader,
                    x=int(get_config(self.config, 'char_x', 0)),
                    y=int(get_config(self.config, 'char_y', 0))
                )
                if variant == 'normal':
                    self.char_gif = gif
                    self.char_tilegrid = tg
                elif variant == 'warm':
                    self.char_warm_gif = gif
                    self.char_warm_tilegrid = tg
                elif variant == 'hot':
                    self.char_hot_gif = gif
                    self.char_hot_tilegrid = tg
        else:
            result = load_r565_image(filepath)
            if result:
                bitmap, w, h = result
                # Use ColorConverter with magenta transparency
                shader = displayio.ColorConverter(input_colorspace=displayio.Colorspace.RGB565)
                shader.make_transparent(0xF81F)  # Magenta = transparent
                tg = displayio.TileGrid(
                    bitmap,
                    pixel_shader=shader,
                    x=int(get_config(self.config, 'char_x', 0)),
                    y=int(get_config(self.config, 'char_y', 0))
                )
                if variant == 'normal':
                    self.char_tilegrid = tg
                elif variant == 'warm':
                    self.char_warm_tilegrid = tg
                elif variant == 'hot':
                    self.char_hot_tilegrid = tg
    
    def _load_gif(self, filepath):
        """Load a GIF file."""
        if gifio is None:
            print("gifio not available")
            return None
        try:
            return gifio.OnDiskGif(filepath)
        except Exception as e:
            print(f"Failed to load GIF {filepath}: {e}")
            return None
    
    def _file_exists(self, path):
        """Check if file exists."""
        try:
            os.stat(path)
            return True
        except OSError:
            return False
    
    def build_display_group(self, group):
        """Build the display group with all layers in correct order.
        
        Layer order (bottom to top):
        1. Background (if flashed)
        2. Stream layer (for streamed content)
        3. Character (if flashed) - on top of stream so character shows over streamed bg
        4. Weather icons (added/removed dynamically)
        """
        # Clear existing
        while len(group) > 0:
            group.pop()
        
        # Add layers in order
        if self.bg_tilegrid:
            group.append(self.bg_tilegrid)
        
        if self.stream_tilegrid:
            group.append(self.stream_tilegrid)
        
        if self.char_tilegrid:
            group.append(self.char_tilegrid)
        
        # Weather will be added/removed dynamically (on top)
    
    def get_bob_offset(self, speed, amplitude):
        """Calculate current bobbing offset."""
        elapsed = time.monotonic() - self.bob_start_time
        return math.sin(elapsed * speed * 2.0 * 3.14159) * amplitude
    
    def update_bobbing(self):
        """Update bobbing positions for character.
        
        After 90 degree rotation, the X axis on ESP32 corresponds to vertical
        movement on the physical display, so we bob on X not Y.
        """
        # Character bobbing (on X axis for vertical movement on rotated display)
        if get_config_bool(self.config, 'char_bob'):
            speed = get_config(self.config, 'char_bob_speed', 1.0)
            amp = get_config(self.config, 'char_bob_amp', 5.0)
            offset = int(self.get_bob_offset(speed, amp))
            
            for tg in [self.char_tilegrid, self.char_warm_tilegrid, self.char_hot_tilegrid]:
                if tg:
                    tg.x = int(self.char_base_x) + offset
    
    def update_character_state(self, group, flags):
        """Update character based on temperature state."""
        new_state = 0  # normal
        if flags & 0x02:
            new_state = 2
        elif flags & 0x01:
            new_state = 1
        
        if new_state == self.last_char_state:
            return
        
        # Get the tilegrid for the new state
        old_tg = None
        new_tg = None
        
        if self.last_char_state == 0:
            old_tg = self.char_tilegrid
        elif self.last_char_state == 1:
            old_tg = self.char_warm_tilegrid
        else:
            old_tg = self.char_hot_tilegrid
        
        if new_state == 0:
            new_tg = self.char_tilegrid
        elif new_state == 1:
            new_tg = self.char_warm_tilegrid or self.char_tilegrid
        else:
            new_tg = self.char_hot_tilegrid or self.char_warm_tilegrid or self.char_tilegrid
        
        # Swap in group
        if old_tg and old_tg in group:
            idx = group.index(old_tg)
            group[idx] = new_tg
        
        self.last_char_state = new_state
    
    def update_weather_icon(self, group, icon_index):
        """Update displayed weather icon."""
        if icon_index == self.last_weather_index:
            return
        
        # Remove old weather icon if present
        if self.last_weather_index >= 0 and self.last_weather_index in self.weather_tilegrids:
            old_tg = self.weather_tilegrids[self.last_weather_index]
            if old_tg in group:
                group.remove(old_tg)
        
        # Add new weather icon
        if icon_index >= 0 and icon_index < 7 and icon_index in self.weather_tilegrids:
            new_tg = self.weather_tilegrids[icon_index]
            # Insert before stream layer (which should be last)
            if self.stream_tilegrid in group:
                idx = group.index(self.stream_tilegrid)
                group.insert(idx, new_tg)
            else:
                group.append(new_tg)
        
        self.last_weather_index = icon_index
    
    def advance_animations(self):
        """Advance GIF animations."""
        if self.bg_gif:
            self.bg_gif.next_frame()
        
        # Advance the currently active character
        if self.last_char_state == 0 and self.char_gif:
            self.char_gif.next_frame()
        elif self.last_char_state == 1 and self.char_warm_gif:
            self.char_warm_gif.next_frame()
        elif self.last_char_state == 2 and self.char_hot_gif:
            self.char_hot_gif.next_frame()
        
        # Advance the currently active weather icon
        if self.last_weather_index >= 0 and self.last_weather_index in self.weather_gifs:
            self.weather_gifs[self.last_weather_index].next_frame()