// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace xbox360ps5 {
// Schedules diagnostics; lack of swaps alone is not proof of deadlock.
// Never forcibly unblock waits or terminate the guest.
class FrameWatch {
 public:
  enum Action { none,capture,finish_capture,recovered };
  FrameWatch(uint64_t frames,double now):frames_(frames),last_progress_(now) {}
  Action Observe(uint64_t frames,double now) {
    if(frames!=frames_) {
      frames_=frames;last_progress_=now;
      const bool stalled=stage_!=0;
      stage_=0;capture_started_=0;
      return stalled?recovered:none;
    }
    if(!stage_ && now-last_progress_ >= (frames_?15:45)) {
      stage_=1;capture_started_=now;return capture;
    }
    if(stage_==1 && now-capture_started_>=3) {stage_=2;return finish_capture;}
    return none;
  }
  bool Notice(double now) const {return now-last_progress_>=45;}
 private:
  uint64_t frames_;
  double last_progress_,capture_started_=0;
  int stage_=0;
};
}
