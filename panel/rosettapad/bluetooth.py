"""
Bluetooth management for controller pairing and connection.
"""

import asyncio
import random
import time
from dataclasses import dataclass, asdict
from enum import Enum
from typing import Optional

from .storage import ControllerStorage, SavedController


class PairingState(Enum):
    IDLE = "idle"
    SCANNING = "scanning"
    PAIRING = "pairing"
    CONNECTED = "connected"
    ERROR = "error"


@dataclass
class BluetoothDevice:
    address: str
    name: str
    paired: bool = False
    connected: bool = False
    trusted: bool = False
    
    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class ConnectionStatus:
    connected: bool
    controller: Optional[SavedController]
    latency_ms: float = 0.0
    
    def to_dict(self) -> dict:
        return {
            "connected": self.connected,
            "controller": self.controller.to_dict() if self.controller else None,
            "latency_ms": round(self.latency_ms, 1)
        }


@dataclass
class BluetoothStatus:
    state: PairingState
    discovered_devices: list[BluetoothDevice]
    trusted_devices: list[BluetoothDevice]
    connection: ConnectionStatus
    message: str = ""
    
    def to_dict(self) -> dict:
        return {
            "state": self.state.value,
            "discovered_devices": [d.to_dict() for d in self.discovered_devices],
            "trusted_devices": [d.to_dict() for d in self.trusted_devices],
            "connection": self.connection.to_dict(),
            "message": self.message
        }


