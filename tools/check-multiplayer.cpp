#include "xbox360ps5/dualsense_input.hpp"
#include <array>
#include <cassert>
#include <memory>
#include <iostream>
using namespace xbox360ps5; using namespace xe; using namespace xe::hid;
int main() {
 std::array<std::unique_ptr<DualSenseInput>,4> pads;
 int rumble=-1;
 for(unsigned i=0;i<4;i++) pads[i]=std::make_unique<DualSenseInput>([&,i](uint16_t l,uint16_t r){rumble=i;return l==123&&r==456;},i);
 for(unsigned i=0;i<4;i++) {
  PadSample sample;sample.connected=true;sample.buttons=0x4000u>>i;
  pads[i]->Submit(sample);
  X_INPUT_STATE s{}; assert(pads[i]->GetState(i,&s)==X_ERROR_SUCCESS);
  const uint16_t expected[]={X_INPUT_GAMEPAD_A,X_INPUT_GAMEPAD_B,X_INPUT_GAMEPAD_Y,X_INPUT_GAMEPAD_RIGHT_SHOULDER};
  assert(s.gamepad.buttons==expected[i]);
  const auto packet=uint32_t(s.packet_number); pads[i]->Submit(sample);
  assert(pads[i]->GetState(i,&s)==X_ERROR_SUCCESS&&s.packet_number==packet);
  assert(pads[i]->GetState((i+1)%4,&s)==X_ERROR_DEVICE_NOT_CONNECTED);
  X_INPUT_KEYSTROKE k{};assert(pads[i]->GetKeystroke(255,0,&k)==X_ERROR_SUCCESS&&k.user_index==i);
  X_INPUT_VIBRATION v{};v.left_motor_speed=123;v.right_motor_speed=456;
  assert(pads[i]->SetState(i,&v)==X_ERROR_SUCCESS&&rumble==int(i));
  sample.connected=false;pads[i]->Submit(sample);assert(pads[i]->GetState(i,&s)==X_ERROR_DEVICE_NOT_CONNECTED);
  sample.connected=true;pads[i]->Submit(sample);assert(pads[i]->GetState(i,&s)==X_ERROR_SUCCESS);
 }
 std::cout<<"PASS: 4 independent slots, packets, any-user keystrokes, rumble, disconnect/reconnect\n";
}
