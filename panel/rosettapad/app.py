"""
Application factory and server setup for RosettaPad.
"""

from pathlib import Path
from aiohttp import web

from .config import Config, get_config
from .storage import ControllerStorage
from .profiles import ProfileManager
from .bluetooth import BluetoothManager
from .routes import BluetoothRoutes, ProfileRoutes, MacroRoutes, RemapRoutes


class RosettaPadApp:
    def __init__(self, config: Config = None):
        self.config = config or get_config()
        self.storage = ControllerStorage(self.config.controllers_file)
        self.profile_manager = ProfileManager(self.config.profiles_file)
        self.bt_manager = BluetoothManager(self.storage, use_real_bluetooth=self.config.use_real_bluetooth)
        self.bt_routes = BluetoothRoutes(self.bt_manager)
        self.profile_routes = ProfileRoutes(self.profile_manager)
        self.macro_routes = MacroRoutes(self.profile_manager)
        self.remap_routes = RemapRoutes(self.profile_manager)
        self.app = self._create_app()
    
    def _create_app(self) -> web.Application:
        app = web.Application()
        self.bt_routes.register_routes(app)
        self.profile_routes.register_routes(app)
        self.macro_routes.register_routes(app)
        self.remap_routes.register_routes(app)
        app.router.add_get("/", self._index_handler)
        if self.config.static_dir and self.config.static_dir.exists():
            app.router.add_static("/static", self.config.static_dir)
        app["rosettapad"] = self
        app["config"] = self.config
        return app
    
    async def _index_handler(self, request: web.Request) -> web.Response:
        html_path = self.config.static_dir / "index.html"
        if html_path.exists():
            return web.FileResponse(html_path)
        return web.Response(text="<h1>RosettaPad</h1><p>Place index.html in static/</p>", content_type="text/html")
    
    def run(self) -> None:
        self._print_banner()
        web.run_app(self.app, host=self.config.host, port=self.config.port, print=None)
    
    def _print_banner(self) -> None:
        mode = "REAL BLUETOOTH" if self.config.use_real_bluetooth else "STUB MODE (development)"
        print(f"""
╔═══════════════════════════════════════════════════════════╗
║                   RosettaPad Web Server                   ║
╠═══════════════════════════════════════════════════════════╣
║  Mode: {mode:^47} ║
║  URL:  http://{self.config.host}:{self.config.port:<41} ║
║  Data: {str(self.config.data_dir):<49} ║
╚═══════════════════════════════════════════════════════════╝
""")


def create_app(config: Config = None) -> web.Application:
    return RosettaPadApp(config).app


def run_server(config: Config = None) -> None:
    RosettaPadApp(config).run()