class BluetoothManager:
    """Manages Bluetooth operations for controller pairing and connection."""
    
    def __init__(self, storage: ControllerStorage, use_real_bluetooth: bool = False):
        self.storage = storage
        self.use_real_bluetooth = use_real_bluetooth
        self.state = PairingState.IDLE
        self.discovered_devices: list[BluetoothDevice] = []
        self.message = ""
        self._scan_task: Optional[asyncio.Task] = None
        self._connected_address: Optional[str] = None
        self._connection_time: float = 0
        self._latency_ms: float = 0.0
    
    def get_status(self) -> BluetoothStatus:
        trusted = []
        for ctrl in self.storage.get_all():
            trusted.append(BluetoothDevice(
                address=ctrl.address, name=ctrl.display_name,
                paired=True, trusted=True, connected=(ctrl.address == self._connected_address)
            ))
        connected_ctrl = self.storage.get(self._connected_address) if self._connected_address else None
        connection = ConnectionStatus(
            connected=self._connected_address is not None,
            controller=connected_ctrl, latency_ms=self._latency_ms
        )
        return BluetoothStatus(
            state=self.state, discovered_devices=self.discovered_devices,
            trusted_devices=trusted, connection=connection, message=self.message
        )
    
    async def start_scan(self) -> bool:
        if self.state == PairingState.SCANNING:
            return False
        self.state = PairingState.SCANNING
        self.discovered_devices = []
        self.message = "Scanning for controllers..."
        if self.use_real_bluetooth:
            self._scan_task = asyncio.create_task(self._real_scan())
        else:
            self._scan_task = asyncio.create_task(self._stub_scan())
        return True
    
    async def stop_scan(self) -> bool:
        if self._scan_task:
            self._scan_task.cancel()
            try:
                await self._scan_task
            except asyncio.CancelledError:
                pass
        self.state = PairingState.IDLE if not self._connected_address else PairingState.CONNECTED
        self.message = "Scan stopped"
        return True
    
    async def pair_device(self, address: str) -> bool:
        self.state = PairingState.PAIRING
        self.message = f"Pairing with {address}..."
        if self.use_real_bluetooth:
            return await self._real_pair(address)
        return await self._stub_pair(address)
    
    async def connect_device(self, address: str) -> bool:
        if self.use_real_bluetooth:
            return await self._real_connect(address)
        return await self._stub_connect(address)
    
    async def disconnect_device(self, address: str) -> bool:
        if self.use_real_bluetooth:
            return await self._real_disconnect(address)
        return await self._stub_disconnect(address)
    
    async def forget_device(self, address: str) -> bool:
        if self._connected_address == address:
            await self.disconnect_device(address)
        if self.use_real_bluetooth:
            await self._run_bluetoothctl(f"remove {address}")
        self.storage.remove(address)
        self.message = "Device forgotten"
        return True
    
    def rename_device(self, address: str, custom_name: str) -> bool:
        return self.storage.rename(address, custom_name)
    
    def update_latency(self) -> None:
        if not self.use_real_bluetooth and self._connected_address:
            self._latency_ms = max(1.0, 8.0 + random.uniform(-2.0, 2.0))
    
    @property
    def is_connected(self) -> bool:
        return self._connected_address is not None
    
    @property
    def connected_address(self) -> Optional[str]:
        return self._connected_address
    
    # Stub implementations
    async def _stub_scan(self) -> None:
        await asyncio.sleep(2)
        fake_devices = [
            BluetoothDevice("AA:BB:CC:DD:EE:F1", "DualSense Wireless Controller"),
            BluetoothDevice("AA:BB:CC:DD:EE:F2", "DualShock 4"),
            BluetoothDevice("AA:BB:CC:DD:EE:F3", "Xbox Wireless Controller"),
        ]
        for device in fake_devices:
            if self.state != PairingState.SCANNING:
                break
            saved = self.storage.get(device.address)
            if saved:
                device.paired = device.trusted = True
                device.name = saved.display_name
            self.discovered_devices.append(device)
            self.message = f"Found: {device.name}"
            await asyncio.sleep(1.5)
        if self.state == PairingState.SCANNING:
            self.state = PairingState.IDLE if not self._connected_address else PairingState.CONNECTED
            self.message = f"Scan complete. Found {len(self.discovered_devices)} device(s)"
    
    async def _stub_pair(self, address: str) -> bool:
        await asyncio.sleep(2)
        name = "Unknown Controller"
        for device in self.discovered_devices:
            if device.address == address:
                name = device.name
                device.paired = device.trusted = True
                break
        self.storage.add(address, name)
        return await self._stub_connect(address)
    
    async def _stub_connect(self, address: str) -> bool:
        await asyncio.sleep(1)
        ctrl = self.storage.get(address)
        if ctrl:
            self._connected_address = address
            self._connection_time = time.time()
            self._latency_ms = 8.0
            self.state = PairingState.CONNECTED
            self.message = f"Connected to {ctrl.display_name}"
            return True
        self.state = PairingState.ERROR
        self.message = "Device not found"
        return False
    
    async def _stub_disconnect(self, address: str) -> bool:
        await asyncio.sleep(0.5)
        if self._connected_address == address:
            ctrl = self.storage.get(address)
            self._connected_address = None
            self._latency_ms = 0
            self.state = PairingState.IDLE
            self.message = f"Disconnected from {ctrl.display_name if ctrl else 'device'}"
            return True
        return False
    
    # Real implementations
    async def _run_bluetoothctl(self, *commands: str, timeout: float = 10.0) -> tuple[bool, str]:
        try:
            cmd_input = "\n".join(commands) + "\nexit\n"
            proc = await asyncio.create_subprocess_exec(
                "bluetoothctl", stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE
            )
            stdout, _ = await asyncio.wait_for(proc.communicate(cmd_input.encode()), timeout=timeout)
            return proc.returncode == 0, stdout.decode()
        except asyncio.TimeoutError:
            return False, "Command timed out"
        except Exception as e:
            return False, str(e)
    
    async def _real_scan(self) -> None:
        success, _ = await self._run_bluetoothctl("scan on", timeout=15.0)
        if not success:
            self.state = PairingState.ERROR
            self.message = "Failed to start scan"
            return
        await asyncio.sleep(10)
        await self._run_bluetoothctl("scan off")
        success, output = await self._run_bluetoothctl("devices")
        if success:
            for line in output.split("\n"):
                if "Device" in line:
                    parts = line.split(" ", 2)
                    if len(parts) >= 3:
                        address, name = parts[1], parts[2] if len(parts) > 2 else "Unknown"
                        saved = self.storage.get(address)
                        self.discovered_devices.append(BluetoothDevice(
                            address=address, name=saved.display_name if saved else name,
                            paired=saved is not None, trusted=saved is not None
                        ))
        self.state = PairingState.IDLE if not self._connected_address else PairingState.CONNECTED
        self.message = f"Scan complete. Found {len(self.discovered_devices)} device(s)"
    
    async def _real_pair(self, address: str) -> bool:
        success, _ = await self._run_bluetoothctl(f"pair {address}", timeout=30.0)
        if not success:
            self.state = PairingState.ERROR
            self.message = "Pairing failed"
            return False
        await self._run_bluetoothctl(f"trust {address}")
        name = next((d.name for d in self.discovered_devices if d.address == address), "Unknown Controller")
        self.storage.add(address, name)
        return await self._real_connect(address)
    
    async def _real_connect(self, address: str) -> bool:
        success, _ = await self._run_bluetoothctl(f"connect {address}", timeout=15.0)
        if not success:
            self.state = PairingState.ERROR
            self.message = "Connection failed"
            return False
        ctrl = self.storage.get(address)
        self._connected_address = address
        self._connection_time = time.time()
        self.state = PairingState.CONNECTED
        self.message = f"Connected to {ctrl.display_name if ctrl else 'device'}"
        return True
    
    async def _real_disconnect(self, address: str) -> bool:
        success, _ = await self._run_bluetoothctl(f"disconnect {address}")
        if success and self._connected_address == address:
            ctrl = self.storage.get(address)
            self._connected_address = None
            self._latency_ms = 0
            self.state = PairingState.IDLE
            self.message = f"Disconnected from {ctrl.display_name if ctrl else 'device'}"
        return success
