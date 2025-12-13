#!/usr/bin/env python3
"""
RosettaPad Web Server
A web interface for the DualSense to PS3 controller adapter.

Usage:
    python server.py                    # Run with defaults (stub mode)
    python server.py --real-bluetooth   # Run with real Bluetooth
    python server.py --port 8000        # Custom port
    python server.py --debug            # Debug mode
    
Environment variables:
    ROSETTAPAD_HOST          Server host (default: 0.0.0.0)
    ROSETTAPAD_PORT          Server port (default: 8080)
    ROSETTAPAD_REAL_BT       Enable real Bluetooth (1/true/yes)
    ROSETTAPAD_DEBUG         Enable debug mode (1/true/yes)
"""

import argparse
import subprocess
import sys

# Check for aiohttp, install if needed
try:
    from aiohttp import web
except ImportError:
    print("Installing aiohttp...")
    subprocess.check_call([
        sys.executable, "-m", "pip", "install", "aiohttp", 
        "-q", "--break-system-packages"
    ])
    from aiohttp import web

from rosettapad.config import Config, set_config
from rosettapad.app import run_server


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="RosettaPad Web Server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help="Server host (default: 0.0.0.0)"
    )
    
    parser.add_argument(
        "--port", "-p",
        type=int,
        default=8080,
        help="Server port (default: 8080)"
    )
    
    parser.add_argument(
        "--real-bluetooth", "-r",
        action="store_true",
        help="Enable real Bluetooth (requires Pi hardware)"
    )
    
    parser.add_argument(
        "--debug", "-d",
        action="store_true",
        help="Enable debug mode"
    )
    
    return parser.parse_args()


def main():
    """Main entry point."""
    args = parse_args()
    
    # Create config from command line args
    config = Config(
        host=args.host,
        port=args.port,
        use_real_bluetooth=args.real_bluetooth,
        debug=args.debug,
    )
    
    # Set as global config
    set_config(config)
    
    # Run the server
    run_server(config)


if __name__ == "__main__":
    main()