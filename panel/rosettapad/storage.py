"""
Persistent storage for paired controllers.
"""

import json
from pathlib import Path
from dataclasses import dataclass
from typing import Optional


@dataclass
class SavedController:
    """A saved/paired controller."""
    address: str
    name: str
    custom_name: str = ""
    
    @property
    def display_name(self) -> str:
        return self.custom_name if self.custom_name else self.name
    
    def to_dict(self) -> dict:
        return {
            "address": self.address,
            "name": self.name,
            "custom_name": self.custom_name,
            "display_name": self.display_name,
        }


class ControllerStorage:
    """Persistent storage for paired controllers."""
    
    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.controllers: dict[str, SavedController] = {}
        self._ensure_data_dir()
        self._load()
    
    def _ensure_data_dir(self) -> None:
        self.filepath.parent.mkdir(parents=True, exist_ok=True)
    
    def _load(self) -> None:
        if not self.filepath.exists():
            return
        try:
            with open(self.filepath, "r") as f:
                data = json.load(f)
                for addr, info in data.items():
                    self.controllers[addr] = SavedController(
                        address=addr,
                        name=info.get("name", "Unknown"),
                        custom_name=info.get("custom_name", "")
                    )
        except (json.JSONDecodeError, IOError) as e:
            print(f"Warning: Could not load controllers file: {e}")
    
    def _save(self) -> None:
        data = {
            addr: {"name": ctrl.name, "custom_name": ctrl.custom_name}
            for addr, ctrl in self.controllers.items()
        }
        try:
            with open(self.filepath, "w") as f:
                json.dump(data, f, indent=2)
        except IOError as e:
            print(f"Warning: Could not save controllers file: {e}")
    
    def add(self, address: str, name: str) -> SavedController:
        if address not in self.controllers:
            self.controllers[address] = SavedController(address=address, name=name)
        else:
            self.controllers[address].name = name
        self._save()
        return self.controllers[address]
    
    def remove(self, address: str) -> bool:
        if address in self.controllers:
            del self.controllers[address]
            self._save()
            return True
        return False
    
    def rename(self, address: str, custom_name: str) -> bool:
        if address in self.controllers:
            self.controllers[address].custom_name = custom_name
            self._save()
            return True
        return False
    
    def get(self, address: str) -> Optional[SavedController]:
        return self.controllers.get(address)
    
    def get_all(self) -> list[SavedController]:
        return list(self.controllers.values())
    
    def exists(self, address: str) -> bool:
        return address in self.controllers
