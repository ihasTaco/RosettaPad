"""
Configuration management for RosettaPad.
"""

from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional
import os


@dataclass
class Config:
    """Application configuration with sensible defaults."""
    
    host: str = "0.0.0.0"
    port: int = 8080
    base_dir: Path = field(default_factory=lambda: Path(__file__).parent.parent)
    static_dir: Optional[Path] = None
    data_dir: Optional[Path] = None
    use_real_bluetooth: bool = False
    debug: bool = False
    
    def __post_init__(self):
        if self.static_dir is None:
            self.static_dir = self.base_dir / "static"
        if self.data_dir is None:
            self.data_dir = self.base_dir / "data"
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.static_dir.mkdir(parents=True, exist_ok=True)
    
    @property
    def controllers_file(self) -> Path:
        return self.data_dir / "controllers.json"
    
    @property
    def profiles_file(self) -> Path:
        return self.data_dir / "profiles.json"
    
    @classmethod
    def from_env(cls) -> "Config":
        return cls(
            host=os.environ.get("ROSETTAPAD_HOST", "0.0.0.0"),
            port=int(os.environ.get("ROSETTAPAD_PORT", "8080")),
            use_real_bluetooth=os.environ.get("ROSETTAPAD_REAL_BT", "").lower() in ("1", "true", "yes"),
            debug=os.environ.get("ROSETTAPAD_DEBUG", "").lower() in ("1", "true", "yes"),
        )


_config: Optional[Config] = None

def get_config() -> Config:
    global _config
    if _config is None:
        _config = Config.from_env()
    return _config

def set_config(config: Config) -> None:
    global _config
    _config = config
