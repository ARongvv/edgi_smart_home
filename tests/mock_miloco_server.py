#!/usr/bin/env python3
"""Local MiLoCo gateway mock for board bring-up and repeatable demo checks."""

import argparse
import json
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from http.client import RemoteDisconnected


class MockState:
    def __init__(self, token, bound=True, drop=False):
        self.token = token
        self.bound = bound
        self.drop = drop
        self.power = False
        self.brightness = 50


class Handler(BaseHTTPRequestHandler):
    server_version = "MockMiLoCo/1.0"

    def log_message(self, fmt, *args):
        print("[mock-miloco] " + fmt % args)

    def send_json(self, payload, status=200):
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def authorized(self):
        expected = "Bearer " + (self.server.state.token or "-")
        if self.headers.get("Authorization") != expected:
            self.send_json({"code": 401, "message": "unauthorized"}, 401)
            return False
        return True

    def do_GET(self):
        state = self.server.state
        if state.drop:
            self.close_connection = True
            return
        if not self.authorized():
            return
        path = self.path.split("?", 1)[0]
        if path == "/api/miot/status":
            self.send_json({"code": 0, "data": {"is_bound": state.bound}})
        elif path == "/api/miot/device_list":
            self.send_json({"code": 0, "data": [{
                "did": "mock-light-1", "name": "Mock Light",
                "room_name": "Living Room", "online": True,
            }]})
        elif path == "/api/miot/devices/mock-light-1/spec":
            self.send_json({"code": 0, "data": {
                "did": "mock-light-1", "category": "light", "spec": {
                    "prop.2.1": {
                        "type_name": "on", "description": "Power",
                        "format": "bool", "readable": True, "writeable": True,
                    },
                    "prop.2.2": {
                        "type_name": "brightness", "description": "Brightness",
                        "format": "uint8", "readable": True, "writeable": True,
                        "value_range": [1, 100, 1],
                    },
                },
            }})
        elif path == "/api/miot/devices/mock-light-1/status":
            self.send_json({"code": 0, "data": {"properties": [
                {"iid": "prop.2.1", "value": state.power, "code": 0},
                {"iid": "prop.2.2", "value": state.brightness, "code": 0},
            ]}})
        else:
            self.send_json({"code": 404, "message": "not found"}, 404)

    def do_POST(self):
        state = self.server.state
        if state.drop:
            self.close_connection = True
            return
        if not self.authorized():
            return
        if self.path != "/api/miot/devices/mock-light-1/control":
            self.send_json({"code": 404, "message": "not found"}, 404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length))
            operation = request["type"]
            iid = request["iid"]
            if operation == "set_property":
                if iid == "prop.2.1" and isinstance(request["value"], bool):
                    state.power = request["value"]
                elif iid == "prop.2.2" and isinstance(request["value"], int) \
                        and 1 <= request["value"] <= 100:
                    state.brightness = request["value"]
                else:
                    raise ValueError("invalid property")
                result = {"did": "mock-light-1", "iid": iid, "code": 0}
                self.send_json({"code": 0, "data": {"results": [result]}})
            elif operation == "call_action":
                self.send_json({"code": 0, "data": {"result": {
                    "did": "mock-light-1", "iid": iid, "code": 0,
                }}})
            else:
                raise ValueError("invalid operation")
        except (KeyError, ValueError, TypeError, json.JSONDecodeError):
            self.send_json({"code": 400, "message": "invalid control"}, 400)


def make_server(host, port, token, bound=True, drop=False):
    server = ThreadingHTTPServer((host, port), Handler)
    server.state = MockState(token, bound, drop)
    return server


def request(port, path, token="demo-token", body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(
        "http://127.0.0.1:%d%s" % (port, path), data=data,
        headers={"Authorization": "Bearer " + token,
                 "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=2) as response:
        return response.status, json.load(response)


def self_test():
    server = make_server("127.0.0.1", 0, "demo-token")
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    port = server.server_address[1]
    try:
        status, payload = request(port, "/api/miot/status")
        assert status == 200 and payload["data"]["is_bound"]
        _, payload = request(port, "/api/miot/device_list")
        assert payload["data"][0]["did"] == "mock-light-1"
        _, payload = request(port, "/api/miot/devices/mock-light-1/spec")
        assert payload["data"]["spec"]["prop.2.1"]["format"] == "bool"
        _, payload = request(port, "/api/miot/devices/mock-light-1/control", body={
            "type": "set_property", "iid": "prop.2.1", "value": True,
        })
        assert payload["data"]["results"][0]["code"] == 0
        _, payload = request(port, "/api/miot/devices/mock-light-1/status?iid=prop.2.1")
        assert payload["data"]["properties"][0]["value"] is True
        try:
            request(port, "/api/miot/status", token="wrong")
            raise AssertionError("invalid token was accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 401
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

    server = make_server("127.0.0.1", 0, "demo-token", drop=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        try:
            request(server.server_address[1], "/api/miot/status")
            raise AssertionError("disconnected request unexpectedly succeeded")
        except (RemoteDisconnected, urllib.error.URLError):
            pass
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    print("mock_miloco_server: status/list/spec/control/readback/token/disconnect passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1810)
    parser.add_argument("--token", default="demo-token")
    parser.add_argument("--unbound", action="store_true")
    parser.add_argument("--drop", action="store_true",
                        help="close every connection without an HTTP response")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    server = make_server(args.host, args.port, args.token, not args.unbound, args.drop)
    print("Mock MiLoCo listening on http://%s:%d (token %s)" %
          (args.host, args.port, "required" if args.token else "disabled"))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
