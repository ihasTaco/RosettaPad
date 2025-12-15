"""
API route handlers for the RosettaPad web server.
"""

import uuid
from aiohttp import web # type: ignore
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .bluetooth import BluetoothManager
    from .profiles import ProfileManager
    from .lightbar import LightbarManager, LightbarConfig, Color


class BluetoothRoutes:
    def __init__(self, bt_manager: "BluetoothManager"):
        self.bt_manager = bt_manager
    
    async def status(self, request: web.Request) -> web.Response:
        self.bt_manager.update_latency()
        return web.json_response(self.bt_manager.get_status().to_dict())
    
    async def scan_start(self, request: web.Request) -> web.Response:
        return web.json_response({"success": await self.bt_manager.start_scan()})
    
    async def scan_stop(self, request: web.Request) -> web.Response:
        return web.json_response({"success": await self.bt_manager.stop_scan()})
    
    async def pair(self, request: web.Request) -> web.Response:
        data = await request.json()
        address = data.get("address")
        if not address:
            return web.json_response({"success": False, "error": "No address provided"}, status=400)
        return web.json_response({"success": await self.bt_manager.pair_device(address)})
    
    async def connect(self, request: web.Request) -> web.Response:
        data = await request.json()
        address = data.get("address")
        if not address:
            return web.json_response({"success": False, "error": "No address provided"}, status=400)
        return web.json_response({"success": await self.bt_manager.connect_device(address)})
    
    async def disconnect(self, request: web.Request) -> web.Response:
        data = await request.json()
        address = data.get("address")
        if not address:
            return web.json_response({"success": False, "error": "No address provided"}, status=400)
        return web.json_response({"success": await self.bt_manager.disconnect_device(address)})
    
    async def forget(self, request: web.Request) -> web.Response:
        data = await request.json()
        address = data.get("address")
        if not address:
            return web.json_response({"success": False, "error": "No address provided"}, status=400)
        return web.json_response({"success": await self.bt_manager.forget_device(address)})
    
    async def rename(self, request: web.Request) -> web.Response:
        data = await request.json()
        address = data.get("address")
        if not address:
            return web.json_response({"success": False, "error": "No address provided"}, status=400)
        return web.json_response({"success": self.bt_manager.rename_device(address, data.get("name", ""))})
    
    def register_routes(self, app: web.Application, prefix: str = "/api") -> None:
        app.router.add_get(f"{prefix}/status", self.status)
        app.router.add_post(f"{prefix}/scan/start", self.scan_start)
        app.router.add_post(f"{prefix}/scan/stop", self.scan_stop)
        app.router.add_post(f"{prefix}/pair", self.pair)
        app.router.add_post(f"{prefix}/connect", self.connect)
        app.router.add_post(f"{prefix}/disconnect", self.disconnect)
        app.router.add_post(f"{prefix}/forget", self.forget)
        app.router.add_post(f"{prefix}/rename", self.rename)


