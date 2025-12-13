"""
API route handlers for the RosettaPad web server.
"""

import uuid
from aiohttp import web
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .bluetooth import BluetoothManager
    from .profiles import ProfileManager


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
        return web.json_response({"success": True, "profile": self.pm.get_profile(profile_id).to_dict()})
    
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