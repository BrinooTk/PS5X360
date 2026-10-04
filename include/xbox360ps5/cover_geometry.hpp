// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace xbox360ps5::covers3d {
struct Vertex { float x, y, depth; };
using Face = std::array<Vertex, 4>;
struct Box { Face front, side, top; bool spine; };
// Parallel fronts, prominent center and overlapping neighbours receding in
// depth, following the supplied Aurora reference (1920x1080 design space).
inline Box Project(float offset, float floor = 690) {
  const float away=std::fabs(offset);
  const float z=1050*(0.32f*std::min(away,1.f)+0.06f*std::max(away-1,0.f));
  const float spacing=away<1 ? away*337 : 337+(away-1)*110;
  const float x=(offset<0?-1.f:1.f)*spacing*(1050+z)/1050;
  const float w=192,h=544,d=2;
  const auto point=[&](float px,float py,float pz) {
    const float depth=1050+z+pz, scale=1050/depth;
    return Vertex{960+(x+px)*scale,floor-h/2+(h/2-py)*scale,depth};
  };
  Box box;
  box.front={point(-w,h,-d),point(w,h,-d),point(w,0,-d),point(-w,0,-d)};
  const float side=offset>=0?-w:w;
  box.spine=offset>=0;
  box.side=box.spine
    ? Face{point(side,h,d),point(side,h,-d),point(side,0,-d),point(side,0,d)}
    : Face{point(side,h,-d),point(side,h,d),point(side,0,d),point(side,0,-d)};
  box.top={point(-w,h,d),point(w,h,d),point(w,h,-d),point(-w,h,-d)};
  return box;
}
}
