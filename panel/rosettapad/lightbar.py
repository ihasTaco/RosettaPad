"""
Lightbar customization system for DualSense controllers.

Supports:
- Static colors
- Breathing/pulse animations
- Rainbow cycle
- Color wave
- Custom patterns
- Battery-reactive colors
- Game event triggers (future)
"""

import json
import asyncio
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional
from enum import Enum


class LightbarMode(Enum):
    """Lightbar display modes."""
    OFF = "off"
    STATIC = "static"
    BREATHING = "breathing"
    RAINBOW = "rainbow"
    WAVE = "wave"
    BATTERY = "battery"
    CUSTOM = "custom"


class EasingFunction(Enum):
    """Animation easing functions."""
    LINEAR = "linear"
    EASE_IN = "ease_in"
    EASE_OUT = "ease_out"
    EASE_IN_OUT = "ease_in_out"
    SINE = "sine"


@dataclass
class Color:
    """RGB color representation."""
    r: int = 0
    g: int = 0
    b: int = 0
    
    def __post_init__(self):
        self.r = max(0, min(255, self.r))
        self.g = max(0, min(255, self.g))
        self.b = max(0, min(255, self.b))
    
    def to_dict(self) -> dict:
        return {"r": self.r, "g": self.g, "b": self.b}
    
    def to_hex(self) -> str:
        return f"#{self.r:02x}{self.g:02x}{self.b:02x}"
    
    def to_tuple(self) -> tuple[int, int, int]:
        return (self.r, self.g, self.b)
    
    @classmethod
    def from_dict(cls, data: dict) -> "Color":
        return cls(
            r=data.get("r", 0),
            g=data.get("g", 0),
            b=data.get("b", 0)
        )
    
    @classmethod
    def from_hex(cls, hex_str: str) -> "Color":
        hex_str = hex_str.lstrip("#")
        if len(hex_str) == 6:
            return cls(
                r=int(hex_str[0:2], 16),
                g=int(hex_str[2:4], 16),
                b=int(hex_str[4:6], 16)
            )
        return cls()
    
    @staticmethod
    def lerp(c1: "Color", c2: "Color", t: float) -> "Color":
        """Linear interpolation between two colors."""
        t = max(0.0, min(1.0, t))
        return Color(
            r=int(c1.r + (c2.r - c1.r) * t),
            g=int(c1.g + (c2.g - c1.g) * t),
            b=int(c1.b + (c2.b - c1.b) * t)
        )


# Preset colors
class PresetColors:
    RED = Color(255, 0, 0)
    GREEN = Color(0, 255, 0)
    BLUE = Color(0, 0, 255)
    CYAN = Color(0, 255, 255)
    MAGENTA = Color(255, 0, 255)
    YELLOW = Color(255, 255, 0)
    ORANGE = Color(255, 128, 0)
    PURPLE = Color(128, 0, 255)
    PINK = Color(255, 105, 180)
    WHITE = Color(255, 255, 255)
    PS_BLUE = Color(0, 48, 135)
    PS_LIGHT = Color(0, 195, 227)


