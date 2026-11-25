// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include "xenia/vfs/device.h"

namespace xbox360ps5 {

class SmbPathDevice final : public xe::vfs::Device {
 public:
  SmbPathDevice(const std::string_view mount_path, std::string smb_root_url);
  ~SmbPathDevice() override;

  bool Initialize() override;
  void Dump(xe::StringBuffer* string_buffer) override;
  xe::vfs::Entry* ResolvePath(const std::string_view path) override;

  bool is_read_only() const override { return true; }
  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 255; }
  uint32_t total_allocation_units() const override { return 0; }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 0x200; }

 private:
  void PopulateChildren(xe::vfs::Entry* parent, const std::string& smb_url);

  std::string name_ = "SMB2";
  std::string smb_root_url_;
  std::unique_ptr<xe::vfs::Entry> root_entry_;
};

}  // namespace xbox360ps5