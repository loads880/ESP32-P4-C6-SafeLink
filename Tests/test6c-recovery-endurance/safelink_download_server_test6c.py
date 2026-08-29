#!/usr/bin/env python3
"""Deterministic local HTTP workload for ESP32-P4 SafeLink testing."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import socket
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

MIB = 1024 * 1024
BLOCK = bytes(range(256)) * 256  # 65,536 deterministic bytes
ALLOWED_MIB = {1, 5, 25, 50, 100}
PATH_RE = re.compile(r"^/download/(1|5|25|50|100)MiB\.bin$")
FAULT_PATH_RE = re.compile(r"^/fault/drop/5MiB-at-(64KiB|1MiB|4MiB)\.bin$")
FAULT_CUTS = {"64KiB": 64 * 1024, "1MiB": MIB, "4MiB": 4 * MIB}


def digest_for_size(size: int) -> str:
    digest = hashlib.sha256()
    complete, remainder = divmod(size, len(BLOCK))
    for _ in range(complete):
        digest.update(BLOCK)
    digest.update(BLOCK[:remainder])
    return digest.hexdigest()


DIGESTS = {mib: digest_for_size(mib * MIB) for mib in ALLOWED_MIB}


class SafeLinkHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "SafeLinkDownload/1.2-test6c"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"{self.client_address[0]} - {fmt % args}")

    def _send_json(self, status: int, value: object) -> None:
        body = json.dumps(value, indent=2).encode("utf-8") + b"\n"
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/health":
            self._send_json(200, {"service": "safelink-download", "status": "ok", "version": "1.2-test6c"})
            return
        if path == "/manifest.json":
            self._send_json(200, {
                "pattern": "bytes 0x00 through 0xFF repeated",
                "faults": [
                    {
                        "path": f"/fault/drop/5MiB-at-{name}.bin",
                        "declared_bytes": 5 * MIB,
                        "disconnect_after_bytes": cutoff,
                    }
                    for name, cutoff in FAULT_CUTS.items()
                ],
                "files": [
                    {"path": f"/download/{mib}MiB.bin", "bytes": mib * MIB, "sha256": DIGESTS[mib]}
                    for mib in sorted(ALLOWED_MIB)
                ],
            })
            return

        match = PATH_RE.match(path)
        fault_match = FAULT_PATH_RE.match(path)
        if not match and not fault_match:
            self._send_json(404, {"error": "not_found", "try": "/manifest.json"})
            return

        fault_drop = fault_match is not None
        fault_cutoff = FAULT_CUTS[fault_match.group(1)] if fault_drop else 0
        total = 5 * MIB if fault_drop else int(match.group(1)) * MIB
        start, end, status = 0, total - 1, 200
        range_header = self.headers.get("Range")
        if range_header:
            range_match = re.fullmatch(r"bytes=(\d+)-(\d*)", range_header.strip())
            if not range_match:
                self.send_error(416, "Only one explicit byte range is supported")
                return
            start = int(range_match.group(1))
            end = int(range_match.group(2)) if range_match.group(2) else total - 1
            if start >= total or end < start:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{total}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            end = min(end, total - 1)
            status = 206

        length = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-SafeLink-SHA256", DIGESTS[total // MIB])
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{total}")
        self.end_headers()

        if self.command == "HEAD":
            return

        position = start
        send_length = min(length, fault_cutoff) if fault_drop else length
        remaining = send_length
        try:
            while remaining:
                block_offset = position % len(BLOCK)
                count = min(remaining, len(BLOCK) - block_offset)
                self.wfile.write(BLOCK[block_offset:block_offset + count])
                position += count
                remaining -= count
            if fault_drop:
                self.wfile.flush()
                self.close_connection = True
                print(f"{self.client_address[0]} - TEST 6C forced disconnect after {send_length} of {length} bytes")
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                self.connection.close()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            self.close_connection = True
            print(f"{self.client_address[0]} - client disconnected after {send_length - remaining} of {length} bytes")


def local_addresses() -> list[str]:
    addresses = set()
    try:
        for item in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            address = item[4][0]
            if not address.startswith("127."):
                addresses.add(address)
    except socket.gaierror:
        pass
    return sorted(addresses)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="address to bind (default: all interfaces)")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.bind, args.port), SafeLinkHandler)
    print("SafeLink deterministic download server 1.2 - Test 6C endurance faults")
    print(f"Listening on {args.bind}:{args.port}")
    for address in local_addresses():
        print(f"P4 health:   http://{address}:{args.port}/health")
        print(f"P4 manifest: http://{address}:{args.port}/manifest.json")
        print(f"P4 test:     http://{address}:{args.port}/download/5MiB.bin")
        for name in FAULT_CUTS:
            print(f"P4 fault:    http://{address}:{args.port}/fault/drop/5MiB-at-{name}.bin")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
