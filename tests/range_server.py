#!/usr/bin/env python3
"""Range-capable HTTP server used by the inv downloader validator tests.

The downloader resumes a partially received part with "Range: bytes=<have>-<to>"
plus "If-Range: <etag>" (see src/core/remote/download.cpp). This server lets a
test reproduce the dangerous case: the release asset is replaced while a part is
half received, so the retry carries a validator that no longer matches.

Endpoints
  GET /data.bin          the object; supports Range/206, ETag, Last-Modified
  GET /hash?gen=0|1      sha256 of generation 0 or 1 as plain text

Knobs
  --size N               object size for generation 0 (default 200000)
  --size2 N              object size for generation 1 (default: same as --size)
  --etag1 "\"v1\""      ETag of generation 0 (default '"v1"', quotes included)
  --etag2 "\"v2\""      ETag of generation 1
  --flip-after-range N   after the N-th Range request the object is replaced
                         (new bytes, new ETag, optional new size)
  --drop-at-range K      on the K-th Range request send only --drop-bytes body
                         bytes and close the socket (truncated transfer)

Range requests are counted in arrival order and include the downloader's
one-byte probe (bytes=0-0), so a typical repro uses:
  --drop-at-range 2 --drop-bytes 50000 --flip-after-range 2
which truncates the first segment and replaces the object before the retry.

Zero dependencies, stdlib only, Python 3.8+.
"""

import argparse
import hashlib
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATE = {
    "size": 200000,
    "size2": 0,
    "etag1": '"v1"',
    "etag2": '"v2"',
    "flip_after": 0,
    "drop_at": 0,
    "drop_bytes": 0,
    "flipped": False,
    "range_count": 0,
    "lock": threading.Lock(),
    "cache": {},
}

LAST_MODIFIED = "Wed, 01 Jan 2025 00:00:00 GMT"


def content(gen, size):
    """Deterministic bytes so a test can hash them without transferring twice."""
    key = (gen, size)
    with STATE["lock"]:
        cached = STATE["cache"].get(key)
    if cached is None:
        seed_a, seed_b = (7, 11) if gen == 0 else (13, 3)
        cached = bytes(((i * seed_a + seed_b) % 251) for i in range(size))
        with STATE["lock"]:
            STATE["cache"][key] = cached
    return cached


def gen_size(gen):
    if gen == 1 and STATE["size2"]:
        return STATE["size2"]
    return STATE["size"]


def gen_etag(gen):
    return STATE["etag2"] if gen else STATE["etag1"]


def parse_range(text, size):
    """Returns (start, end) inclusive, or (None, None) when unsatisfiable."""
    if not text or not text.startswith("bytes="):
        return None, None
    spec = text[6:].split(",")[0].strip()
    if "-" not in spec:
        return None, None
    first, last = spec.split("-", 1)
    if first == "":
        try:
            suffix = int(last)
        except ValueError:
            return None, None
        suffix = min(suffix, size)
        if suffix <= 0:
            return None, None
        return size - suffix, size - 1
    try:
        start = int(first)
    except ValueError:
        return None, None
    if last == "":
        end = size - 1
    else:
        try:
            end = int(last)
        except ValueError:
            return None, None
    end = min(end, size - 1)
    if start >= size or start > end:
        return None, None
    return start, end


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def finish(self):
        try:
            BaseHTTPRequestHandler.finish(self)
        except Exception:
            pass

    def _send(self, status, headers, body):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if body:
            try:
                self.wfile.write(body)
            except Exception:
                self.close_connection = True

    def _maybe_flip(self, count):
        if STATE["flip_after"] and count >= STATE["flip_after"]:
            with STATE["lock"]:
                STATE["flipped"] = True

    def do_GET(self):
        try:
            self._route()
        except Exception:
            self.close_connection = True

    def _route(self):
        if self.path.startswith("/hash"):
            gen = 1 if "gen=1" in self.path else 0
            digest = hashlib.sha256(content(gen, gen_size(gen))).hexdigest().encode()
            self._send(200, [("Content-Length", str(len(digest)))], digest)
            return
        if not self.path.startswith("/data.bin"):
            self._send(404, [("Content-Length", "9")], b"not found")
            return

        rng = self.headers.get("Range")
        if_range = self.headers.get("If-Range")
        with STATE["lock"]:
            gen = 1 if STATE["flipped"] else 0
            count = 0
            if rng:
                STATE["range_count"] += 1
                count = STATE["range_count"]
        size = gen_size(gen)
        etag = gen_etag(gen)
        data = content(gen, size)
        common = [("Accept-Ranges", "bytes"), ("ETag", etag),
                  ("Last-Modified", LAST_MODIFIED), ("Connection", "close")]

        if not rng:
            self._send(200, [("Content-Length", str(size))] + common, data)
            return

        # RFC 7233: when If-Range does not match, ignore Range and send the whole
        # current object with 200. This is exactly what a replaced asset does.
        if if_range and if_range.strip() != etag:
            self._send(200, [("Content-Length", str(size))] + common, data)
            self._maybe_flip(count)
            return

        start, end = parse_range(rng, size)
        if start is None:
            self._send(416, [("Content-Range", "bytes */" + str(size)),
                             ("Content-Length", "0")] + common, b"")
            return
        chunk = data[start:end + 1]
        headers = [("Content-Range", "bytes %d-%d/%d" % (start, end, size)),
                   ("Content-Length", str(len(chunk)))] + common
        self.send_response(206)
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()

        if STATE["drop_at"] and count == STATE["drop_at"] and STATE["drop_bytes"]:
            cut = min(STATE["drop_bytes"], len(chunk))
            try:
                self.wfile.write(chunk[:cut])
                self.wfile.flush()
            except Exception:
                pass
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except Exception:
                pass
            try:
                self.connection.close()
            except Exception:
                pass
            self._maybe_flip(count)
            return

        try:
            self.wfile.write(chunk)
        except Exception:
            self.close_connection = True
        self._maybe_flip(count)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18090)
    parser.add_argument("--size", type=int, default=200000)
    parser.add_argument("--size2", type=int, default=0)
    parser.add_argument("--etag1", default='"v1"')
    parser.add_argument("--etag2", default='"v2"')
    parser.add_argument("--flip-after-range", type=int, default=0)
    parser.add_argument("--drop-at-range", type=int, default=0)
    parser.add_argument("--drop-bytes", type=int, default=0)
    args = parser.parse_args()

    STATE["size"] = args.size
    STATE["size2"] = args.size2
    STATE["etag1"] = args.etag1
    STATE["etag2"] = args.etag2
    STATE["flip_after"] = args.flip_after_range
    STATE["drop_at"] = args.drop_at_range
    STATE["drop_bytes"] = args.drop_bytes

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("range_server on http://%s:%d size=%d size2=%d flip_after=%d drop_at=%d drop_bytes=%d"
          % (args.host, args.port, args.size, args.size2 or args.size,
             args.flip_after_range, args.drop_at_range, args.drop_bytes), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
