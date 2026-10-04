"""Exercise the actual Canary limiter calculations with both VSync states."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
source = (root / '.deps/xenia-canary/src/xenia/gpu/graphics_system.cc').read_text(encoding='utf-8')
begin = source.index('uint64_t normalized_framerate_limit =')
end = source.index('if (previous_vsync', begin)
calculation = source[begin:end]
begin = source.index('uint64_t sleep_duration_ns =', end)
end = source.index('// Sleeping a flat period', begin)
sleep = source[begin:end]
out = root / 'build/vsync-check'
out.mkdir(exist_ok=True)
test = '''#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "xbox360ps5/patch_runtime.hpp"
namespace cvars { uint64_t framerate_limit; }
int main() {
  for (bool override_off : {false,true}) for (bool preference : {false,true}) {
    xbox360ps5::patch_vsync_off=override_off;
    const bool effective_vsync=xbox360ps5::EffectiveVsync(preference);
    cvars::framerate_limit=0;
''' + calculation + sleep + '''
    // With an unlimited render setting, guest interrupts still run at 60 Hz.
    if (!normalized_framerate_limit) sleep_duration_ns=1000000;
    if (sleep_duration_ns < 16666666 || sleep_duration_ns > 16666667) {
      std::printf("FAIL: VSync %d, patch override %d: guest pacing %llu ns\\n",
        preference,override_off,(unsigned long long)sleep_duration_ns); return 1;
    }
  }
  std::puts("PASS: 60 Hz guest pacing with VSync on/off and patch override");
}
'''
(out / 'test.cc').write_text(test,encoding='utf-8')
subprocess.run(['clang++-18','-std=c++20','-I'+str(root/'include'),str(out/'test.cc'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
