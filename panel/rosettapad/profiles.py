"""
Profile system for controller configurations.
"""

import json
import uuid
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class MacroAction:
    """A single action within a macro."""
    type: str
    button: str = ""
    duration_ms: int = 0
    
    def to_dict(self) -> dict:
        return {"type": self.type, "button": self.button, "duration_ms": self.duration_ms}
    
    @classmethod
    def from_dict(cls, data: dict) -> "MacroAction":
        return cls(
            type=data.get("type", "press"),
            button=data.get("button", ""),
            duration_ms=data.get("duration_ms", 0)
        )


@dataclass
class Macro:
    """A macro triggered by a button."""
    id: str
    name: str
    trigger_button: str
    trigger_mode: str
    actions: list[MacroAction] = field(default_factory=list)
    enabled: bool = True
    
    def to_dict(self) -> dict:
        return {
            "id": self.id, "name": self.name, "trigger_button": self.trigger_button,
            "trigger_mode": self.trigger_mode,
            "actions": [a.to_dict() for a in self.actions], "enabled": self.enabled
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "Macro":
        return cls(
            id=data.get("id", ""), name=data.get("name", ""),
            trigger_button=data.get("trigger_button", ""),
            trigger_mode=data.get("trigger_mode", "on_press"),
            actions=[MacroAction.from_dict(a) for a in data.get("actions", [])],
            enabled=data.get("enabled", True)
        )


@dataclass
class ButtonRemap:
    """A button remapping with optional bidirectional swap."""
    id: str
    from_button: str
    to_button: str
    bidirectional: bool = False
    enabled: bool = True
    
    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "from_button": self.from_button,
            "to_button": self.to_button,
            "bidirectional": self.bidirectional,
            "enabled": self.enabled
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "ButtonRemap":
        # Handle legacy format (no id field)
        remap_id = data.get("id", str(uuid.uuid4())[:8])
        return cls(
            id=remap_id,
            from_button=data.get("from_button", ""),
            to_button=data.get("to_button", ""),
            bidirectional=data.get("bidirectional", False),
            enabled=data.get("enabled", True)
        )


@dataclass 
class Profile:
    """A controller profile with macros and remaps."""
    id: str
    name: str
    description: str = ""
    is_default: bool = False
    macros: list[Macro] = field(default_factory=list)
    button_remaps: list[ButtonRemap] = field(default_factory=list)
    
    def to_dict(self) -> dict:
        return {
            "id": self.id, "name": self.name, "description": self.description,
            "is_default": self.is_default,
            "macros": [m.to_dict() for m in self.macros],
            "button_remaps": [r.to_dict() for r in self.button_remaps]
        }
    
    @classmethod
    def from_dict(cls, data: dict) -> "Profile":
        return cls(
            id=data.get("id", ""), name=data.get("name", ""),
            description=data.get("description", ""), is_default=data.get("is_default", False),
            macros=[Macro.from_dict(m) for m in data.get("macros", [])],
            button_remaps=[ButtonRemap.from_dict(r) for r in data.get("button_remaps", [])]
        )


class ProfileManager:
    """Manages controller profiles with persistent storage."""
    
    DEFAULT_PROFILES: list[Profile] = [
        Profile(
            id="default",
            name="Default",
            description="Standard controller configuration",
            is_default=True,
            macros=[],
            button_remaps=[]
        )
    ]
    
    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.profiles: dict[str, Profile] = {}
        self.active_profile_id: str = "default"
        self._active_profile_cache: Optional[Profile] = None
        self._ensure_data_dir()
        self._load()
    
    def _ensure_data_dir(self) -> None:
        self.filepath.parent.mkdir(parents=True, exist_ok=True)
    
    def _load(self) -> None:
        for profile in self.DEFAULT_PROFILES:
            self.profiles[profile.id] = Profile(
                id=profile.id, name=profile.name, description=profile.description,
                is_default=True, macros=list(profile.macros), button_remaps=list(profile.button_remaps)
            )
        if self.filepath.exists():
            try:
                with open(self.filepath, "r") as f:
                    data = json.load(f)
                    self.active_profile_id = data.get("active_profile_id", "default")
                    for profile_data in data.get("profiles", []):
                        profile = Profile.from_dict(profile_data)
                        if not profile.is_default:
                            self.profiles[profile.id] = profile
                        elif profile.id in self.profiles:
                            self.profiles[profile.id].macros = profile.macros
                            self.profiles[profile.id].button_remaps = profile.button_remaps
            except (json.JSONDecodeError, IOError) as e:
                print(f"Warning: Could not load profiles file: {e}")
        if self.active_profile_id not in self.profiles:
            self.active_profile_id = "default"
        self._update_active_cache()
    
    def _save(self) -> None:
        data = {"active_profile_id": self.active_profile_id, "profiles": [p.to_dict() for p in self.profiles.values()]}
        try:
            with open(self.filepath, "w") as f:
                json.dump(data, f, indent=2)
        except IOError as e:
            print(f"Warning: Could not save profiles file: {e}")
    
    def _update_active_cache(self) -> None:
        self._active_profile_cache = self.profiles.get(self.active_profile_id)
    
    def _generate_id(self, name: str) -> str:
        base_id = name.lower().replace(" ", "_")
        base_id = "".join(c for c in base_id if c.isalnum() or c == "_") or "profile"
        if base_id not in self.profiles:
            return base_id
        counter = 1
        while f"{base_id}_{counter}" in self.profiles:
            counter += 1
        return f"{base_id}_{counter}"
    
    def get_active_profile(self) -> Optional[Profile]:
        return self._active_profile_cache
    
    def set_active_profile(self, profile_id: str) -> bool:
        if profile_id not in self.profiles:
            return False
        self.active_profile_id = profile_id
        self._update_active_cache()
        self._save()
        return True
    
    def get_all_profiles(self) -> list[Profile]:
        return list(self.profiles.values())
    
    def get_profile(self, profile_id: str) -> Optional[Profile]:
        return self.profiles.get(profile_id)
    
    def create_profile(self, name: str, description: str = "") -> Profile:
        profile_id = self._generate_id(name)
        profile = Profile(id=profile_id, name=name, description=description, is_default=False)
        self.profiles[profile_id] = profile
        self._save()
        return profile
    
    def update_profile(self, profile_id: str, name: Optional[str] = None, description: Optional[str] = None) -> bool:
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        if name is not None:
            profile.name = name
        if description is not None:
            profile.description = description
        self._save()
        if profile_id == self.active_profile_id:
            self._update_active_cache()
        return True
    
    def delete_profile(self, profile_id: str) -> bool:
        profile = self.profiles.get(profile_id)
        if not profile or profile.is_default:
            return False
        del self.profiles[profile_id]
        if self.active_profile_id == profile_id:
            self.active_profile_id = "default"
            self._update_active_cache()
        self._save()
        return True
    
    def duplicate_profile(self, profile_id: str, new_name: Optional[str] = None) -> Optional[Profile]:
        source = self.profiles.get(profile_id)
        if not source:
            return None
        name = new_name or f"{source.name} (Copy)"
        new_id = self._generate_id(name)
        new_profile = Profile(
            id=new_id, name=name, description=source.description, is_default=False,
            macros=[Macro.from_dict(m.to_dict()) for m in source.macros],
            button_remaps=[ButtonRemap.from_dict(r.to_dict()) for r in source.button_remaps]
        )
        self.profiles[new_id] = new_profile
        self._save()
        return new_profile
    
    # =================================================================
    # Macro Methods
    # =================================================================
    def add_macro(self, profile_id: str, macro: Macro) -> bool:
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        profile.macros.append(macro)
        self._save()
        if profile_id == self.active_profile_id:
            self._update_active_cache()
        return True
    
    def update_macro(self, profile_id: str, macro_id: str, macro: Macro) -> bool:
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        for i, m in enumerate(profile.macros):
            if m.id == macro_id:
                profile.macros[i] = macro
                self._save()
                if profile_id == self.active_profile_id:
                    self._update_active_cache()
                return True
        return False
    
    def remove_macro(self, profile_id: str, macro_id: str) -> bool:
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        original_len = len(profile.macros)
        profile.macros = [m for m in profile.macros if m.id != macro_id]
        if len(profile.macros) < original_len:
            self._save()
            if profile_id == self.active_profile_id:
                self._update_active_cache()
            return True
        return False
    
    def get_macro(self, profile_id: str, macro_id: str) -> Optional[Macro]:
        profile = self.profiles.get(profile_id)
        if not profile:
            return None
        for macro in profile.macros:
            if macro.id == macro_id:
                return macro
        return None
    
    # =================================================================
    # Remap Methods
    # =================================================================
    def add_remap(self, profile_id: str, remap: ButtonRemap) -> bool:
        """Add a new button remap to a profile."""
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        profile.button_remaps.append(remap)
        self._save()
        if profile_id == self.active_profile_id:
            self._update_active_cache()
        return True
    
    def update_remap(self, profile_id: str, remap_id: str, remap: ButtonRemap) -> bool:
        """Update an existing button remap."""
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        for i, r in enumerate(profile.button_remaps):
            if r.id == remap_id:
                profile.button_remaps[i] = remap
                self._save()
                if profile_id == self.active_profile_id:
                    self._update_active_cache()
                return True
        return False
    
    def remove_remap(self, profile_id: str, remap_id: str) -> bool:
        """Remove a button remap by ID."""
        profile = self.profiles.get(profile_id)
        if not profile:
            return False
        original_len = len(profile.button_remaps)
        profile.button_remaps = [r for r in profile.button_remaps if r.id != remap_id]
        if len(profile.button_remaps) < original_len:
            self._save()
            if profile_id == self.active_profile_id:
                self._update_active_cache()
            return True
        return False
    
    def get_remap(self, profile_id: str, remap_id: str) -> Optional[ButtonRemap]:
        """Get a specific remap by ID."""
        profile = self.profiles.get(profile_id)
        if not profile:
            return None
        for remap in profile.button_remaps:
            if remap.id == remap_id:
                return remap
        return None