@dataclass
class AnimationKeyframe:
    """A single keyframe in a custom animation."""
    time_ms: int  # Time offset from animation start
    color: Color
    brightness: float = 1.0  # 0.0 to 1.0
    easing: EasingFunction = EasingFunction.LINEAR
    
    def to_dict(self) -> dict:
        return {
            "time_ms": self.time_ms,
            "color": self.color.to_dict(),
            "brightness": self.brightness,
            "easing": self.easing.value
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "AnimationKeyframe":
        return cls(
            time_ms=data.get("time_ms", 0),
            color=Color.from_dict(data.get("color", {})),
            brightness=data.get("brightness", 1.0),
            easing=EasingFunction(data.get("easing", "linear"))
        )


@dataclass
class LightbarAnimation:
    """Custom animation definition."""
    id: str
    name: str
    keyframes: list[AnimationKeyframe] = field(default_factory=list)
    duration_ms: int = 1000
    loop: bool = True
    
    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "keyframes": [k.to_dict() for k in self.keyframes],
            "duration_ms": self.duration_ms,
            "loop": self.loop
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "LightbarAnimation":
        return cls(
            id=data.get("id", ""),
            name=data.get("name", ""),
            keyframes=[AnimationKeyframe.from_dict(k) for k in data.get("keyframes", [])],
            duration_ms=data.get("duration_ms", 1000),
            loop=data.get("loop", True)
        )


@dataclass
class LightbarConfig:
    """Complete lightbar configuration for a profile."""
    mode: LightbarMode = LightbarMode.STATIC
    color: Color = field(default_factory=lambda: Color(0, 0, 255))  # Default PS blue
    brightness: float = 1.0  # 0.0 to 1.0 (master brightness)
    
    # Breathing mode settings
    breathing_speed_ms: int = 2000  # Full cycle duration
    breathing_min_brightness: float = 0.1
    breathing_color2: Optional[Color] = None  # Optional second color for breathing
    
    # Rainbow mode settings
    rainbow_speed_ms: int = 3000  # Full cycle duration
    rainbow_saturation: float = 1.0
    
    # Wave mode settings (color shifts across spectrum)
    wave_speed_ms: int = 2000
    wave_colors: list[Color] = field(default_factory=list)
    
    # Battery mode settings
    battery_high_color: Color = field(default_factory=lambda: Color(0, 255, 0))
    battery_mid_color: Color = field(default_factory=lambda: Color(255, 255, 0))
    battery_low_color: Color = field(default_factory=lambda: Color(255, 0, 0))
    battery_high_threshold: int = 70
    battery_low_threshold: int = 20
    battery_pulse_when_low: bool = True
    
    # Custom animation
    custom_animation_id: Optional[str] = None
    
    # Player LED settings (the 5 LEDs on DualSense)
    player_leds: int = 0  # Bitmask: 0x01=LED1, 0x02=LED2, etc.
    player_led_brightness: float = 1.0
    
    def to_dict(self) -> dict:
        return {
            "mode": self.mode.value,
            "color": self.color.to_dict(),
            "brightness": self.brightness,
            "breathing_speed_ms": self.breathing_speed_ms,
            "breathing_min_brightness": self.breathing_min_brightness,
            "breathing_color2": self.breathing_color2.to_dict() if self.breathing_color2 else None,
            "rainbow_speed_ms": self.rainbow_speed_ms,
            "rainbow_saturation": self.rainbow_saturation,
            "wave_speed_ms": self.wave_speed_ms,
            "wave_colors": [c.to_dict() for c in self.wave_colors],
            "battery_high_color": self.battery_high_color.to_dict(),
            "battery_mid_color": self.battery_mid_color.to_dict(),
            "battery_low_color": self.battery_low_color.to_dict(),
            "battery_high_threshold": self.battery_high_threshold,
            "battery_low_threshold": self.battery_low_threshold,
            "battery_pulse_when_low": self.battery_pulse_when_low,
            "custom_animation_id": self.custom_animation_id,
            "player_leds": self.player_leds,
            "player_led_brightness": self.player_led_brightness
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "LightbarConfig":
        wave_colors = [Color.from_dict(c) for c in data.get("wave_colors", [])]
        if not wave_colors:
            wave_colors = [PresetColors.RED, PresetColors.ORANGE, PresetColors.YELLOW,
                          PresetColors.GREEN, PresetColors.CYAN, PresetColors.BLUE, PresetColors.PURPLE]
        
        return cls(
            mode=LightbarMode(data.get("mode", "static")),
            color=Color.from_dict(data.get("color", {"r": 0, "g": 0, "b": 255})),
            brightness=data.get("brightness", 1.0),
            breathing_speed_ms=data.get("breathing_speed_ms", 2000),
            breathing_min_brightness=data.get("breathing_min_brightness", 0.1),
            breathing_color2=Color.from_dict(data["breathing_color2"]) if data.get("breathing_color2") else None,
            rainbow_speed_ms=data.get("rainbow_speed_ms", 3000),
            rainbow_saturation=data.get("rainbow_saturation", 1.0),
            wave_speed_ms=data.get("wave_speed_ms", 2000),
            wave_colors=wave_colors,
            battery_high_color=Color.from_dict(data.get("battery_high_color", {"r": 0, "g": 255, "b": 0})),
            battery_mid_color=Color.from_dict(data.get("battery_mid_color", {"r": 255, "g": 255, "b": 0})),
            battery_low_color=Color.from_dict(data.get("battery_low_color", {"r": 255, "g": 0, "b": 0})),
            battery_high_threshold=data.get("battery_high_threshold", 70),
            battery_low_threshold=data.get("battery_low_threshold", 20),
            battery_pulse_when_low=data.get("battery_pulse_when_low", True),
            custom_animation_id=data.get("custom_animation_id"),
            player_leds=data.get("player_leds", 0),
            player_led_brightness=data.get("player_led_brightness", 1.0)
        )


class LightbarManager:
    """
    Manages lightbar state and animations.
    
    This manager runs animation loops and communicates with the C adapter
    via a simple IPC mechanism (shared file or socket).
    """
    
    PRESET_ANIMATIONS: dict[str, LightbarAnimation] = {
        "pulse_slow": LightbarAnimation(
            id="pulse_slow",
            name="Slow Pulse",
            keyframes=[
                AnimationKeyframe(0, Color(255, 255, 255), 1.0, EasingFunction.EASE_IN_OUT),
                AnimationKeyframe(1500, Color(255, 255, 255), 0.2, EasingFunction.EASE_IN_OUT),
                AnimationKeyframe(3000, Color(255, 255, 255), 1.0, EasingFunction.EASE_IN_OUT),
            ],
            duration_ms=3000,
            loop=True
        ),
        "pulse_fast": LightbarAnimation(
            id="pulse_fast",
            name="Fast Pulse",
            keyframes=[
                AnimationKeyframe(0, Color(255, 255, 255), 1.0, EasingFunction.LINEAR),
                AnimationKeyframe(250, Color(255, 255, 255), 0.2, EasingFunction.LINEAR),
                AnimationKeyframe(500, Color(255, 255, 255), 1.0, EasingFunction.LINEAR),
            ],
            duration_ms=500,
            loop=True
        ),
        "police": LightbarAnimation(
            id="police",
            name="Police Lights",
            keyframes=[
                AnimationKeyframe(0, Color(255, 0, 0), 1.0),
                AnimationKeyframe(100, Color(255, 0, 0), 0.0),
                AnimationKeyframe(200, Color(0, 0, 255), 1.0),
                AnimationKeyframe(300, Color(0, 0, 255), 0.0),
                AnimationKeyframe(400, Color(255, 0, 0), 1.0),
            ],
            duration_ms=400,
            loop=True
        ),
        "fire": LightbarAnimation(
            id="fire",
            name="Fire Flicker",
            keyframes=[
                AnimationKeyframe(0, Color(255, 50, 0), 1.0, EasingFunction.EASE_OUT),
                AnimationKeyframe(100, Color(255, 100, 0), 0.8, EasingFunction.EASE_IN),
                AnimationKeyframe(200, Color(255, 30, 0), 0.9, EasingFunction.EASE_OUT),
                AnimationKeyframe(350, Color(255, 80, 0), 0.7, EasingFunction.EASE_IN),
                AnimationKeyframe(500, Color(255, 50, 0), 1.0),
            ],
            duration_ms=500,
            loop=True
        ),
    }
    
    def __init__(self, filepath: Path, ipc_path: Optional[Path] = None):
        self.filepath = filepath
        self.ipc_path = ipc_path or filepath.parent / "lightbar_state.json"
        self.custom_animations: dict[str, LightbarAnimation] = {}
        self.current_config: LightbarConfig = LightbarConfig()
        self.current_battery: int = 100
        self._animation_task: Optional[asyncio.Task] = None
        self._running: bool = False
        self._ensure_data_dir()
        self._load()
    
    def _ensure_data_dir(self) -> None:
        self.filepath.parent.mkdir(parents=True, exist_ok=True)
    
    def _load(self) -> None:
        """Load custom animations from disk."""
        if not self.filepath.exists():
            return
        try:
            with open(self.filepath, "r") as f:
                data = json.load(f)
                for anim_data in data.get("custom_animations", []):
                    anim = LightbarAnimation.from_dict(anim_data)
                    self.custom_animations[anim.id] = anim
        except (json.JSONDecodeError, IOError) as e:
            print(f"Warning: Could not load lightbar animations: {e}")
    
    def _save(self) -> None:
        """Save custom animations to disk."""
        data = {
            "custom_animations": [a.to_dict() for a in self.custom_animations.values()]
        }
        try:
            with open(self.filepath, "w") as f:
                json.dump(data, f, indent=2)
        except IOError as e:
            print(f"Warning: Could not save lightbar animations: {e}")
    
    def _write_ipc_state(self, r: int, g: int, b: int, player_leds: int = 0) -> None:
        """Write current lightbar state for the C adapter to read."""
        state = {
            "r": r,
            "g": g,
            "b": b,
            "player_leds": player_leds,
            "player_led_brightness": self.current_config.player_led_brightness
        }
        try:
            with open(self.ipc_path, "w") as f:
                json.dump(state, f)
        except IOError:
            pass
    
    def get_all_animations(self) -> list[LightbarAnimation]:
        """Get all animations (presets + custom)."""
        return list(self.PRESET_ANIMATIONS.values()) + list(self.custom_animations.values())
    
    def get_animation(self, anim_id: str) -> Optional[LightbarAnimation]:
        """Get an animation by ID."""
        if anim_id in self.PRESET_ANIMATIONS:
            return self.PRESET_ANIMATIONS[anim_id]
        return self.custom_animations.get(anim_id)
    
    def create_animation(self, name: str, keyframes: list[dict], duration_ms: int, loop: bool = True) -> LightbarAnimation:
        """Create a new custom animation."""
        import uuid
        anim_id = str(uuid.uuid4())[:8]
        anim = LightbarAnimation(
            id=anim_id,
            name=name,
            keyframes=[AnimationKeyframe.from_dict(k) for k in keyframes],
            duration_ms=duration_ms,
            loop=loop
        )
        self.custom_animations[anim_id] = anim
        self._save()
        return anim
    
    def update_animation(self, anim_id: str, **kwargs) -> bool:
        """Update a custom animation."""
        if anim_id not in self.custom_animations:
            return False
        anim = self.custom_animations[anim_id]
        if "name" in kwargs:
            anim.name = kwargs["name"]
        if "keyframes" in kwargs:
            anim.keyframes = [AnimationKeyframe.from_dict(k) for k in kwargs["keyframes"]]
        if "duration_ms" in kwargs:
            anim.duration_ms = kwargs["duration_ms"]
        if "loop" in kwargs:
            anim.loop = kwargs["loop"]
        self._save()
        return True
    
    def delete_animation(self, anim_id: str) -> bool:
        """Delete a custom animation."""
        if anim_id in self.custom_animations:
            del self.custom_animations[anim_id]
            self._save()
            return True
        return False
    
    def set_battery_level(self, level: int) -> None:
        """Update battery level for battery-reactive mode."""
        self.current_battery = max(0, min(100, level))
    
    def apply_config(self, config: LightbarConfig) -> None:
        """Apply a lightbar configuration and start/stop animations as needed."""
        self.current_config = config
        
        # Stop any running animation
        if self._animation_task and not self._animation_task.done():
            self._running = False
            self._animation_task.cancel()
        
        # Handle different modes
        if config.mode == LightbarMode.OFF:
            self._write_ipc_state(0, 0, 0, config.player_leds)
        
        elif config.mode == LightbarMode.STATIC:
            c = config.color
            b = config.brightness
            self._write_ipc_state(
                int(c.r * b), int(c.g * b), int(c.b * b),
                config.player_leds
            )
        
        elif config.mode in (LightbarMode.BREATHING, LightbarMode.RAINBOW, 
                            LightbarMode.WAVE, LightbarMode.BATTERY, LightbarMode.CUSTOM):
            # Start animation loop
            self._running = True
            self._animation_task = asyncio.create_task(self._run_animation_loop())
    
    async def _run_animation_loop(self) -> None:
        """Main animation loop - runs until stopped."""
        import math
        import time
        
        start_time = time.monotonic()
        
        while self._running:
            config = self.current_config
            elapsed_ms = int((time.monotonic() - start_time) * 1000)
            
            r, g, b = 0, 0, 0
            
            if config.mode == LightbarMode.BREATHING:
                # Breathing animation
                cycle_pos = (elapsed_ms % config.breathing_speed_ms) / config.breathing_speed_ms
                # Sine wave for smooth breathing
                breath = (math.sin(cycle_pos * math.pi * 2 - math.pi / 2) + 1) / 2
                brightness = config.breathing_min_brightness + (1 - config.breathing_min_brightness) * breath
                
                if config.breathing_color2:
                    # Fade between two colors
                    color = Color.lerp(config.color, config.breathing_color2, breath)
                else:
                    color = config.color
                
                r = int(color.r * brightness * config.brightness)
                g = int(color.g * brightness * config.brightness)
                b = int(color.b * brightness * config.brightness)
            
            elif config.mode == LightbarMode.RAINBOW:
                # Rainbow cycle
                cycle_pos = (elapsed_ms % config.rainbow_speed_ms) / config.rainbow_speed_ms
                hue = cycle_pos
                sat = config.rainbow_saturation
                val = config.brightness
                
                # HSV to RGB
                r, g, b = self._hsv_to_rgb(hue, sat, val)
            
            elif config.mode == LightbarMode.WAVE:
                # Wave through multiple colors
                if config.wave_colors:
                    cycle_pos = (elapsed_ms % config.wave_speed_ms) / config.wave_speed_ms
                    num_colors = len(config.wave_colors)
                    
                    # Find which two colors we're between
                    idx = cycle_pos * num_colors
                    idx1 = int(idx) % num_colors
                    idx2 = (idx1 + 1) % num_colors
                    t = idx - int(idx)
                    
                    color = Color.lerp(config.wave_colors[idx1], config.wave_colors[idx2], t)
                    r = int(color.r * config.brightness)
                    g = int(color.g * config.brightness)
                    b = int(color.b * config.brightness)
            
            elif config.mode == LightbarMode.BATTERY:
                # Battery-reactive coloring
                level = self.current_battery
                
                if level >= config.battery_high_threshold:
                    color = config.battery_high_color
                elif level >= config.battery_low_threshold:
                    # Interpolate between mid and high
                    t = (level - config.battery_low_threshold) / (config.battery_high_threshold - config.battery_low_threshold)
                    color = Color.lerp(config.battery_mid_color, config.battery_high_color, t)
                else:
                    # Interpolate between low and mid
                    t = level / config.battery_low_threshold
                    color = Color.lerp(config.battery_low_color, config.battery_mid_color, t)
                
                brightness = config.brightness
                
                # Pulse when low battery
                if level < config.battery_low_threshold and config.battery_pulse_when_low:
                    pulse = (math.sin(elapsed_ms / 200.0) + 1) / 2
                    brightness *= (0.3 + 0.7 * pulse)
                
                r = int(color.r * brightness)
                g = int(color.g * brightness)
                b = int(color.b * brightness)
            
            elif config.mode == LightbarMode.CUSTOM and config.custom_animation_id:
                # Custom animation playback
                anim = self.get_animation(config.custom_animation_id)
                if anim and anim.keyframes:
                    if anim.loop:
                        anim_time = elapsed_ms % anim.duration_ms
                    else:
                        anim_time = min(elapsed_ms, anim.duration_ms)
                    
                    # Find surrounding keyframes
                    prev_kf = anim.keyframes[0]
                    next_kf = anim.keyframes[-1]
                    
                    for i, kf in enumerate(anim.keyframes):
                        if kf.time_ms <= anim_time:
                            prev_kf = kf
                            if i + 1 < len(anim.keyframes):
                                next_kf = anim.keyframes[i + 1]
                            else:
                                next_kf = anim.keyframes[0] if anim.loop else kf
                    
                    # Interpolate between keyframes
                    if prev_kf.time_ms != next_kf.time_ms:
                        segment_duration = next_kf.time_ms - prev_kf.time_ms
                        if segment_duration < 0:  # Wrap around
                            segment_duration += anim.duration_ms
                        t = (anim_time - prev_kf.time_ms) / segment_duration
                        t = self._apply_easing(t, prev_kf.easing)
                    else:
                        t = 0
                    
                    color = Color.lerp(prev_kf.color, next_kf.color, t)
                    brightness = prev_kf.brightness + (next_kf.brightness - prev_kf.brightness) * t
                    brightness *= config.brightness
                    
                    r = int(color.r * brightness)
                    g = int(color.g * brightness)
                    b = int(color.b * brightness)
            
            # Clamp and write
            r = max(0, min(255, r))
            g = max(0, min(255, g))
            b = max(0, min(255, b))
            
            self._write_ipc_state(r, g, b, config.player_leds)
            
            # ~60 FPS update rate for smooth animations
            await asyncio.sleep(1/60)
    
    def _hsv_to_rgb(self, h: float, s: float, v: float) -> tuple[int, int, int]:
        """Convert HSV to RGB."""
        import colorsys
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        return int(r * 255), int(g * 255), int(b * 255)
    
    def _apply_easing(self, t: float, easing: EasingFunction) -> float:
        """Apply easing function to interpolation."""
        import math
        
        if easing == EasingFunction.LINEAR:
            return t
        elif easing == EasingFunction.EASE_IN:
            return t * t
        elif easing == EasingFunction.EASE_OUT:
            return 1 - (1 - t) * (1 - t)
        elif easing == EasingFunction.EASE_IN_OUT:
            return t * t * (3 - 2 * t)
        elif easing == EasingFunction.SINE:
            return (1 - math.cos(t * math.pi)) / 2
        return t
    
    def stop(self) -> None:
        """Stop all animations."""
        self._running = False
        if self._animation_task:
            self._animation_task.cancel()
        self._write_ipc_state(0, 0, 0, 0)
    
    def get_current_state(self) -> dict:
        """Get current lightbar state."""
        return {
            "config": self.current_config.to_dict(),
            "battery": self.current_battery,
            "running": self._running
        }