class ProfileRoutes:
    def __init__(self, profile_manager: "ProfileManager"):
        self.pm = profile_manager
    
    async def list_profiles(self, request: web.Request) -> web.Response:
        profiles = self.pm.get_all_profiles()
        active = self.pm.get_active_profile()
        return web.json_response({
            "profiles": [p.to_dict() for p in profiles],
            "active_profile_id": self.pm.active_profile_id,
            "active_profile": active.to_dict() if active else None
        })
    
    async def get_profile(self, request: web.Request) -> web.Response:
        profile = self.pm.get_profile(request.match_info.get("profile_id"))
        if not profile:
            return web.json_response({"error": "Profile not found"}, status=404)
        return web.json_response(profile.to_dict())
    
    async def create_profile(self, request: web.Request) -> web.Response:
        data = await request.json()
        name = data.get("name", "").strip()
        if not name:
            return web.json_response({"success": False, "error": "Name is required"}, status=400)
        profile = self.pm.create_profile(name, data.get("description", "").strip())
        return web.json_response({"success": True, "profile": profile.to_dict()})
    
    async def update_profile(self, request: web.Request) -> web.Response:
        profile_id = request.match_info.get("profile_id")
        data = await request.json()
        if not self.pm.update_profile(profile_id, data.get("name"), data.get("description")):
            return web.json_response({"success": False, "error": "Profile not found"}, status=404)
        return web.json_response({"success": True, "profile": self.pm.get_profile(profile_id).to_dict()}) # type: ignore
    
    async def delete_profile(self, request: web.Request) -> web.Response:
        profile_id = request.match_info.get("profile_id")
        profile = self.pm.get_profile(profile_id)
        if not profile:
            return web.json_response({"success": False, "error": "Profile not found"}, status=404)
        if profile.is_default:
            return web.json_response({"success": False, "error": "Cannot delete default profiles"}, status=400)
        return web.json_response({"success": self.pm.delete_profile(profile_id)})
    
    async def duplicate_profile(self, request: web.Request) -> web.Response:
        data = await request.json()
        profile = self.pm.duplicate_profile(request.match_info.get("profile_id"), data.get("name"))
        if not profile:
            return web.json_response({"success": False, "error": "Source profile not found"}, status=404)
        return web.json_response({"success": True, "profile": profile.to_dict()})
    
    async def activate_profile(self, request: web.Request) -> web.Response:
        profile_id = request.match_info.get("profile_id")
        if not self.pm.set_active_profile(profile_id):
            return web.json_response({"success": False, "error": "Profile not found"}, status=404)
        return web.json_response({"success": True, "active_profile_id": profile_id})
    
    def register_routes(self, app: web.Application, prefix: str = "/api") -> None:
        app.router.add_get(f"{prefix}/profiles", self.list_profiles)
        app.router.add_post(f"{prefix}/profiles", self.create_profile)
        app.router.add_get(f"{prefix}/profiles/{{profile_id}}", self.get_profile)
        app.router.add_put(f"{prefix}/profiles/{{profile_id}}", self.update_profile)
        app.router.add_delete(f"{prefix}/profiles/{{profile_id}}", self.delete_profile)
        app.router.add_post(f"{prefix}/profiles/{{profile_id}}/duplicate", self.duplicate_profile)
        app.router.add_post(f"{prefix}/profiles/{{profile_id}}/activate", self.activate_profile)


class MacroRoutes:
    def __init__(self, profile_manager: "ProfileManager"):
        self.pm = profile_manager
    
    async def list_macros(self, request: web.Request) -> web.Response:
        profile = self.pm.get_profile(request.match_info.get("profile_id"))
        if not profile:
            return web.json_response({"error": "Profile not found"}, status=404)
        return web.json_response({"macros": [m.to_dict() for m in profile.macros]})
    
    async def get_macro(self, request: web.Request) -> web.Response:
        macro = self.pm.get_macro(request.match_info.get("profile_id"), request.match_info.get("macro_id"))
        if not macro:
            return web.json_response({"error": "Macro not found"}, status=404)
        return web.json_response(macro.to_dict())
    
    async def create_macro(self, request: web.Request) -> web.Response:
        from .profiles import Macro, MacroAction
        profile_id = request.match_info.get("profile_id")
        if not self.pm.get_profile(profile_id):
            return web.json_response({"success": False, "error": "Profile not found"}, status=404)
        data = await request.json()
        macro = Macro(
            id=data.get("id", str(uuid.uuid4())[:8]), name=data.get("name", "New Macro"),
            trigger_button=data.get("trigger_button", ""), trigger_mode=data.get("trigger_mode", "on_press"),
            actions=[MacroAction.from_dict(a) for a in data.get("actions", [])], enabled=data.get("enabled", True)
        )
        return web.json_response({"success": self.pm.add_macro(profile_id, macro), "macro": macro.to_dict()})
    
    async def update_macro(self, request: web.Request) -> web.Response:
        from .profiles import Macro, MacroAction
        profile_id, macro_id = request.match_info.get("profile_id"), request.match_info.get("macro_id")
        existing = self.pm.get_macro(profile_id, macro_id)
        if not existing:
            return web.json_response({"success": False, "error": "Macro not found"}, status=404)
        data = await request.json()
        macro = Macro(
            id=macro_id, name=data.get("name", existing.name),
            trigger_button=data.get("trigger_button", existing.trigger_button),
            trigger_mode=data.get("trigger_mode", existing.trigger_mode),
            actions=[MacroAction.from_dict(a) for a in data.get("actions", [])] if "actions" in data else existing.actions,
            enabled=data.get("enabled", existing.enabled)
        )
        return web.json_response({"success": self.pm.update_macro(profile_id, macro_id, macro), "macro": macro.to_dict()})
    
    async def delete_macro(self, request: web.Request) -> web.Response:
        if not self.pm.remove_macro(request.match_info.get("profile_id"), request.match_info.get("macro_id")):
            return web.json_response({"success": False, "error": "Macro not found"}, status=404)
        return web.json_response({"success": True})
    
    def register_routes(self, app: web.Application, prefix: str = "/api") -> None:
        base = f"{prefix}/profiles/{{profile_id}}/macros"
        app.router.add_get(base, self.list_macros)
        app.router.add_post(base, self.create_macro)
        app.router.add_get(f"{base}/{{macro_id}}", self.get_macro)
        app.router.add_put(f"{base}/{{macro_id}}", self.update_macro)
        app.router.add_delete(f"{base}/{{macro_id}}", self.delete_macro)


