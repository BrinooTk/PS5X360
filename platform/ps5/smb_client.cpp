// SPDX-License-Identifier: MIT
#include "xbox360ps5/smb_client.hpp"
#include "xenia/base/logging.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <list>
#include <mutex>
#include <unordered_map>

extern "C" {
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

namespace xbox360ps5 {
namespace {

constexpr size_t kChunkSize = 256 * 1024;      // 256 KiB read-ahead block
constexpr size_t kMaxCachedChunks = 128;        // 32 MiB max RAM cache on PS5

std::string UrlDecode(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size() &&
        std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      char hex[3] = {in[i + 1], in[i + 2], 0};
      out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
      i += 2;
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

// Thread-safe SMB2 session pool keyed by "domain\user@host:port/share".
struct SmbSession {
  std::mutex mutex;
  smb2_context* ctx = nullptr;
  std::string key;
  std::string open_file_path;
  smb2fh* open_fh = nullptr;
  uint64_t open_file_size = 0;

  ~SmbSession() { Disconnect(); }

  void Disconnect() {
    if (ctx) {
      if (open_fh) {
        smb2_close(ctx, open_fh);
        open_fh = nullptr;
        open_file_path.clear();
      }
      smb2_disconnect_share(ctx);
      smb2_destroy_context(ctx);
      ctx = nullptr;
    }
  }

  bool EnsureConnected(const SmbUrl& url) {
    if (ctx) return true;
    ctx = smb2_init_context();
    if (!ctx) return false;
    smb2_set_security_mode(ctx, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (!url.domain.empty()) smb2_set_domain(ctx, url.domain.c_str());
    smb2_set_user(ctx, url.username.empty() ? "Guest" : url.username.c_str());
    if (!url.password.empty()) smb2_set_password(ctx, url.password.c_str());

    std::string server = url.host;
    if (url.port != 445) server += ":" + std::to_string(url.port);

    if (smb2_connect_share(ctx, server.c_str(), url.share.c_str(),
                           url.username.empty() ? "Guest" : url.username.c_str()) < 0) {
      XELOGE("SMB2: failed to connect to //{}/{}: {}", server, url.share, smb2_get_error(ctx));
      smb2_destroy_context(ctx);
      ctx = nullptr;
      return false;
    }
    return true;
  }

  bool EnsureFileOpen(const SmbUrl& url) {
    if (!EnsureConnected(url)) return false;
    if (open_fh && open_file_path == url.path) return true;
    if (open_fh) {
      smb2_close(ctx, open_fh);
      open_fh = nullptr;
      open_file_path.clear();
    }
    open_fh = smb2_open(ctx, url.path.c_str(), O_RDONLY);
    if (!open_fh) {
      // Retry once on stale connection
      Disconnect();
      if (!EnsureConnected(url)) return false;
      open_fh = smb2_open(ctx, url.path.c_str(), O_RDONLY);
      if (!open_fh) return false;
    }
    smb2_stat_64 st{};
    if (smb2_fstat(ctx, open_fh, &st) < 0) {
      smb2_close(ctx, open_fh);
      open_fh = nullptr;
      return false;
    }
    open_file_path = url.path;
    open_file_size = st.smb2_size;
    return true;
  }
};

std::mutex g_pool_mutex;
std::unordered_map<std::string, std::shared_ptr<SmbSession>> g_sessions;

std::shared_ptr<SmbSession> AcquireSession(const SmbUrl& url) {
  std::string key = url.domain + "\\" + url.username + "@" + url.host + ":" +
                    std::to_string(url.port) + "/" + url.share;
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  auto& session = g_sessions[key];
  if (!session) {
    session = std::make_shared<SmbSession>();
    session->key = key;
  }
  return session;
}

// LRU 256 KiB chunk cache to keep ISO/GOD streaming fast over Wi-Fi/LAN.
struct CacheKey {
  std::string url;
  uint64_t chunk_index;
  bool operator==(const CacheKey& o) const {
    return chunk_index == o.chunk_index && url == o.url;
  }
};
struct CacheKeyHash {
  size_t operator()(const CacheKey& k) const {
    return std::hash<std::string>{}(k.url) ^ (std::hash<uint64_t>{}(k.chunk_index) << 1);
  }
};

struct CacheEntry {
  CacheKey key;
  std::vector<uint8_t> data;
};

std::mutex g_cache_mutex;
std::list<CacheEntry> g_cache_lru;
std::unordered_map<CacheKey, std::list<CacheEntry>::iterator, CacheKeyHash> g_cache_map;

bool ReadChunkUncached(const SmbUrl& url, uint64_t chunk_offset, std::vector<uint8_t>& out) {
  auto session = AcquireSession(url);
  std::lock_guard<std::mutex> lock(session->mutex);
  if (!session->EnsureFileOpen(url)) return false;
  if (chunk_offset >= session->open_file_size) {
    out.clear();
    return true;
  }
  const size_t to_read = static_cast<size_t>(
      std::min<uint64_t>(kChunkSize, session->open_file_size - chunk_offset));
  out.resize(to_read);
  size_t done = 0;
  while (done < to_read) {
    const int n = smb2_pread(session->ctx, session->open_fh, out.data() + done,
                             static_cast<uint32_t>(to_read - done), chunk_offset + done);
    if (n <= 0) {
      session->Disconnect();
      return false;
    }
    done += static_cast<size_t>(n);
  }
  return true;
}

class SmbMappedMemory final : public xe::MappedMemory {
 public:
  SmbMappedMemory(std::string url, uint64_t total_size, uint64_t slice_offset = 0)
      : MappedMemory(nullptr, static_cast<size_t>(total_size)),
        url_(std::move(url)),
        slice_offset_(slice_offset) {}

  bool ReadAt(size_t offset, void* buffer, size_t length) const override {
    if (offset > size() || length > size() - offset) return false;
    if (!length) return true;
    size_t got = 0;
    return SmbReadAt(url_, slice_offset_ + offset, buffer, length, &got) && got == length;
  }

  std::unique_ptr<MappedMemory> Slice(size_t offset, size_t length) override {
    if (offset > size() || length > size() - offset) return nullptr;
    return std::make_unique<SmbMappedMemory>(url_, length, slice_offset_ + offset);
  }

 private:
  std::string url_;
  uint64_t slice_offset_ = 0;
};

}  // namespace

bool IsSmbPath(std::string_view path) {
  return path.size() >= 6 &&
         (path[0] == 's' || path[0] == 'S') &&
         (path[1] == 'm' || path[1] == 'M') &&
         (path[2] == 'b' || path[2] == 'B') &&
         path[3] == ':' && path[4] == '/' && path[5] == '/';
}

bool ParseSmbUrl(std::string_view url, SmbUrl& out) {
  out = SmbUrl{};
  if (!IsSmbPath(url)) return false;
  std::string_view rest = url.substr(6);
  const size_t slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0) return false;

  std::string_view authority = rest.substr(0, slash);
  std::string_view path_part = rest.substr(slash + 1);

  const size_t at = authority.rfind('@');
  std::string_view hostport = authority;
  if (at != std::string_view::npos) {
    std::string_view userinfo = authority.substr(0, at);
    hostport = authority.substr(at + 1);
    const size_t semi = userinfo.find(';');
    if (semi != std::string_view::npos) {
      out.domain = UrlDecode(userinfo.substr(0, semi));
      userinfo = userinfo.substr(semi + 1);
    }
    const size_t colon = userinfo.find(':');
    if (colon != std::string_view::npos) {
      out.username = UrlDecode(userinfo.substr(0, colon));
      out.password = UrlDecode(userinfo.substr(colon + 1));
    } else {
      out.username = UrlDecode(userinfo);
    }
  }

  const size_t port_colon = hostport.rfind(':');
  if (port_colon != std::string_view::npos) {
    out.host = std::string(hostport.substr(0, port_colon));
    const int p = std::atoi(std::string(hostport.substr(port_colon + 1)).c_str());
    if (p <= 0 || p > 65535) return false;
    out.port = static_cast<uint16_t>(p);
  } else {
    out.host = std::string(hostport);
  }
  if (out.host.empty()) return false;

  const size_t share_slash = path_part.find('/');
  if (share_slash == std::string_view::npos) {
    out.share = UrlDecode(path_part);
  } else {
    out.share = UrlDecode(path_part.substr(0, share_slash));
    out.path = UrlDecode(path_part.substr(share_slash + 1));
    while (!out.path.empty() && out.path.back() == '/') out.path.pop_back();
  }
  return !out.share.empty();
}

std::string JoinSmbUrl(std::string_view base_url, std::string_view child) {
  std::string result(base_url);
  while (!result.empty() && result.back() == '/') result.pop_back();
  while (!child.empty() && (child.front() == '/' || child.front() == '\\')) child.remove_prefix(1);
  if (child.empty()) return result;
  result.push_back('/');
  for (char c : child) result.push_back(c == '\\' ? '/' : c);
  return result;
}

bool SmbListDirectory(std::string_view url, std::vector<SmbDirEntry>& out_entries) {
  out_entries.clear();
  SmbUrl parsed;
  if (!ParseSmbUrl(url, parsed)) return false;

  auto session = AcquireSession(parsed);
  std::lock_guard<std::mutex> lock(session->mutex);
  if (!session->EnsureConnected(parsed)) return false;

  smb2dir* dir = smb2_opendir(session->ctx, parsed.path.c_str());
  if (!dir) {
    session->Disconnect();
    if (!session->EnsureConnected(parsed)) return false;
    dir = smb2_opendir(session->ctx, parsed.path.c_str());
    if (!dir) return false;
  }

  while (const smb2dirent* ent = smb2_readdir(session->ctx, dir)) {
    if (!ent->name || !std::strcmp(ent->name, ".") || !std::strcmp(ent->name, "..")) continue;
    SmbDirEntry item;
    item.name = ent->name;
    item.size = ent->st.smb2_size;
    item.is_directory = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;
    out_entries.push_back(std::move(item));
  }
  smb2_closedir(session->ctx, dir);
  return true;
}

bool SmbStat(std::string_view url, uint64_t& out_size, bool& out_is_directory) {
  SmbUrl parsed;
  if (!ParseSmbUrl(url, parsed)) return false;
  if (parsed.path.empty()) {
    out_size = 0;
    out_is_directory = true;
    return true;
  }

  auto session = AcquireSession(parsed);
  std::lock_guard<std::mutex> lock(session->mutex);
  if (!session->EnsureConnected(parsed)) return false;
  smb2_stat_64 st{};
  if (smb2_stat(session->ctx, parsed.path.c_str(), &st) < 0) {
    session->Disconnect();
    if (!session->EnsureConnected(parsed) ||
      smb2_stat(session->ctx, parsed.path.c_str(), &st) < 0) {
        return false;
      }
    }
  out_size = st.smb2_size;
  out_is_directory = st.smb2_type == SMB2_TYPE_DIRECTORY;
  return true;
}

bool SmbReadAt(std::string_view url, uint64_t offset, void* buffer, size_t length, size_t* out_read) {
 if (out_read) *out_read = 0;
 if (!length) return true;
 SmbUrl parsed;
 if (!ParseSmbUrl(url, parsed) || parsed.path.empty()) return false;
 const std::string key_url(url);
 auto* dst = static_cast<uint8_t*>(buffer);
 size_t total_copied = 0;
 while (total_copied < length) {
	const uint64_t cur_offset = offset + total_copied;
	const uint64_t chunk_index = cur_offset / kChunkSize;
	const size_t in_chunk = static_cast<size_t>(cur_offset % kChunkSize);
	CacheKey ckey{key_url, chunk_index};
	std::vector<uint8_t> chunk_copy;
	bool hit = false;
	{
	 std::lock_guard<std::mutex> lock(g_cache_mutex);
	 auto it = g_cache_map.find(ckey);
	 if (it != g_cache_map.end()) {
	  g_cache_lru.splice(g_cache_lru.begin(), g_cache_lru, it->second);
	  chunk_copy = it->second->data;
	  hit = true;
	 }
	}
	if (!hit) {
	if (!ReadChunkUncached(parsed, chunk_index * kChunkSize, chunk_copy)) {
	 return false;
	}
	std::lock_guard<std::mutex> lock(g_cache_mutex);
	auto it = g_cache_map.find(ckey);
	if (it == g_cache_map.end()) {
	 if (g_cache_lru.size() >= kMaxCachedChunks) {
	  g_cache_map.erase(g_cache_lru.back().key);
	  g_cache_lru.pop_back();
	 }
	 g_cache_lru.push_front(CacheEntry{ckey, chunk_copy});
	 g_cache_map[ckey] = g_cache_lru.begin();
	}
	}
	if (in_chunk >= chunk_copy.size()) break;  // EOF
	const size_t avail = std::min(length - total_copied, chunk_copy.size() - in_chunk);
	std::memcpy(dst + total_copied, chunk_copy.data() + in_chunk, avail);
	total_copied += avail;
	if (chunk_copy.size() < kChunkSize) break;  // Last block of file
 }
 if (out_read) *out_read = total_copied;
 return total_copied > 0 || length == 0;
}

std::unique_ptr<xe::MappedMemory> OpenSmbMappedMemory(std::string_view url) {
 uint64_t size = 0;
 bool is_dir = false;
 if (!SmbStat(url, size, is_dir) || is_dir || size == 0) {
  return nullptr;
 }
 return std::make_unique<SmbMappedMemory>(std::string(url), size, 0);
}

void SmbCloseIdleSessions() {
 std::lock_guard<std::mutex> lock(g_pool_mutex);
 g_sessions.clear();
 std::lock_guard<std::mutex> cache_lock(g_cache_mutex);
 g_cache_map.clear();
 g_cache_lru.clear();
}
}  // namespace xbox360ps5