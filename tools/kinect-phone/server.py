"""Loopback camera research relay: poses only, to one explicit LAN PS5.

No guest sensor is advertised. No camera images, cloud upload or pose files.
"""
import argparse
import functools
import ipaddress
import json
import math
import re
import struct
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def pack_frame(frame):
    session, sequence = frame.get('session'), frame.get('sequence')
    if any(type(x) is not int or not 0 < x <= 0xFFFFFFFF for x in (session, sequence)):
        raise ValueError('invalid sequence/session')
    tracked = frame.get('tracked')
    if type(tracked) is not bool:
        raise ValueError('invalid tracking state')
    joints = frame.get('joints')
    if not isinstance(joints, list) or len(joints) != 20:
        raise ValueError('expected 20 joints')
    values = []
    for joint in joints:
        if not isinstance(joint, dict):
            raise ValueError('invalid joint')
        for field in ('x', 'y', 'z', 'confidence'):
            value = joint.get(field)
            if type(value) not in (int, float) or not math.isfinite(value):
                raise ValueError('invalid coordinate')
            if (field == 'confidence' and not 0 <= value <= 1) or (field != 'confidence' and abs(value) > 4):
                raise ValueError('coordinate outside bounds')
            values.append(value)
    return b'XMP1' + struct.pack('<III80f', sequence, session, int(tracked), *values)


class Relay(ThreadingHTTPServer):
    daemon_threads = True
    key = ''
    def upstream(self, path, body=None, key=None):
        request = Request(self.console + path, data=body,
                          headers={'X-Key': self.key if key is None else key,
                                   'Content-Type': 'application/octet-stream'})
        try:
            with urlopen(request, timeout=2) as response:
                return response.status, json.loads(response.read(8192))
        except HTTPError as error:
            if error.code == 403:
                # A restarted emulator rotates its key. Never keep reporting
                # this expired connection as paired, or log the rejected key.
                self.key = ''
                return 403, {'error': 'Pairing expired. Enter the current key shown on the PS5 and reconnect.',
                             'code': 'pairing_expired', 'paired': False}
            return error.code, {'error': 'Console rejected request', 'status': error.code}
        except (URLError, OSError, ValueError):
            return 502, {'error': 'Console unavailable; check address, web page and build version'}


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass  # Do not log pairing keys or camera data.

    def reply(self, status, data):
        payload = json.dumps(data).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def host_ok(self):
        port = self.server.server_port
        return self.headers.get('Host') in (f'localhost:{port}', f'127.0.0.1:{port}')

    def do_GET(self):
        if not self.host_ok():
            return self.reply(403, {'error': 'Loopback host required'})
        if self.path == '/relay/status':
            if not self.server.key:
                return self.reply(200, {'paired': False})
            status, result = self.server.upstream('/api/motion/status')
            return self.reply(status, result)
        super().do_GET()

    def do_POST(self):
        origins = (f'http://localhost:{self.server.server_port}', f'http://127.0.0.1:{self.server.server_port}')
        if not self.host_ok() or self.headers.get('Origin') not in origins:
            return self.reply(403, {'error': 'Local camera page required'})
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 8192:
                raise ValueError('invalid body size')
            self.connection.settimeout(3)
            data = json.loads(self.rfile.read(size))
            if not isinstance(data, dict):
                raise ValueError('expected object')
            if self.path == '/relay/connect':
                key = data.get('key', '')
                if not isinstance(key, str) or not re.fullmatch('[a-f0-9]{6}', key):
                    raise ValueError('Use the six-character key shown on the PS5')
                status, result = self.server.upstream('/api/motion/status', key=key)
                if status == 200:
                    self.server.key = key
                return self.reply(status, result)
            if self.path == '/relay/frame':
                if not self.server.key:
                    return self.reply(409, {'error': 'Pair with the console first'})
                payload = pack_frame(data)
                status, result = self.server.upstream('/api/motion/frame', payload)
                return self.reply(status, result)
            return self.reply(404, {'error': 'Unknown route'})
        except (ValueError, TypeError, OSError):
            self.reply(400, {'error': 'Invalid research frame or pairing request'})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--console', required=True)
    parser.add_argument('--console-port', type=int, default=8360)
    parser.add_argument('--port', type=int, default=8780)
    args = parser.parse_args()
    ip = ipaddress.IPv4Address(args.console)
    if not any(ip in ipaddress.ip_network(net) for net in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')):
        parser.error('Specify a private LAN IPv4 console address')
    if not 8360 <= args.console_port <= 8367:
        parser.error('Console web settings port must be 8360–8367')
    folder = str(Path(__file__).resolve().parent)
    server = Relay(('127.0.0.1', args.port), functools.partial(Handler, directory=folder))
    server.console = f'http://{ip}:{args.console_port}'
    print(f'Motion research page: http://localhost:{args.port} (poses only; guest Kinect unavailable)', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
