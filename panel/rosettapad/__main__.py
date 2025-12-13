"""
Allow running the package directly: python -m rosettapad
"""

from .app import run_server
from .config import Config

if __name__ == "__main__":
    config = Config.from_env()
    run_server(config)
