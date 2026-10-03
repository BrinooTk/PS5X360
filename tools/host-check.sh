#!/usr/bin/env bash
# Runs one game headless on the PC (Linux container, software Vulkan) and
# summarises it: frames, sound, warnings, a crash dump if any, screenshots.
#   bash tools/host-check.sh "<folder under dist/PPSA50011/assets/roms>" [seconds] [tag]
# With a tag the output goes to its own folder (for runs with XBOX360PS5_CONFIG).
# Output in build/host-check/<folder>/: run.log, shot-NNN.png, summary.txt.
set -uo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
game_dir="$root/dist/PPSA50011/assets/roms/$1"
seconds=${2:-90}
out="$root/build/host-check/$1${3:+ ($3)}"
rm -rf "$out" && mkdir -p "$out"
executable=$(find "$game_dir" -maxdepth 3 -iname 'default.xex' | head -1)
[[ -z $executable ]] && executable=$(find "$game_dir" -maxdepth 3 -iname '*.xex' | head -1)
[[ -z $executable ]] && executable=$(find "$game_dir" -maxdepth 2 -iname '*.iso' | head -1)
if [[ -z $executable ]]; then echo "no executable in $game_dir" | tee "$out/summary.txt"; exit 2; fi
cd /tmp
timeout $((seconds + 90)) "$root/build/kernel-host/xenia-engine-integration" --run-xex "$executable" "$seconds" "$out" > "$out/run.log" 2>&1 &
runner=$!
# With gdb available (image xbox360ps5-hostdebug), every thread's stack is
# saved ten seconds before the end: where a stalled game is waiting.
if command -v gdb > /dev/null; then
  sleep $((seconds - 10 > 5 ? seconds - 10 : 5))
  pid=$(pidof xenia-engine-integration | cut -d" " -f1)
  [[ -n $pid ]] && gdb -p "$pid" -batch -ex "set pagination off" -ex "thread apply all bt 12" > "$out/threads.txt" 2>&1
fi
wait $runner
status=$?
rm -f /tmp/xenia_memory_* /tmp/xenia_code_cache_*
{
  echo "game: $1"
  echo "executable: $executable"
  echo "exit: $status"
  echo "frames: $(grep -a -c 'VdSwap(' "$out/run.log")"
  echo "screenshots: $(ls "$out"/shot-*.png 2>/dev/null | wc -l), distinct sizes: $(stat -c %s "$out"/shot-*.png 2>/dev/null | sort -u | wc -l)"
  echo "audio peaks: $(grep -a 'Audio level' "$out/run.log" | sed 's/.*peak \([0-9]*\) .*/\1/' | tr '\n' ' ')"
  echo "title: $(grep -a -m1 'Title name' "$out/run.log" | cut -c1-120)"
  echo "--- errors and warnings (counted)"
  grep -a '[!w]> ' "$out/run.log" | sed 's/^[!w]> [0-9A-F]\{8\} /&/; s/([^)]*)//g; s/[0-9A-F]\{8\}/N/g' | cut -c1-150 | sort | uniq -c | sort -rn | head -25
  echo "--- unimplemented kernel calls used"
  grep -a 'unimplemented\|Unimplemented' "$out/run.log" | sed 's/[0-9A-F]\{8\}//g' | sort | uniq -c | sort -rn | head -15
  echo "--- crash"
  grep -a -A3 'CRASH DUMP\|Host PC' "$out/run.log" | head -12
} > "$out/summary.txt"
cat "$out/summary.txt"
