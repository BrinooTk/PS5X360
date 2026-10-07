"""Production receiver and authenticated HTTP route regression checks."""
import importlib.util
import json
import struct
import subprocess
import time
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

root = Path(__file__).resolve().parents[1]
out = root / 'build/check-motion'
out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('motion_relay', root / 'tools/kinect-phone/server.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
sample = {'session': 123, 'sequence': 1, 'tracked': True,
          'joints': [{'x': .1, 'y': .2, 'z': -.3, 'confidence': .9} for _ in range(20)]}
payload = relay.pack_frame(sample)
assert len(payload) == 336
for bad in ([], {'sequence': True}, {'tracked': 'yes'}, {'joints': []}):
    try:
        relay.pack_frame(dict(sample, **bad) if isinstance(bad, dict) else {})
    except ValueError:
        pass
    else:
        raise AssertionError('malformed relay input accepted')
(out / 'harness.cpp').write_text(r'''
#include "xbox360ps5/motion_input.hpp"
#include "xbox360ps5/web_settings.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
int main(int, char** argv) {
  using namespace xbox360ps5;
  std::string frame(336, char(0)); frame.replace(0,4,"XMP1");
  frame[4]=1; frame[8]=123; frame[12]=1;
  assert(motion::Receive(frame)==motion::Result::disabled);
  motion::SetEnabled(true);
  assert(motion::Receive(frame)==motion::Result::accepted);
  assert(motion::Receive(frame)==motion::Result::replay);
  auto invalid=frame; invalid[4]=2; invalid[19]=char(0x7f); invalid[18]=char(0xc0);
  assert(motion::Receive(invalid)==motion::Result::malformed); // NaN
  assert(motion::Receive(frame.substr(0,335))==motion::Result::malformed);
  assert(motion::Snapshot().tracked);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  assert(!motion::Snapshot().tracked);
  frame[4]=2;frame[12]=0; assert(motion::Receive(frame)==motion::Result::accepted);
  assert(!motion::Snapshot().tracked);
  motion::SetEnabled(false);assert(!motion::Snapshot().sequence);
  motion::SetEnabled(true);
  assert(StartWebSettings(argv[1]));
  std::printf("%s\n%s\n",WebSettingsKey().c_str(),WebSettingsPlainAddress().c_str());std::fflush(stdout);
  std::this_thread::sleep_for(std::chrono::seconds(12));
}
''')
subprocess.run(['clang++-18', '-std=c++20', '-fsanitize=address,undefined', '-pthread',
                '-I'+str(root/'include'), str(out/'harness.cpp'), str(root/'platform/ps5/web_settings.cpp'),
                '-o', str(out/'harness')], check=True)
server = subprocess.Popen([str(out/'harness'), str(out)], stdout=subprocess.PIPE, text=True)
try:
    key = server.stdout.readline().strip()
    address = server.stdout.readline().strip()
    assert key and address, 'receiver harness failed'
    port = address.split(':')[2].split('/')[0]
    base = 'http://127.0.0.1:'+port
    def ask(path, body=None, given=key):
        request = Request(base+path, data=body, headers={} if given is None else {'X-Key': given})
        try:
            with urlopen(request, timeout=3) as reply:
                return reply.status, json.loads(reply.read())
        except HTTPError as error:
            return error.code, json.loads(error.read())
    assert ask('/api/motion/frame', payload, given=None)[0] == 403
    assert ask('/api/motion/frame', payload, given='wrong')[0] == 403
    assert ask('/api/motion/frame', payload)[0] == 200
    assert ask('/api/motion/frame', payload)[0] == 409
    status, state = ask('/api/motion/status')
    assert status == 200 and state['tracked'] and state['packets'] == 1 and not state['guest_sensor_ready']
    assert ask('/api/motion/frame', payload[:-1])[0] == 400
    invalid = bytearray(payload); invalid[16:20] = struct.pack('<f', float('nan'))
    assert ask('/api/motion/frame', bytes(invalid))[0] == 400
    time.sleep(.6)
    assert not ask('/api/motion/status')[1]['tracked']
    assert ask('/api/motion/status', given=None)[0] == 403
    print('PASS: disabled isolation, framing/NaN rejection, duplicate rejection, pose expiry/reset, authenticated real HTTP delivery, little-endian relay conversion (ASAN/UBSAN)')
finally:
    server.terminate()
    server.wait(timeout=3)
