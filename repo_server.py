#!/usr/bin/env python3
"""TinyRepo/1 file server for the educational i386 OS.

Wire format: client sends ``TINY/1 GET /path\n``; server replies
``TINY/1 200 <byte-count>\n`` followed by exactly that many bytes.
"""

from __future__ import annotations

import argparse
import json
import socketserver
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "repo"


class Handler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        request = self.rfile.readline(512).decode("ascii", "replace").strip().split()
        if len(request) != 3 or request[:2] != ["TINY/1", "GET"]:
            self.wfile.write(b"TINY/1 400 0\n")
            return
        name = request[2].lstrip("/")
        if not name or ".." in Path(name).parts:
            self.wfile.write(b"TINY/1 400 0\n")
            return
        path = (ROOT / name).resolve()
        if ROOT.resolve() not in path.parents or not path.is_file():
            self.wfile.write(b"TINY/1 404 0\n")
            return
        data = path.read_bytes()
        self.wfile.write(f"TINY/1 200 {len(data)}\n".encode("ascii"))
        self.wfile.write(data)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve the TinyOS package repository")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    with Server((args.host, args.port), Handler) as server:
        print(f"TinyRepo/1 serving {ROOT} on {args.host}:{args.port}")
        print(f"QEMU host address: 10.0.2.2:{args.port}")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nStopping TinyRepo/1")


if __name__ == "__main__":
    main()
