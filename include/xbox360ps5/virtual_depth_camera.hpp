// SPDX-License-Identifier: MIT
// Research virtual camera: estimated poses rasterized as a depth silhouette.
// This is not measured depth and does not guarantee Kinect SDK body recognition.
#pragma once
#include "xbox360ps5/motion_input.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
namespace xbox360ps5::motion {
struct VirtualDepthCalibration {
  std::array<uint8_t,116> registration{};
  float dcmos_emitter_distance=7.5f, dcmos_rcmos_distance=0;
  float reference_distance=120, reference_pixel_size=120.f/(2*525);
  uint16_t const_shift=200;
};
class VirtualDepthCamera {
 public:
  static constexpr size_t kDepthFrameBytes=640*480*11/8, kColorFrameBytes=640*480;
  static std::unique_ptr<VirtualDepthCamera> Open(bool, std::function<void()>) {
    return enabled.load()?std::make_unique<VirtualDepthCamera>():nullptr;
  }
  const VirtualDepthCalibration& calibration() const {return calibration_;}
  static uint16_t RawDepth(float metres) {
    return uint16_t(std::clamp(std::lround(metres*250),1l,2046l));
  }
  static void Render(const Frame& pose, std::vector<uint16_t>& depth) {
    depth.assign(640*480,2047);
    if (!pose.tracked) return;
    struct Point{float x,y,z,confidence;};
    std::array<Point,20> points{};
    for(size_t i=0;i<20;++i){const auto& j=pose.joints[i];const float z=std::clamp(2.5f+j.z,1.f,5.f);
      points[i]={320+525*j.x/z,240-525*(j.y+.1f)/z,z,j.confidence};}
    auto capsule=[&](size_t a,size_t b,float radius){
      const auto& p=points[a];const auto& q=points[b];
      if(p.confidence<.4f||q.confidence<.4f)return;
      const float pixel_radius=525*radius/((p.z+q.z)*.5f);
      const int x0=std::clamp(int(std::floor(std::min(p.x,q.x)-pixel_radius)),0,639);
      const int x1=std::clamp(int(std::ceil(std::max(p.x,q.x)+pixel_radius)),0,639);
      const int y0=std::clamp(int(std::floor(std::min(p.y,q.y)-pixel_radius)),0,479);
      const int y1=std::clamp(int(std::ceil(std::max(p.y,q.y)+pixel_radius)),0,479);
      const float dx=q.x-p.x,dy=q.y-p.y,length=dx*dx+dy*dy;
      for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const float t=length>0?std::clamp(((x-p.x)*dx+(y-p.y)*dy)/length,0.f,1.f):0;
        const float ex=x-p.x-t*dx,ey=y-p.y-t*dy;
        const float distance=ex*ex+ey*ey;
        if(distance>pixel_radius*pixel_radius)continue;
        // Rounded surface, not a flat stick figure.
        const float z=p.z+t*(q.z-p.z)-radius*std::sqrt(std::max(0.f,1-distance/(pixel_radius*pixel_radius)));
        auto& at=depth[y*640+x];at=std::min(at,RawDepth(z));
      }
    };
    capsule(0,2,.19f);capsule(4,8,.12f);capsule(0,12,.12f);capsule(0,16,.12f);
    capsule(2,3,.065f);capsule(3,3,.105f);
    for(auto edge:std::array<std::array<size_t,2>,14>{{{4,5},{5,6},{6,7},{8,9},{9,10},{10,11},{12,13},{13,14},{14,15},{16,17},{17,18},{18,19},{2,4},{2,8}}})
      capsule(edge[0],edge[1],edge[0]>=12?.075f:.045f);
  }
  static void Pack(const std::vector<uint16_t>& raw,std::vector<uint8_t>& packed){
    packed.clear();packed.reserve(kDepthFrameBytes);uint32_t bits=0;int count=0;
    for(uint16_t value:raw){bits=(bits<<11)|(value&2047);count+=11;while(count>=8){count-=8;packed.push_back(uint8_t(bits>>count));}}
  }
  uint32_t CopyDepthFrame(uint32_t after,std::vector<uint8_t>& out){
    const auto sequence=Sequence();if(sequence==after)return 0;
    Render(Snapshot(),depth_);Pack(depth_,out);return sequence;
  }
  uint32_t CopyColorFrame(uint32_t after,std::vector<uint8_t>& out){
    const auto sequence=Sequence();if(sequence==after)return 0;
    // The phone protocol deliberately contains no camera image.
    out.assign(kColorFrameBytes,128);return sequence;
  }
 private:
  uint32_t Sequence() const {
    const auto tick=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_).count()/33;
    return uint32_t(tick%0xfffffffe)+1;
  }
  VirtualDepthCalibration calibration_;
  std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
  std::vector<uint16_t> depth_;
};
}