class RemapRoutes:
    """API routes for button remap management."""
    
    def __init__(self, profile_manager: "ProfileManager"):
        self.pm = profile_manager
    
    async def list_remaps(self, request: web.Request) -> web.Response:
        """Get all remaps for a profile."""
        profile = self.pm.get_profile(request.match_info.get("profile_id"))
        if not profile:
            return web.json_response({"error": "Profile not found"}, status=404)
        return web.json_response({"remaps": [r.to_dict() for r in profile.button_remaps]})
    
    async def get_remap(self, request: web.Request) -> web.Response:
        """Get a specific remap by ID."""
        remap = self.pm.get_remap(
            request.match_info.get("profile_id"),
            request.match_info.get("remap_id")
        )
        if not remap:
            return web.json_response({"error": "Remap not found"}, status=404)
        return web.json_response(remap.to_dict())
    
    async def create_remap(self, request: web.Request) -> web.Response:
        """Create a new button remap."""
        from .profiles import ButtonRemap
        
        profile_id = request.match_info.get("profile_id")
        if not self.pm.get_profile(profile_id):
            return web.json_response({"success": False, "error": "Profile not found"}, status=404)
        
        data = await request.json()
        
        # Validate required fields
        from_button = data.get("from_button", "").strip()
        to_button = data.get("to_button", "").strip()
        
        if not from_button or not to_button:
            return web.json_response({
                "success": False,
                "error": "Both from_button and to_button are required"
            }, status=400)
        
        if from_button == to_button:
            return web.json_response({
                "success": False,
                "error": "from_button and to_button cannot be the same"
            }, status=400)
        
        remap = ButtonRemap(
            id=data.get("id", str(uuid.uuid4())[:8]),
            from_button=from_button,
            to_button=to_button,
            bidirectional=data.get("bidirectional", False),
            enabled=data.get("enabled", True)
        )
        
        success = self.pm.add_remap(profile_id, remap)
        return web.json_response({"success": success, "remap": remap.to_dict()})
    
    async def update_remap(self, request: web.Request) -> web.Response:
        """Update an existing button remap."""
        from .profiles import ButtonRemap
        
        profile_id = request.match_info.get("profile_id")
        remap_id = request.match_info.get("remap_id")
        
        existing = self.pm.get_remap(profile_id, remap_id)
        if not existing:
            return web.json_response({"success": False, "error": "Remap not found"}, status=404)
        
        data = await request.json()
        
        # Get values with fallback to existing
        from_button = data.get("from_button", existing.from_button)
        to_button = data.get("to_button", existing.to_button)
        
        if from_button == to_button:
            return web.json_response({
                "success": False,
                "error": "from_button and to_button cannot be the same"
            }, status=400)
        
        remap = ButtonRemap(
            id=remap_id,
            from_button=from_button,
            to_button=to_button,
            bidirectional=data.get("bidirectional", existing.bidirectional),
            enabled=data.get("enabled", existing.enabled)
        )
        
        success = self.pm.update_remap(profile_id, remap_id, remap)
        return web.json_response({"success": success, "remap": remap.to_dict()})
    
    async def delete_remap(self, request: web.Request) -> web.Response:
        """Delete a button remap."""
        profile_id = request.match_info.get("profile_id")
        remap_id = request.match_info.get("remap_id")
        
        if not self.pm.remove_remap(profile_id, remap_id):
            return web.json_response({"success": False, "error": "Remap not found"}, status=404)
        return web.json_response({"success": True})
    
    def register_routes(self, app: web.Application, prefix: str = "/api") -> None:
        """Register remap API routes."""
        base = f"{prefix}/profiles/{{profile_id}}/remaps"
        app.router.add_get(base, self.list_remaps)
        app.router.add_post(base, self.create_remap)
        app.router.add_get(f"{base}/{{remap_id}}", self.get_remap)
        app.router.add_put(f"{base}/{{remap_id}}", self.update_remap)
        app.router.add_delete(f"{base}/{{remap_id}}", self.delete_remap)

