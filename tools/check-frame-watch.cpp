// SPDX-License-Identifier: MIT
#include "xbox360ps5/frame_watch.hpp"
#include "xbox360ps5/cover_geometry.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
  using W=xbox360ps5::FrameWatch;
  W initial(0,0);
  assert(initial.Observe(0,44)==W::none);
  assert(initial.Observe(0,45)==W::capture); // Regression: old `else if(frames)` never reached this.
  assert(initial.Observe(0,47)==W::none);
  assert(initial.Observe(0,48)==W::finish_capture);
  assert(initial.Observe(0,100)==W::none); // Bounded log capture, not an endless trace.
  assert(initial.Notice(100));
  assert(initial.Observe(1,101)==W::recovered);
  assert(!initial.Notice(102));
  assert(initial.Observe(1,116)==W::capture);
  assert(initial.Observe(2,117)==W::recovered); // Resume before the second diagnostic pass.
  W loading(0,0);
  assert(loading.Observe(1,30)==W::none);
  assert(loading.Observe(2,44)==W::none);
  for(float x:{-9.4f,-1.f,-0.001f,0.f,0.001f,1.f,9.4f}) {
    const auto box=xbox360ps5::covers3d::Project(x);
    for(const auto& face:{box.front,box.side,box.top}) for(const auto& p:face)
      assert(std::isfinite(p.x)&&std::isfinite(p.y)&&p.depth>0);
  }
  const auto center=xbox360ps5::covers3d::Project(0).front;
  const auto near=xbox360ps5::covers3d::Project(1).front;
  const auto far=xbox360ps5::covers3d::Project(2).front;
  assert(center[2].y-center[1].y > near[2].y-near[1].y);
  assert(near[2].y-near[1].y > far[2].y-far[1].y);
  assert(near[1].x > far[0].x); // Neighbours overlap, unlike the old spaced boxes.
  assert(std::fabs(near[0].y-near[1].y)<0.001f); // Fronts stay parallel.
  puts("PASS: initial zero-frame loading, bounded capture, recovery, normal progress and projected cuboid range");
}
