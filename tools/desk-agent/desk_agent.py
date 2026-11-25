#!/usr/bin/env python3
"""Desk agent: serves this PC's CPU, RAM, disk and network numbers to an X3 Lite in Desk mode.

The reader polls GET /v1/stats over the LAN (plain HTTP, read-only, no auth: it exposes only
the numbers below) and finds the agent by mDNS (_xdesk._tcp). See
docs/development/desk-mode-plan.md and tools/desk-agent/README.md.

Response, under 1 KB:
  {"v": 1, "host": "DESKTOP", "ts": 1795174800,
   "cpu": 23, "ram": [19.5, 32.0], "disks": [["C:", 412.0, 1000.0]],
   "net": [2400000, 310000], "uptime": 274000, "claude": null}
Units: percent, GB, bytes per second, seconds.

"photos": how many photos --photos DIR holds; photo n is GET /v1/photos/<n>.png, a grayscale
PNG sized to cover the reader's photo area (528 x 664), from Pillow.

"claude": {"session": [pct, resets_at], "week": [pct, resets_at]} from the Claude Code
subscription limits that desk_statusline.py saves (Claude Code's status line data), or null
when there is none. A window whose reset time has passed reads 0 % until Claude Code reports it
again; the numbers only move while a Claude Code session is open.
"""

from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import psutil

SERVICE_TYPE = "_xdesk._tcp.local."
DEFAULT_PORT = 8765
SAMPLE_SECONDS = 2.0
MAX_DISKS = 4
GB = 1024**3
CLAUDE_LIMITS_PATH = Path.home() / ".claude" / "desk-rate-limits.json"  # desk_statusline.py
PHOTO_W, PHOTO_H = 528, 664  # the X3's screen above Desk's info strip
PHOTO_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif", ".tif", ".tiff"}


class Photos:
    """The --photos folder: a sorted list (re-read every minute) and the last few PNGs made."""

    def __init__(self, folder: str | None) -> None:
        self.folder = Path(folder).expanduser() if folder else None
        self._lock = threading.Lock()
        self._names: list[Path] = []
        self._listed_at = 0.0
        self._cache: dict[tuple[str, float], bytes] = {}

    def names(self) -> list[Path]:
        if self.folder is None:
            return []
        with self._lock:
            if time.monotonic() - self._listed_at > 60:
                try:
                    self._names = sorted(
                        (f for f in self.folder.rglob("*") if f.is_file() and f.suffix.lower() in PHOTO_EXTENSIONS
                         and not f.name.startswith(".")),
                        key=lambda f: str(f).lower())
                except OSError:
                    self._names = []
                self._listed_at = time.monotonic()
            return list(self._names)

    def count(self) -> int:
        return len(self.names())

    def png(self, index: int) -> bytes | None:
        names = self.names()
        if not 0 <= index < len(names):
            return None
        path = names[index]
        try:
            key = (str(path), path.stat().st_mtime)
        except OSError:
            return None
        with self._lock:
            if key in self._cache:
                return self._cache[key]
        try:
            from io import BytesIO

            from PIL import Image, ImageOps
        except ImportError:
            return None
        try:
            with Image.open(path) as img:
                img = ImageOps.exif_transpose(img).convert("L")
                scale = max(PHOTO_W / img.width, PHOTO_H / img.height)
                if scale < 1:
                    img = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))),
                                     Image.LANCZOS)
                out = BytesIO()
                img.save(out, "PNG", optimize=True)
        except (OSError, ValueError):
            return None
        data = out.getvalue()
        with self._lock:
            if len(self._cache) >= 4:
                self._cache.pop(next(iter(self._cache)))
            self._cache[key] = data
        return data



