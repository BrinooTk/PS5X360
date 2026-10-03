// SPDX-License-Identifier: MIT
#include "xbox360ps5/i18n.hpp"
#include "xbox360ps5/covers.hpp"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#if XE_PLATFORM_PS5
// The system resolver (libSceNet): the title has no resolv.conf for libc's.
extern "C" int sceNetResolverCreate(const char* name, int memory, int flags);
extern "C" int sceNetResolverStartNtoa(int resolver, const char* host, in_addr* address, int timeout,
                                       int retries, int flags);
extern "C" int sceNetResolverDestroy(int resolver);
#endif
namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
constexpr const char* kHost = "xboxunity.net";

uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t LE32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
uint16_t LE16(const uint8_t* p) { return uint16_t(p[1] << 8 | p[0]); }

// The execution info optional header (0x00040006) holds the title id at +12.
std::string TitleIdFromHeader(const std::vector<uint8_t>& header) {
  if (header.size() < 24 || std::memcmp(header.data(), "XEX2", 4)) return "";
  const uint32_t count = BE32(&header[0x14]);
  for (uint32_t n = 0; n < count && n < 64 && 0x18 + n * 8 + 8 <= header.size(); ++n) {
    if (BE32(&header[0x18 + n * 8]) != 0x00040006) continue;
    const uint32_t offset = BE32(&header[0x18 + n * 8 + 4]);
    if (offset + 16 > header.size()) return "";
    char id[12];
    std::snprintf(id, sizeof(id), "%08X", BE32(&header[offset + 12]));
    return BE32(&header[offset + 12]) ? id : "";
  }
  return "";
}

bool ReadAt(std::FILE* file, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
  out.resize(size);
  if (fseeko(file, off_t(offset), SEEK_SET)) return false;
  return std::fread(out.data(), 1, size, file) == size;
}

// One A record for host straight from a public DNS server, over UDP. The
// console's own DNS is often a jailbreak helper that blocks or redirects
// sites; asking a public server for this one name leaves that setting alone.
in_addr_t QueryDns(const char* server, const char* host) {
  const int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return 0;
  timeval timeout{3, 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint8_t query[512] = {0x58, 0x35, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};  // id, recursion, 1 question
  size_t length = 12;
  for (const char* label = host; *label;) {
    const char* dot = std::strchr(label, '.');
    const size_t size = dot ? size_t(dot - label) : std::strlen(label);
    query[length++] = uint8_t(size);
    std::memcpy(&query[length], label, size);
    length += size;
    label += size + (dot ? 1 : 0);
  }
  const uint8_t tail[] = {0, 0, 1, 0, 1};  // end of name, type A, class IN
  std::memcpy(&query[length], tail, sizeof(tail));
  length += sizeof(tail);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(53);
  to.sin_addr.s_addr = inet_addr(server);
  in_addr_t found = 0;
  uint8_t answer[512];
  if (sendto(sock, query, length, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == ssize_t(length)) {
    const ssize_t got = recv(sock, answer, sizeof(answer), 0);
    if (got > 12 && answer[0] == 0x58 && answer[1] == 0x35 && (answer[3] & 15) == 0) {
      const int answers = answer[6] << 8 | answer[7];
      size_t at = length;  // The answers follow the echoed question.
      for (int n = 0; n < answers && at < size_t(got); ++n) {
        // The record's name: a pointer to an earlier one, or labels to a zero.
        if ((answer[at] & 0xC0) == 0xC0) {
          at += 2;
        } else {
          while (at < size_t(got) && answer[at]) at += answer[at] + 1;
          ++at;
        }
        if (at + 10 > size_t(got)) break;
        const int type = answer[at] << 8 | answer[at + 1];
        const int data = answer[at + 8] << 8 | answer[at + 9];
        at += 10;
        if (type == 1 && data == 4 && at + 4 <= size_t(got)) {
          std::memcpy(&found, &answer[at], 4);
          break;
        }
        at += size_t(data);
      }
    }
  }
  close(sock);
  return found;
}

in_addr_t SystemResolve(const char* host) {
#if XE_PLATFORM_PS5
  {
    const int resolver = sceNetResolverCreate("xbox360ps5", 0, 0);
    if (resolver >= 0) {
      in_addr address{};
      const int status = sceNetResolverStartNtoa(resolver, host, &address, 5 * 1000 * 1000, 2, 0);
      sceNetResolverDestroy(resolver);
      if (status >= 0 && address.s_addr) return address.s_addr;
    }
  }
#endif
  addrinfo hints{}, *found = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, "80", &hints, &found) || !found) return 0;
  const in_addr_t address = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(found);
  return address;
}

