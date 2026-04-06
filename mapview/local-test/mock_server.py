#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "data"


def load_json(path: Path) -> Any:
    if not path.exists() or path.stat().st_size == 0:
        return None

    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_json_bytes(path: Path) -> bytes | None:
    if not path.exists() or path.stat().st_size == 0:
        return None

    return path.read_bytes()


ENDPOINT_FILES = {
    "health": ["health.json"],
    "cargo": ["cargo.json"],
    "rupture-cycle": ["rupture-cycle.json", "rupture_cycle.json"],
}


def default_payloads(port: int) -> dict[str, Any]:
    return {
        "health": {
            "ok": True,
            "plugin": "mapview-local-mock",
            "version": 1,
            "world": "LocalTest",
            "port": port,
            "snapshot_generation": 1,
            "marker_count": 0,
            "teleporter_count": 0,
            "player_count": 0,
        },
        "cargo": {
            "generation": 1,
            "world": "LocalTest",
            "reason": "mock",
            "counts": {
                "markers": 0,
                "teleporters": 0,
                "players": 0,
            },
            "map": {
                "content_width": 3547,
                "content_height": 3471,
                "dst_x1": 380,
                "dst_y1": 567,
                "image_width": 4352,
                "image_height": 5120,
            },
            "markers": [],
            "connections": [],
            "teleporters": [],
            "players": [],
        },
        "rupture-cycle": {
            "ok": True,
            "generation": 1,
            "world": "LocalTest",
            "timeline": {
                "cycle_total_seconds": 3240,
                "phase_seconds": {
                    "burning": 30,
                    "cooling": 60,
                    "stabilizing": 600,
                    "stable": 2550,
                },
            },
            "rupture_cycle": {
                "available": False,
                "wave": "mock",
                "stage": "PreWave",
                "step": "None",
                "elapsed_seconds": 0,
                "observed_at_unix_ms": 0,
            },
        },
    }


def resolve_payload(endpoint: str, port: int) -> Any:
    for filename in ENDPOINT_FILES[endpoint]:
        payload = load_json(DATA_DIR / filename)
        if payload is not None:
            return payload

    return default_payloads(port)[endpoint]


def resolve_payload_bytes(endpoint: str) -> bytes | None:
    for filename in ENDPOINT_FILES[endpoint]:
        payload = load_json_bytes(DATA_DIR / filename)
        if payload is not None:
            return payload

    return None


class MockHandler(BaseHTTPRequestHandler):
    server_version = "MapViewMock/0.1"

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"

        if path == "/":
            self.send_json(
                {
                    "ok": True,
                    "service": "mapview-local-mock",
                    "endpoints": ["/health", "/cargo", "/rupture-cycle"],
                    "data_dir": str(DATA_DIR),
                }
            )
            return

        if path in {"/health", "/cargo", "/rupture-cycle"}:
            endpoint = path.lstrip("/")
            port = self.server.server_port
            payload_bytes = resolve_payload_bytes(endpoint)
            if payload_bytes is not None:
                self.send_json_bytes(payload_bytes)
                return
            self.send_json(resolve_payload(endpoint, port))
            return

        self.send_json(
            {"ok": False, "error": "not_found", "path": parsed.path},
            status=HTTPStatus.NOT_FOUND,
        )

    def log_message(self, format: str, *args: Any) -> None:
        return

    def send_json(self, payload: Any, status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload, ensure_ascii=True, indent=2).encode("utf-8")
        self.send_json_bytes(body, status)

    def send_json_bytes(
        self,
        body: bytes,
        status: HTTPStatus = HTTPStatus.OK,
    ) -> None:
        self.send_response(status)
        self.send_cors_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_cors_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local mock API for mapview")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    for endpoint, filenames in ENDPOINT_FILES.items():
        matched_path = next(
            (
                DATA_DIR / name
                for name in filenames
                if (DATA_DIR / name).exists() and (DATA_DIR / name).stat().st_size > 0
            ),
            None,
        )
        if matched_path is not None:
            print(f"{endpoint}: serving file {matched_path.name}", flush=True)
        else:
            print(f"{endpoint}: using built-in fallback payload", flush=True)
    server = ThreadingHTTPServer((args.host, args.port), MockHandler)
    print(
        f"Mapview mock server listening on http://{args.host}:{args.port}",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
