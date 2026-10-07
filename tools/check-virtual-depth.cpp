// SPDX-License-Identifier: MIT
#include "xbox360ps5/virtual_depth_camera.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using namespace xbox360ps5::motion;
 std::vector<uint16_t> depth;std::vector<uint8_t> packed;
 Frame f;VirtualDepthCamera::Render(f,depth);assert(depth.size()==640*480);
 for(auto d:depth)assert(d==2047);
 f.tracked=true;for(auto& j:f.joints)j.confidence=1;
 f.joints[2]={0,.45f,0,1};f.joints[3]={0,.65f,0,1};
 f.joints[4]={-.2f,.4f,0,1};f.joints[8]={.2f,.4f,0,1};
 f.joints[9]={.35f,.6f,0,1};f.joints[10]={.4f,.85f,0,1};f.joints[11]={.4f,.9f,0,1};
 VirtualDepthCamera::Render(f,depth);
 size_t visible=0;for(auto d:depth){assert(d<=2047);if(d!=2047)++visible;}assert(visible>2000);
 const auto raised=depth;
 f.joints[9].y=.2f;f.joints[10].y=0;f.joints[11].y=-.03f;
 VirtualDepthCamera::Render(f,depth);assert(depth!=raised);
 VirtualDepthCamera::Pack(depth,packed);assert(packed.size()==VirtualDepthCamera::kDepthFrameBytes);
 uint32_t bits=0;int count=0;size_t pos=0;
 for(auto expected:depth){while(count<11){bits=(bits<<8)|packed.at(pos++);count+=8;}count-=11;assert(((bits>>count)&2047)==expected);}
 assert(!VirtualDepthCamera::Open(false,{}));SetEnabled(true);auto camera=VirtualDepthCamera::Open(false,{});assert(camera);
 assert(camera->CopyDepthFrame(0,packed));assert(packed.size()==422400);SetEnabled(false);
 puts("PASS: virtual depth size, blank tracking loss, moving-arm geometry, 11-bit roundtrip and disabled gating");
}
