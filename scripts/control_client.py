#!/usr/bin/env python3
"""Small stdlib client for the opt-in local engine API (not MCP)."""

import argparse
import json
import socket
import time
import uuid


PENDING_STATES = frozenset({"queued", "applied", "evaluating", "committing", "rolling_back"})


class Client:
    def __init__(self, path, timeout=5):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")

    def close(self):
        try:
            self.stream.close()
        except OSError:
            pass  # A dead renderer must not hide the request's original error.
        finally:
            self.socket.close()

    def call(self, method, params=None, request_id=None):
        request = {"id": request_id or uuid.uuid4().hex, "method": method}
        if params is not None:
            request["params"] = params
        self.stream.write(json.dumps(request, allow_nan=False).encode() + b"\n")
        self.stream.flush()
        line = self.stream.readline(8 * 1024 * 1024 + 1)
        if not line or not line.endswith(b"\n"):
            raise ConnectionError("connection closed or response exceeded limit")
        result = json.loads(line)
        if result.get("id") != request["id"]:
            raise ValueError("response ID mismatch")
        return result

    def wait(self, request_id, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            response = self.call("get_request", {"request_id": request_id})
            if not response["ok"]:
                return response
            receipt = response["result"]
            if receipt["status"] not in PENDING_STATES:
                return receipt
            time.sleep(0.03)
        raise TimeoutError(f"request {request_id} remained pending")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True)
    parser.add_argument("method")
    parser.add_argument("params", nargs="?", default="{}", help="JSON object")
    parser.add_argument("--wait", action="store_true", help="Poll a returned queued/applied receipt")
    args = parser.parse_args()
    client = Client(args.socket)
    try:
        response = client.call(args.method, json.loads(args.params))
        if args.wait and response["status"] in PENDING_STATES:
            response = client.wait(response["id"], timeout=35)
        print(json.dumps(response, indent=2))
        return 0 if response["ok"] else 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
