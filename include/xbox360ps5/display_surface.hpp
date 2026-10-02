// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/ui/surface.h"
namespace xbox360ps5 {
class DisplaySurface final : public xe::ui::Surface {
 public:
  DisplaySurface(uint32_t width, uint32_t height, uint32_t refresh = 60000)
      : width_(width), height_(height), refresh_(refresh) {}
  TypeIndex GetType() const override { return kTypeIndex_KhrDisplay; }
  uint32_t refresh() const { return refresh_; }
 protected:
  bool GetSizeImpl(uint32_t& width, uint32_t& height) const override {
    width = width_; height = height_; return width && height;
  }
 private:
  uint32_t width_, height_, refresh_;
};
}
