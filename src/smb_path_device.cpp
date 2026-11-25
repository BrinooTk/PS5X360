// SPDX-License-Identifier: MIT
#include "xbox360ps5/smb_path_device.hpp"
#include "xbox360ps5/smb_client.hpp"
#include "xenia/base/string_util.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"
#include <algorithm>

namespace xbox360ps5 {
using xe::X_STATUS;
namespace {

class SmbVfsFile final : public xe::vfs::File {
 public:
  SmbVfsFile(uint32_t file_access, xe::vfs::Entry* entry, std::string url)
      : File(file_access, entry), url_(std::move(url)) {}

  void Destroy() override { delete this; }

  xe::X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                        size_t* out_bytes_read) override {
    if (byte_offset >= entry_->size()) {
      if (out_bytes_read) *out_bytes_read = 0;
      return X_STATUS_END_OF_FILE;
    }
    const size_t to_read = std::min(buffer.size(), entry_->size() - byte_offset);
    size_t got = 0;
    if (!SmbReadAt(url_, byte_offset, buffer.data(), to_read, &got)) {
      return X_STATUS_UNSUCCESSFUL;
    }
    if (out_bytes_read) *out_bytes_read = got;
    return X_STATUS_SUCCESS;
  }

  xe::X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t* out_bytes_written) override {
    if (out_bytes_written) *out_bytes_written = 0;
    return X_STATUS_ACCESS_DENIED;
  }

  xe::X_STATUS SetLength(size_t) override { return X_STATUS_ACCESS_DENIED; }

 private:
  std::string url_;
};

class SmbVfsEntry final : public xe::vfs::Entry {
 public:
  SmbVfsEntry(xe::vfs::Device* device, xe::vfs::Entry* parent,
              const std::string_view path, std::string smb_url,
              uint64_t size, bool is_dir)
      : Entry(device, parent, path), smb_url_(std::move(smb_url)) {
    size_ = size;
    allocation_size_ = xe::round_up(size, 0x200);
    attributes_ = is_dir ? xe::vfs::kFileAttributeDirectory : xe::vfs::kFileAttributeReadOnly;
  }

  const std::string& smb_url() const { return smb_url_; }

  xe::X_STATUS Open(uint32_t desired_access, xe::vfs::File** out_file) override {
    if (desired_access & (xe::vfs::FileAccess::kGenericWrite | xe::vfs::FileAccess::kFileWriteData)) {
      return X_STATUS_ACCESS_DENIED;
    }
    *out_file = new SmbVfsFile(desired_access, this, smb_url_);
    return X_STATUS_SUCCESS;
  }

  bool can_map() const override { return false; }
  std::unique_ptr<xe::MappedMemory> OpenMapped(xe::MappedMemory::Mode, size_t, size_t) override {
    return nullptr;
  }

  std::unique_ptr<Entry> CreateEntryInternal(const std::string_view, uint32_t) override {
    return nullptr;
  }
  bool DeleteEntryInternal(Entry*) override { return false; }

  void AddChild(std::unique_ptr<Entry> child) {
    children_.push_back(std::move(child));
  }

 private:
  std::string smb_url_;
};

}  // namespace

SmbPathDevice::SmbPathDevice(const std::string_view mount_path, std::string smb_root_url)
    : Device(mount_path), smb_root_url_(std::move(smb_root_url)) {}

SmbPathDevice::~SmbPathDevice() = default;

bool SmbPathDevice::Initialize() {
  uint64_t size = 0;
  bool is_dir = false;
  if (!SmbStat(smb_root_url_, size, is_dir) || !is_dir) {
    return false;
  }
  auto root = std::make_unique<SmbVfsEntry>(this, nullptr, "", smb_root_url_, 0, true);
  PopulateChildren(root.get(), smb_root_url_);
  root_entry_ = std::move(root);
  return true;
}

void SmbPathDevice::PopulateChildren(xe::vfs::Entry* parent, const std::string& smb_url) {
  std::vector<SmbDirEntry> entries;
  if (!SmbListDirectory(smb_url, entries)) return;
  auto* smb_parent = static_cast<SmbVfsEntry*>(parent);
  for (const auto& item : entries) {
    std::string child_url = JoinSmbUrl(smb_url, item.name);
    std::string guest_path = parent->path().empty()
                                 ? item.name
                                 : xe::utf8::join_guest_paths(parent->path(), item.name);
    auto child = std::make_unique<SmbVfsEntry>(
        this, parent, guest_path, child_url, item.size, item.is_directory);
    if (item.is_directory) {
      PopulateChildren(child.get(), child_url);
    }
    smb_parent->AddChild(std::move(child));
  }
}

void SmbPathDevice::Dump(xe::StringBuffer* string_buffer) {
  if (root_entry_) root_entry_->Dump(string_buffer, 0);
}

xe::vfs::Entry* SmbPathDevice::ResolvePath(const std::string_view path) {
  if (!root_entry_) return nullptr;
  auto parts = xe::utf8::split_path(path);
  xe::vfs::Entry* current = root_entry_.get();
  for (const auto& part : parts) {
    current = current->GetChild(part);
    if (!current) return nullptr;
  }
  return current;
}

}  // namespace xbox360ps5