class Sampler:
    """Keeps CPU percent and network rates fresh in the background."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.cpu = 0.0
        self.down_bps = 0
        self.up_bps = 0
        psutil.cpu_percent(interval=None)  # first call primes the counter
        self._last_net = psutil.net_io_counters()
        self._last_time = time.monotonic()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self) -> None:
        while True:
            time.sleep(SAMPLE_SECONDS)
            cpu = psutil.cpu_percent(interval=None)
            net = psutil.net_io_counters()
            now = time.monotonic()
            elapsed = max(now - self._last_time, 0.001)
            down = max(net.bytes_recv - self._last_net.bytes_recv, 0) / elapsed
            up = max(net.bytes_sent - self._last_net.bytes_sent, 0) / elapsed
            with self._lock:
                self.cpu = cpu
                self.down_bps = int(down)
                self.up_bps = int(up)
            self._last_net = net
            self._last_time = now

    def snapshot(self) -> tuple[float, int, int]:
        with self._lock:
            return self.cpu, self.down_bps, self.up_bps


def list_disks() -> list[list]:
    """Real drives only: no CD drives, nothing under 1 GB, one entry per device."""
    disks = []
    seen = set()
    for part in psutil.disk_partitions(all=False):
        if "cdrom" in part.opts or not part.fstype or part.device in seen:
            continue
        try:
            usage = psutil.disk_usage(part.mountpoint)
        except (PermissionError, OSError):
            continue
        if usage.total < GB:
            continue
        seen.add(part.device)
        name = part.mountpoint.rstrip("\\") if len(part.mountpoint) <= 3 else part.mountpoint
        disks.append([name, round(usage.used / GB, 1), round(usage.total / GB, 1)])
        if len(disks) >= MAX_DISKS:
            break
    return disks


def claude_usage(now: float) -> dict | None:
    """The saved Claude Code limits as {"session": [pct, reset], "week": [pct, reset]}."""
    try:
        saved = json.loads(CLAUDE_LIMITS_PATH.read_text(encoding="utf-8"))
        limits = saved["rate_limits"]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    out = {}
    for key, name in (("five_hour", "session"), ("seven_day", "week")):
        window = limits.get(key) if isinstance(limits, dict) else None
        if not isinstance(window, dict):
            continue
        try:
            pct = int(round(float(window.get("used_percentage") or 0)))
            reset = int(window.get("resets_at") or 0)
        except (TypeError, ValueError):
            continue
        if reset and reset <= now:
            pct, reset = 0, 0  # the window has reset since Claude Code last reported it
        out[name] = [max(0, min(100, pct)), reset]
    if "session" not in out and "week" not in out:
        return None
    # A window Claude Code has not reported is null, so the reader shows "–" rather than 0 %
    # (the five-hour window went missing while it stood at 25 %, 2026-11-25).
    out.setdefault("session", None)
    out.setdefault("week", None)
    # When Claude Code last reported the limits: they only move while a Claude Code session runs.
    try:
        out["saved"] = int(saved.get("saved") or 0)
    except (TypeError, ValueError):
        out["saved"] = 0
    return out


def build_stats(sampler: Sampler, host: str, photos: "Photos | None" = None) -> dict:
    cpu, down, up = sampler.snapshot()
    mem = psutil.virtual_memory()
    now = time.time()
    return {
        "v": 1,
        "host": host,
        "ts": int(time.time()),
        "cpu": int(round(cpu)),
        "ram": [round(mem.used / GB, 1), round(mem.total / GB, 1)],
        "disks": list_disks(),
        "net": [down, up],
        "uptime": int(time.time() - psutil.boot_time()),
        "claude": claude_usage(now),
        "photos": photos.count() if photos else 0,
    }


def lan_address() -> str:
    """The address other LAN hosts reach this PC at. A UDP connect sends no packet."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("10.255.255.255", 1))
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def make_handler(sampler: Sampler, host: str, photos: Photos):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802 (http.server naming)
            path = self.path.split("?", 1)[0]
            if path.startswith("/v1/photos/") and path.endswith(".png"):
                try:
                    index = int(path[len("/v1/photos/"):-len(".png")])
                except ValueError:
                    self.send_error(404)
                    return
                data = photos.png(index)
                if data is None:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            if path != "/v1/stats":
                self.send_error(404)
                return
            body = json.dumps(build_stats(sampler, host, photos), separators=(",", ":")).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt: str, *args) -> None:
            pass  # quiet: the reader polls every minute

    return Handler


def advertise(port: int, host: str, address: str):
    try:
        from zeroconf import ServiceInfo, Zeroconf
    except ImportError:
        print("zeroconf not installed: mDNS off, set the reader's agent address by hand")
        return None
    info = ServiceInfo(
        SERVICE_TYPE,
        f"{host}.{SERVICE_TYPE}",
        addresses=[socket.inet_aton(address)],
        port=port,
        properties={"v": "1", "path": "/v1/stats"},
        server=f"{host.lower()}.local.",
    )
    zc = Zeroconf()
    zc.register_service(info)
    return zc, info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--bind", default="0.0.0.0", help="interface to listen on (default: all)")
    parser.add_argument("--name", default=socket.gethostname(), help="name shown on the reader")
    parser.add_argument("--no-mdns", action="store_true", help="do not advertise _xdesk._tcp")
    parser.add_argument("--photos", default=None, help="folder of photos for Desk's photo frame (needs Pillow)")
    args = parser.parse_args()

    sampler = Sampler()
    photos = Photos(args.photos)
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(sampler, args.name, photos))
    address = lan_address()
    mdns = None if args.no_mdns else advertise(args.port, args.name, address)
    print(f"Desk agent '{args.name}' on http://{address}:{args.port}/v1/stats"
          f"{'' if mdns else ' (no mDNS)'}"
          f"{f', {photos.count()} photos in {photos.folder}' if photos.folder else ''}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if mdns:
            zc, info = mdns
            zc.unregister_service(info)
            zc.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