// Where to reach the cover server, best first: public DNS servers, then the
// console's DNS, then the address it had when this was written.
std::vector<in_addr_t> Candidates(const char* host) {
  static std::vector<in_addr_t> cached;
  if (!cached.empty()) return cached;
  std::vector<in_addr_t> list;
  const auto add = [&](in_addr_t address) {
    if (address && address != INADDR_NONE && std::find(list.begin(), list.end(), address) == list.end())
      list.push_back(address);
  };
  for (const char* server : {"1.1.1.1", "8.8.8.8", "9.9.9.9"}) {
    add(QueryDns(server, host));
    if (!list.empty()) break;
  }
  add(SystemResolve(host));
  add(inet_addr("149.56.47.64"));  // xboxunity.net, October 2026.
  cached = list;
  return list;
}

// A GET over HTTP/1.1; the body, with chunked transfer decoded.
bool HttpGet(const std::string& path, std::vector<uint8_t>& body, std::string& error) {
  int stream = -1;
  for (const in_addr_t address : Candidates(kHost)) {
    stream = socket(AF_INET, SOCK_STREAM, 0);
    if (stream < 0) { error = Tr("sem socket"); return false; }
    timeval timeout{15, 0};
    setsockopt(stream, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(stream, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr.s_addr = address;
    if (!connect(stream, reinterpret_cast<sockaddr*>(&server), sizeof(server))) break;
    close(stream);
    stream = -1;
  }
  if (stream < 0) { error = Tr("sem conexão com ") + std::string(kHost) + Tr(" (o PS5 está na internet?)"); return false; }
  const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + kHost +
                              "\r\nUser-Agent: Xbox360PS5\r\nAccept: */*\r\nConnection: close\r\n\r\n";
  send(stream, request.data(), request.size(), 0);
  std::vector<uint8_t> raw;
  uint8_t block[16384];
  for (ssize_t got; (got = recv(stream, block, sizeof(block), 0)) > 0 && raw.size() < (8u << 20);)
    raw.insert(raw.end(), block, block + got);
  close(stream);
  const char* const separator = "\r\n\r\n";
  const auto end = std::search(raw.begin(), raw.end(), separator, separator + 4);
  if (end == raw.end()) { error = Tr("resposta incompleta"); return false; }
  std::string head(raw.begin(), end);
  std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (head.compare(0, 12, "http/1.1 200") && head.compare(0, 12, "http/1.0 200")) {
    error = Tr("o servidor respondeu ") + head.substr(9, 3);
    return false;
  }
  std::vector<uint8_t> payload(end + 4, raw.end());
  body.clear();
  if (head.find("transfer-encoding: chunked") == std::string::npos) {
    body.swap(payload);
    return true;
  }
  for (size_t at = 0; at < payload.size();) {
    size_t line_end = at;
    while (line_end + 1 < payload.size() && !(payload[line_end] == '\r' && payload[line_end + 1] == '\n')) ++line_end;
    const size_t size = std::strtoul(std::string(payload.begin() + at, payload.begin() + line_end).c_str(), nullptr, 16);
    at = line_end + 2;
    if (!size || at + size > payload.size()) break;
    body.insert(body.end(), payload.begin() + at, payload.begin() + at + size);
    at += size + 2;
  }
  return !body.empty();
}

// The covers XboxUnity has for a title: the official one first, then the best rated.
std::string ChooseCover(const std::string& json) {
  std::string best;
  int best_score = -1;
  for (size_t at = 0; (at = json.find("\"CoverID\":\"", at)) != std::string::npos;) {
    at += 11;
    const std::string id = json.substr(at, json.find('"', at) - at);
    const size_t next = json.find("\"CoverID\"", at);
    const std::string entry = json.substr(at, next == std::string::npos ? std::string::npos : next - at);
    int score = entry.find("\"Official\":\"1\"") != std::string::npos ? 1000 : 0;
    if (const size_t rating = entry.find("\"Rating\":\""); rating != std::string::npos)
      score += std::atoi(entry.c_str() + rating + 10);
    if (score > best_score && !id.empty()) { best = id; best_score = score; }
  }
  return best;
}
}

std::string ReadXexTitleId(const fs::path& xex) {
  std::FILE* file = std::fopen(xex.c_str(), "rb");
  if (!file) return "";
  std::vector<uint8_t> header;
  std::string id;
  if (ReadAt(file, 0, 24, header)) {
    const uint32_t size = std::min<uint32_t>(BE32(&header[8]), 1u << 20);
    if (ReadAt(file, 0, std::max<uint32_t>(size, 24), header)) id = TitleIdFromHeader(header);
  }
  std::fclose(file);
  return id;
}

std::string ReadIsoTitleId(const fs::path& iso) {
  std::FILE* file = std::fopen(iso.c_str(), "rb");
  if (!file) return "";
  std::string id;
  std::vector<uint8_t> sector;
  // The game partition starts at one of these offsets (plain XDVDFS, XGD1, XGD2, XGD3).
  for (const uint64_t base : {0ull, 0x18300000ull, 0xFD90000ull, 0x2080000ull}) {
    if (!ReadAt(file, base + 32 * 2048, 2048, sector) || std::memcmp(sector.data(), "MICROSOFT*XBOX*MEDIA", 20)) continue;
    const uint32_t root = LE32(&sector[20]), root_size = LE32(&sector[24]);
    std::vector<uint8_t> table;
    if (!root_size || root_size > (1u << 20) || !ReadAt(file, base + uint64_t(root) * 2048, root_size, table)) break;
    // Directory entries form a binary tree; walk all of it.
    std::vector<uint32_t> pending{0};
    for (int visited = 0; !pending.empty() && visited < 4096; ++visited) {
      const uint32_t at = pending.back();
      pending.pop_back();
      if (at + 14 > table.size()) continue;
      const uint16_t left = LE16(&table[at]), right = LE16(&table[at + 2]);
      if (left == 0xFFFF) continue;
      const uint8_t length = table[at + 13];
      if (at + 14 + length > table.size()) continue;
      std::string name(table.begin() + at + 14, table.begin() + at + 14 + length);
      std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (name == "default.xex") {
        std::vector<uint8_t> header;
        const uint64_t start = base + uint64_t(LE32(&table[at + 4])) * 2048;
        if (ReadAt(file, start, 24, header)) {
          const uint32_t size = std::min<uint32_t>(BE32(&header[8]), 1u << 20);
          if (ReadAt(file, start, std::max<uint32_t>(size, 24), header)) id = TitleIdFromHeader(header);
        }
        break;
      }
      if (left) pending.push_back(uint32_t(left) * 4);
      if (right) pending.push_back(uint32_t(right) * 4);
    }
    break;
  }
  std::fclose(file);
  return id;
}

fs::path CoverFile(const std::string& title_id) {
#if XE_PLATFORM_PS5
  const fs::path folder = "/download0/xbox360ps5/covers";
#else
  const fs::path folder = fs::temp_directory_path() / "xbox360ps5-covers";
#endif
  return folder / (title_id + ".jpg");
}

CoverDownloader::~CoverDownloader() {
  if (thread_.joinable()) thread_.join();
}

void CoverDownloader::Start(std::vector<std::string> title_ids) {
  if (busy_) return;
  if (thread_.joinable()) thread_.join();
  busy_ = true;
  thread_ = std::thread([this, ids = std::move(title_ids)]() mutable { Run(std::move(ids)); });
}

std::string CoverDownloader::Status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

void CoverDownloader::Run(std::vector<std::string> title_ids) {
  const auto set = [&](const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = text;
  };
  std::error_code error;
  if (!title_ids.empty()) fs::create_directories(CoverFile(title_ids[0]).parent_path(), error);
  int fetched = 0, missing = 0, failed = 0;
  std::string problem;
  for (size_t n = 0; n < title_ids.size(); ++n) {
    const std::string& id = title_ids[n];
    set(Tr("Baixando capas: ") + std::to_string(n + 1) + Tr(" de ") + std::to_string(title_ids.size()));
    std::vector<uint8_t> info, image;
    std::string why;
    if (!HttpGet("/Resources/Lib/CoverInfo.php?titleid=" + id, info, why)) {
      ++failed;
      problem = why;
      // Without a connection, the rest would fail the same way.
      if (why.rfind(Tr("sem conexão com "), 0) == 0 || why == Tr("sem socket")) break;
      continue;
    }
    const std::string cover = ChooseCover(std::string(info.begin(), info.end()));
    if (cover.empty()) { ++missing; continue; }
    if (!HttpGet("/Resources/Lib/Cover.php?size=large&cid=" + cover, image, why) || image.size() < 4 ||
        !((image[0] == 0xFF && image[1] == 0xD8) || (image[0] == 0x89 && image[1] == 'P'))) {
      ++failed;
      problem = why.empty() ? Tr("imagem inválida") : why;
      continue;
    }
    std::ofstream output(CoverFile(id), std::ios::trunc | std::ios::binary);
    output.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
    ++fetched;
    XELOGI("Cover {} downloaded ({} bytes, XboxUnity cover {})", id, image.size(), cover);
  }
  std::string summary = std::to_string(fetched) + (fetched == 1 ? Tr(" capa baixada") : Tr(" capas baixadas"));
  if (missing) summary += ", " + std::to_string(missing) + Tr(" sem capa no XboxUnity");
  if (failed) summary += ", " + std::to_string(failed) + Tr(" com erro (") + problem + ")";
  if (title_ids.empty()) summary = Tr("Todos os jogos identificados já têm capa");
  XELOGW("Covers: {}", summary);
  set(summary);
  busy_ = false;
  // Only a download that brought something needs the shelf reloaded.
  finished_ = fetched > 0;
}
}