class LightbarRoutes:
    """API routes for lightbar configuration."""
    
    def __init__(self, lightbar_manager: "LightbarManager"):
        self.lm = lightbar_manager
    
    async def get_state(self, request: web.Request) -> web.Response:
        """Get current lightbar state."""
        return web.json_response(self.lm.get_current_state())
    
    async def set_config(self, request: web.Request) -> web.Response:
        """Set lightbar configuration."""
        from .lightbar import LightbarConfig
        
        data = await request.json()
        try:
            config = LightbarConfig.from_dict(data)
            self.lm.apply_config(config)
            return web.json_response({"success": True, "config": config.to_dict()})
        except Exception as e:
            return web.json_response({"success": False, "error": str(e)}, status=400)
    
    async def set_color(self, request: web.Request) -> web.Response:
        """Quick endpoint to set a static color."""
        from .lightbar import LightbarConfig, LightbarMode, Color
        
        data = await request.json()
        
        # Accept both hex and RGB
        if "hex" in data:
            color = Color.from_hex(data["hex"])
        else:
            color = Color(
                r=data.get("r", 0),
                g=data.get("g", 0),
                b=data.get("b", 255)
            )
        
        brightness = data.get("brightness", 1.0)
        
        config = LightbarConfig(
            mode=LightbarMode.STATIC,
            color=color,
            brightness=brightness
        )
        self.lm.apply_config(config)
        
        return web.json_response({
            "success": True, 
            "color": color.to_dict(),
            "hex": color.to_hex()
        })
    
    async def set_mode(self, request: web.Request) -> web.Response:
        """Set lightbar mode with optional parameters."""
        from .lightbar import LightbarConfig, LightbarMode, Color
        
        data = await request.json()
        mode_str = data.get("mode", "static")
        
        try:
            mode = LightbarMode(mode_str)
        except ValueError:
            return web.json_response({
                "success": False, 
                "error": f"Invalid mode: {mode_str}"
            }, status=400)
        
        # Start with current config as base
        config = self.lm.current_config
        config.mode = mode
        
        # Update any provided parameters
        if "brightness" in data:
            config.brightness = max(0.0, min(1.0, float(data["brightness"])))
        
        if "color" in data:
            if isinstance(data["color"], str):
                config.color = Color.from_hex(data["color"])
            else:
                config.color = Color.from_dict(data["color"])
        
        # Mode-specific parameters
        if mode == LightbarMode.BREATHING:
            if "speed_ms" in data:
                config.breathing_speed_ms = int(data["speed_ms"])
            if "min_brightness" in data:
                config.breathing_min_brightness = float(data["min_brightness"])
            if "color2" in data:
                if isinstance(data["color2"], str):
                    config.breathing_color2 = Color.from_hex(data["color2"])
                else:
                    config.breathing_color2 = Color.from_dict(data["color2"])
        
        elif mode == LightbarMode.RAINBOW:
            if "speed_ms" in data:
                config.rainbow_speed_ms = int(data["speed_ms"])
            if "saturation" in data:
                config.rainbow_saturation = float(data["saturation"])
        
        elif mode == LightbarMode.WAVE:
            if "speed_ms" in data:
                config.wave_speed_ms = int(data["speed_ms"])
            if "colors" in data:
                config.wave_colors = [
                    Color.from_hex(c) if isinstance(c, str) else Color.from_dict(c)
                    for c in data["colors"]
                ]
        
        elif mode == LightbarMode.CUSTOM:
            if "animation_id" in data:
                config.custom_animation_id = data["animation_id"]
        
        self.lm.apply_config(config)
        return web.json_response({"success": True, "config": config.to_dict()})
    
    async def turn_off(self, request: web.Request) -> web.Response:
        """Turn off the lightbar."""
        from .lightbar import LightbarConfig, LightbarMode
        
        config = LightbarConfig(mode=LightbarMode.OFF)
        self.lm.apply_config(config)
        return web.json_response({"success": True})
    
    async def set_battery(self, request: web.Request) -> web.Response:
        """Update battery level for battery-reactive mode."""
        data = await request.json()
        level = data.get("level", 100)
        self.lm.set_battery_level(level)
        return web.json_response({"success": True, "battery": level})
    
    async def set_player_leds(self, request: web.Request) -> web.Response:
        """Set player indicator LEDs."""
        data = await request.json()
        
        # Accept either a bitmask or individual LED states
        if "leds" in data:
            self.lm.current_config.player_leds = int(data["leds"])
        elif "led_states" in data:
            # led_states: [bool, bool, bool, bool, bool]
            states = data["led_states"]
            mask = 0
            for i, state in enumerate(states[:5]):
                if state:
                    mask |= (1 << i)
            self.lm.current_config.player_leds = mask
        
        if "brightness" in data:
            self.lm.current_config.player_led_brightness = float(data["brightness"])
        
        # Reapply current config to update LEDs
        self.lm.apply_config(self.lm.current_config)
        
        return web.json_response({
            "success": True, 
            "player_leds": self.lm.current_config.player_leds
        })
    
    # =================================================================
    # Animation CRUD
    # =================================================================
    async def list_animations(self, request: web.Request) -> web.Response:
        """List all available animations."""
        animations = self.lm.get_all_animations()
        return web.json_response({
            "animations": [a.to_dict() for a in animations],
            "presets": list(self.lm.PRESET_ANIMATIONS.keys()),
            "custom": list(self.lm.custom_animations.keys())
        })
    
    async def get_animation(self, request: web.Request) -> web.Response:
        """Get a specific animation by ID."""
        anim_id = request.match_info.get("animation_id")
        anim = self.lm.get_animation(anim_id)
        
        if not anim:
            return web.json_response({"error": "Animation not found"}, status=404)
        
        return web.json_response(anim.to_dict())
    
    async def create_animation(self, request: web.Request) -> web.Response:
        """Create a new custom animation."""
        data = await request.json()
        
        name = data.get("name", "").strip()
        if not name:
            return web.json_response({
                "success": False, 
                "error": "Name is required"
            }, status=400)
        
        keyframes = data.get("keyframes", [])
        if not keyframes:
            return web.json_response({
                "success": False,
                "error": "At least one keyframe is required"
            }, status=400)
        
        anim = self.lm.create_animation(
            name=name,
            keyframes=keyframes,
            duration_ms=data.get("duration_ms", 1000),
            loop=data.get("loop", True)
        )
        
        return web.json_response({"success": True, "animation": anim.to_dict()})
    
    async def update_animation(self, request: web.Request) -> web.Response:
        """Update a custom animation."""
        anim_id = request.match_info.get("animation_id")
        
        # Check if it's a preset (can't edit presets)
        if anim_id in self.lm.PRESET_ANIMATIONS:
            return web.json_response({
                "success": False,
                "error": "Cannot edit preset animations"
            }, status=400)
        
        data = await request.json()
        if not self.lm.update_animation(anim_id, **data):
            return web.json_response({
                "success": False,
                "error": "Animation not found"
            }, status=404)
        
        anim = self.lm.get_animation(anim_id)
        return web.json_response({"success": True, "animation": anim.to_dict()})
    
    async def delete_animation(self, request: web.Request) -> web.Response:
        """Delete a custom animation."""
        anim_id = request.match_info.get("animation_id")
        
        if anim_id in self.lm.PRESET_ANIMATIONS:
            return web.json_response({
                "success": False,
                "error": "Cannot delete preset animations"
            }, status=400)
        
        if not self.lm.delete_animation(anim_id):
            return web.json_response({
                "success": False,
                "error": "Animation not found"
            }, status=404)
        
        return web.json_response({"success": True})
    
    async def preview_animation(self, request: web.Request) -> web.Response:
        """Preview an animation without saving."""
        from .lightbar import LightbarConfig, LightbarMode
        
        anim_id = request.match_info.get("animation_id")
        anim = self.lm.get_animation(anim_id)
        
        if not anim:
            return web.json_response({"error": "Animation not found"}, status=404)
        
        config = LightbarConfig(
            mode=LightbarMode.CUSTOM,
            custom_animation_id=anim_id
        )
        self.lm.apply_config(config)
        
        return web.json_response({"success": True, "animation": anim.to_dict()})
    
    # =================================================================
    # Presets
    # =================================================================
    async def list_presets(self, request: web.Request) -> web.Response:
        """List color and configuration presets."""
        from .lightbar import PresetColors
        
        color_presets = {
            "red": PresetColors.RED.to_dict(),
            "green": PresetColors.GREEN.to_dict(),
            "blue": PresetColors.BLUE.to_dict(),
            "cyan": PresetColors.CYAN.to_dict(),
            "magenta": PresetColors.MAGENTA.to_dict(),
            "yellow": PresetColors.YELLOW.to_dict(),
            "orange": PresetColors.ORANGE.to_dict(),
            "purple": PresetColors.PURPLE.to_dict(),
            "pink": PresetColors.PINK.to_dict(),
            "white": PresetColors.WHITE.to_dict(),
            "ps_blue": PresetColors.PS_BLUE.to_dict(),
            "ps_light": PresetColors.PS_LIGHT.to_dict(),
        }
        
        animation_presets = list(self.lm.PRESET_ANIMATIONS.keys())
        
        return web.json_response({
            "colors": color_presets,
            "animations": animation_presets,
            "modes": ["off", "static", "breathing", "rainbow", "wave", "battery", "custom"]
        })
    
    async def apply_preset(self, request: web.Request) -> web.Response:
        """Apply a named preset."""
        from .lightbar import LightbarConfig, LightbarMode, Color, PresetColors
        
        preset_name = request.match_info.get("preset_name")
        
        # Check if it's a color preset
        color_presets = {
            "red": PresetColors.RED,
            "green": PresetColors.GREEN,
            "blue": PresetColors.BLUE,
            "cyan": PresetColors.CYAN,
            "magenta": PresetColors.MAGENTA,
            "yellow": PresetColors.YELLOW,
            "orange": PresetColors.ORANGE,
            "purple": PresetColors.PURPLE,
            "pink": PresetColors.PINK,
            "white": PresetColors.WHITE,
            "ps_blue": PresetColors.PS_BLUE,
            "ps_light": PresetColors.PS_LIGHT,
        }
        
        if preset_name in color_presets:
            config = LightbarConfig(
                mode=LightbarMode.STATIC,
                color=color_presets[preset_name]
            )
            self.lm.apply_config(config)
            return web.json_response({
                "success": True, 
                "type": "color",
                "config": config.to_dict()
            })
        
        # Check if it's an animation preset
        if preset_name in self.lm.PRESET_ANIMATIONS:
            config = LightbarConfig(
                mode=LightbarMode.CUSTOM,
                custom_animation_id=preset_name
            )
            self.lm.apply_config(config)
            return web.json_response({
                "success": True,
                "type": "animation",
                "config": config.to_dict()
            })
        
        return web.json_response({
            "success": False,
            "error": f"Unknown preset: {preset_name}"
        }, status=404)
    
    def register_routes(self, app: web.Application, prefix: str = "/api") -> None:
        """Register all lightbar routes."""
        # State management
        app.router.add_get(f"{prefix}/lightbar", self.get_state)
        app.router.add_post(f"{prefix}/lightbar", self.set_config)
        app.router.add_post(f"{prefix}/lightbar/color", self.set_color)
        app.router.add_post(f"{prefix}/lightbar/mode", self.set_mode)
        app.router.add_post(f"{prefix}/lightbar/off", self.turn_off)
        app.router.add_post(f"{prefix}/lightbar/battery", self.set_battery)
        app.router.add_post(f"{prefix}/lightbar/player-leds", self.set_player_leds)
        
        # Animations
        app.router.add_get(f"{prefix}/lightbar/animations", self.list_animations)
        app.router.add_post(f"{prefix}/lightbar/animations", self.create_animation)
        app.router.add_get(f"{prefix}/lightbar/animations/{{animation_id}}", self.get_animation)
        app.router.add_put(f"{prefix}/lightbar/animations/{{animation_id}}", self.update_animation)
        app.router.add_delete(f"{prefix}/lightbar/animations/{{animation_id}}", self.delete_animation)
        app.router.add_post(f"{prefix}/lightbar/animations/{{animation_id}}/preview", self.preview_animation)
        
        # Presets
        app.router.add_get(f"{prefix}/lightbar/presets", self.list_presets)
        app.router.add_post(f"{prefix}/lightbar/presets/{{preset_name}}", self.apply_preset)






