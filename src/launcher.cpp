// SPDX-License-Identifier: MIT
#include "xbox360ps5/i18n.hpp"
#include "xbox360ps5/build_version.hpp"
#include "xbox360ps5/cover_geometry.hpp"
#include "xbox360ps5/launcher.hpp"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "third_party/stb/stb_image.h"
DECLARE_int32(user_language);
DECLARE_int32(log_level);
DECLARE_bool(mute);
DECLARE_bool(vsync);
DECLARE_bool(log_to_stdout);
DECLARE_int32(draw_resolution_scale_x);
DECLARE_int32(draw_resolution_scale_y);
DECLARE_bool(gpu_allow_invalid_fetch_constants);
#if XE_PLATFORM_PS5
extern "C" int sceSystemServiceParamGetInt(int parameter_id, int* value);
#endif
namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
const fs::path kStorage = "/download0/xbox360ps5";
const char* const kFont = "/app0/assets/fonts/NotoSans-Regular.ttf";
const char* const kHeadingFont = "/app0/assets/fonts/NotoSans-SemiBold.ttf";

// Colours: graphite surfaces, near-white type and one emerald accent.
constexpr uint32_t kInk = 0x0a0c11, kSheet = 0x12161d, kSurface = 0x1a1f28, kSurfaceHigh = 0x252c38,
                   kAccent = 0x35d07f, kText = 0xf3f5f8, kBody = 0xd5dae2, kMuted = 0x9aa3b2,
                   kFaint = 0x6f7888, kWarning = 0xf0a67a, kWhite = 0xffffff, kCaseGreen = 0x8cc63f;
// The keys of the gamertag keyboard: four rows of nine.
const char kNameKeys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char* const kFilters[] = {"Todos", "Recentes", "Pastas", "Imagens ISO", "Arcade e GOD"};

struct Language { int id; const char* name; };
constexpr Language kLanguages[] = {{0, "Automático (console)"}, {1, "English"}, {9, "Português"}, {5, "Español"}, {4, "Français"},
                                   {3, "Deutsch"}, {6, "Italiano"}, {2, "Japonês"}};

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}
bool ReadFile(const fs::path& path, std::vector<uint8_t>& bytes, size_t most = 16u << 20) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return false;
  bytes.clear();
  uint8_t block[65536];
  for (size_t got; (got = std::fread(block, 1, sizeof(block), file)) > 0 && bytes.size() < most;)
    bytes.insert(bytes.end(), block, block + got);
  std::fclose(file);
  return !bytes.empty();
}
// A stable name for a game's cached title and icon.
std::string CacheKey(const std::string& path) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : path) { hash ^= c; hash *= 1099511628211ull; }
  char text[20];
  std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
  return text;
}
std::string SizeText(uint64_t bytes) {
  char text[32];
  if (bytes >= 1ull << 30) std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / double(1ull << 30));
  else std::snprintf(text, sizeof(text), "%.0f MB", double(bytes) / double(1 << 20));
  return text;
}
std::string Utf16BE(const uint8_t* data, size_t characters) {
  std::string name;
  for (size_t n = 0; n < characters; ++n) {
    const unsigned code = unsigned(data[n * 2]) << 8 | data[n * 2 + 1];
    if (!code) break;
    if (code < 0x80) name += char(code);
    else if (code < 0x800) { name += char(0xC0 | code >> 6); name += char(0x80 | (code & 0x3F)); }
    else { name += char(0xE0 | code >> 12); name += char(0x80 | (code >> 6 & 0x3F)); name += char(0x80 | (code & 0x3F)); }
  }
  return name;
}
uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
// An STFS/SVOD package (Games on Demand, XBLA, installed disc): its header
// carries the display name, the title id and a thumbnail.
struct Package { std::string name, title_id; std::vector<uint8_t> thumbnail; bool game = false; };
bool ReadPackage(const fs::path& path, Package& package) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return false;
  std::vector<uint8_t> header(0x971A);
  const size_t got = std::fread(header.data(), 1, header.size(), file);
  std::fclose(file);
  if (got < 0x1800) return false;
  if (std::memcmp(header.data(), "LIVE", 4) && std::memcmp(header.data(), "PIRS", 4) &&
      std::memcmp(header.data(), "CON ", 4)) return false;
  // Content types that are something to run: arcade, demos, Games on Demand,
  // installed discs, community games. Saves, DLC and updates are skipped.
  const uint32_t type = BE32(&header[0x344]);
  package.game = type == 0x000D0000 || type == 0x00080000 || type == 0x00007000 || type == 0x00004000 ||
                 type == 0x00005000 || type == 0x02000000 || type == 0x00060000 || type == 0x000C0000;
  package.name = Utf16BE(&header[0x411], 0x40);
  if (package.name.empty()) package.name = Utf16BE(&header[0x1691], 0x40);
  char id[12];
  std::snprintf(id, sizeof(id), "%08X", BE32(&header[0x360]));
  package.title_id = id;
  const uint32_t thumbnail = BE32(&header[0x1712]), title_thumbnail = BE32(&header[0x1716]);
  if (title_thumbnail && title_thumbnail <= 0x4000 && got >= 0x571A + title_thumbnail)
    package.thumbnail.assign(&header[0x571A], &header[0x571A] + title_thumbnail);
  else if (thumbnail && thumbnail <= 0x4000 && got >= 0x171A + thumbnail)
    package.thumbnail.assign(&header[0x171A], &header[0x171A] + thumbnail);
  return true;
}
float Smooth(float value, float target, float dt, float speed) {
  return value + (target - value) * std::min(1.0f, dt * speed);
}
}

void Settings::Load() {
  int ps5_language = -1;
#if XE_PLATFORM_PS5
  // Public system-service parameter 1 is the console language.
  const int result = sceSystemServiceParamGetInt(1, &ps5_language);
  if (result != 0) ps5_language = -1;
#endif
  console_language = ConsoleGameLanguage(ps5_language);
  if (!ReadGamePaths(kStorage / "game_paths.txt", game_paths)) {
    ReadGamePaths("/app0/assets/game_paths.txt", game_paths);
    WriteGamePaths(kStorage / "game_paths.txt", game_paths);
  }
  std::ifstream input(kStorage / "settings.txt");
  int logging_policy = 0;
  std::string line;
  while (std::getline(input, line)) {
    const size_t split = line.find('=');
    if (split == std::string::npos) continue;
    const std::string key = line.substr(0, split);
    const int value = std::atoi(line.c_str() + split + 1);
    if (key == "language") language = value;
    else if (key == "interface_language") interface_language = value;
    else if (key == "mute") mute = value != 0;
    else if (key == "detailed_logs") detailed_logs = value != 0;
    else if (key == "logging_policy") logging_policy = value;
    else if (key == "show_fps") show_fps = value != 0;
    else if (key == "vsync") vsync = value != 0;
    // resolution_scale: not read. Scaled rendering takes the PS5's memory away
    // from the game's threads (every game crashed at its first frame).
    else if (key == "image_filter") image_filter = std::clamp(value, 0, 2);
    else if (key == "touchpad_menu") touchpad_menu = value != 0;
  }
  if (language < 0 || language > 17) language = 0;
  if (interface_language < 0 || interface_language > 17) interface_language = 0;
  ui_language.store(SupportedUiLanguage(interface_language ? interface_language : console_language));
  // One-time migration: previous builds could enable continuous tracing via
  // a debug sentinel. Start with normal logging; explicit later choices persist.
  if (logging_policy < 1) { detailed_logs = false; Save(); }
}
void Settings::Save() const {
  std::ofstream output(kStorage / "settings.txt", std::ios::trunc);
  output << "language=" << language << "\ninterface_language=" << interface_language
         << "\nmute=" << int(mute) << "\nlogging_policy=1\ndetailed_logs=" << int(detailed_logs)
         << "\nshow_fps=" << int(show_fps) << "\nresolution_scale=" << resolution_scale << "\nimage_filter="
         << image_filter << "\ntouchpad_menu=" << int(touchpad_menu) << "\nvsync=" << int(vsync) << "\n";
}
void Settings::Apply() const {
  cvars::gpu_allow_invalid_fetch_constants = true;
  ui_language.store(SupportedUiLanguage(interface_language ? interface_language : console_language));
  cvars::user_language = GameLanguage();
  cvars::mute = mute;
  cvars::draw_resolution_scale_x = cvars::draw_resolution_scale_y = resolution_scale;
  // Normal gameplay records warnings/errors plus explicit session metadata.
  // Do not duplicate every line into the platform stdout transport, and do
  // not let stale debug files silently override the user's visible setting.
  cvars::log_to_stdout = false;
  cvars::log_level = detailed_logs ? 3 : 1;
}
const char* Settings::ScaleName(int scale) {
  return scale >= 3 ? Tr("3x (2160p, muito pesado)") : scale == 2 ? Tr("2x (1440p, pesado)") : Tr("1x (720p, original)");
}
const char* Settings::FilterName(int filter) {
  return filter == 2 ? "FSR (upscale)" : filter == 1 ? Tr("CAS (nitidez)") : Tr("Simples");
}

Launcher::Fonts Launcher::LoadFonts(ImGuiIO& io) {
  Fonts fonts;
  std::error_code error;
  const char* body = fs::is_regular_file(kFont, error) ? kFont : "/app0/assets/fonts/Roboto-Medium.ttf";
  if (!fs::is_regular_file(body, error)) return fonts;
  const char* heading = fs::is_regular_file(kHeadingFont, error) ? kHeadingFont : body;
  // Latin Extended includes accents in interface text and European game titles.
  static const ImWchar ranges[] = {0x0020, 0x024F, 0x2000, 0x206F, 0};
  ImFontConfig config;
  config.OversampleH = 2;
  config.OversampleV = 2;
  fonts.f20 = io.Fonts->AddFontFromFileTTF(body, 20.0f, &config, ranges);
  fonts.f24 = io.Fonts->AddFontFromFileTTF(body, 24.0f, &config, ranges);
  fonts.f28 = io.Fonts->AddFontFromFileTTF(body, 28.0f, &config, ranges);
  fonts.f36 = io.Fonts->AddFontFromFileTTF(heading, 36.0f, &config, ranges);
  fonts.f48 = io.Fonts->AddFontFromFileTTF(heading, 48.0f, &config, ranges);
  return fonts;
}

int Launcher::LoadCover(const std::vector<uint8_t>& bytes) {
  if (!drawer_ || bytes.empty()) return -1;
  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
  if (!pixels) return -1;
  std::vector<uint8_t> image(pixels, pixels + size_t(width) * height * 4);
  stbi_image_free(pixels);
  // A full case insert (back, spine and front, as XboxUnity has them): the front
  // is the right 47 percent, the spine the narrow strip in the middle.
  CoverArt art;
  if (width > height * 6 / 5) {
    art.box = true;
    art.front_u0 = 0.529f;
    art.spine_u0 = 0.473f;
    art.spine_u1 = 0.527f;
  }
  // Fronts are shown at 336 pixels at most: halve larger pictures until they fit
  // (case inserts hold three faces side by side, so they may be twice as wide).
  const int largest = art.box ? 1024 : 512;
  while (width > largest || height > largest) {
    const int w = width / 2, h = height / 2;
    std::vector<uint8_t> half(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 4; ++c) {
      const size_t a = (size_t(y) * 2 * width + size_t(x) * 2) * 4 + c;
      half[(size_t(y) * w + x) * 4 + c] =
          uint8_t((image[a] + image[a + 4] + image[a + size_t(width) * 4] + image[a + size_t(width) * 4 + 4]) / 4);
    }
    image.swap(half); width = w; height = h;
  }
  auto texture = drawer_->CreateTexture(uint32_t(width), uint32_t(height),
                                        xe::ui::ImmediateTextureFilter::kLinear, false, image.data());
  if (!texture) return -1;
  textures_.push_back(std::move(texture));
  art.aspect = float(width) * (art.front_u1 - art.front_u0) / float(height);
  arts_.push_back(art);
  return int(textures_.size()) - 1;
}

void Launcher::Scan() {
  games_.clear();
  textures_.clear();
  arts_.clear();
  std::set<std::string> seen_games, seen_roots;
  const auto add = [&](GameEntry game, const std::vector<uint8_t>& embedded_cover) {
    // All scan paths are absolute. Keep the complete lexical path as the
    // key without depending on the console's libc realpath implementation.
    const auto identity = NormalizeGamePath(game.path.generic_string());
    if (identity.empty() || !seen_games.insert(identity).second) {
      XELOGI("LAUNCHER duplicate game {}", game.path.string());
      return;
    }
    XELOGI("LAUNCHER game {}", identity);
    // What the first launch learned: the game's own name, id, hash and icon.
    const fs::path cache = kStorage / "library" / CacheKey(game.path.string());
    std::ifstream info(cache.string() + ".txt");
    std::string title, id, hash;
    if (std::getline(info, title) && !title.empty()) game.name = title;
    if (std::getline(info, id) && !id.empty()) game.title_id = id;
    if (std::getline(info, hash) && !hash.empty()) game.hash = std::strtoull(hash.c_str(), nullptr, 16);
    // Before the first launch the id comes from the game's own header.
    if (game.title_id.empty() || game.title_id == "00000000")
      game.title_id = game.kind == "XEX" ? ReadXexTitleId(game.path) : game.kind == "ISO" ? ReadIsoTitleId(game.path) : game.title_id;
    // Cover: a picture the user put beside the game, a downloaded one, the
    // game's cached icon, or the thumbnail inside a package.
    std::vector<uint8_t> bytes;
    const fs::path beside = game.kind == "XEX" ? game.path.parent_path() / "cover" : fs::path(game.path).replace_extension();
    for (const char* extension : {".png", ".jpg", ".jpeg"}) {
      if (game.cover >= 0) break;
      if (ReadFile(beside.string() + extension, bytes)) game.cover = LoadCover(bytes);
    }
    // Downloaded by the title, or sent from a computer (tools/console.py covers).
    if (game.cover < 0 && !game.title_id.empty() && ReadFile(CoverFile(game.title_id), bytes)) game.cover = LoadCover(bytes);
    if (game.cover < 0 && !game.title_id.empty() && ReadFile("/app0/assets/covers/" + game.title_id + ".jpg", bytes))
      game.cover = LoadCover(bytes);
    if (game.cover < 0 && ReadFile(cache.string() + ".png", bytes)) game.cover = LoadCover(bytes);
    if (game.cover < 0) game.cover = LoadCover(embedded_cover);
    games_.push_back(std::move(game));
  };
  const std::function<void(const fs::path&, int, const fs::path&)> scan =
      [&](const fs::path& directory, int depth, const fs::path& root) {
    std::error_code error;
    std::vector<fs::directory_entry> entries;
    for (fs::directory_iterator at(directory, error), end; !error && at != end; at.increment(error)) entries.push_back(*at);
    XELOGI("LAUNCHER directory {} entries {} error {}", directory.string(), entries.size(), error.value());
    // An extracted game is its folder: default.xex, or the only .xex in it.
    fs::path executable;
    int executables = 0;
    for (const auto& entry : entries) {
      if (Lower(entry.path().extension().string()) != ".xex" || entry.is_directory(error)) continue;
      ++executables;
      if (Lower(entry.path().filename().string()) == "default.xex") { executable = entry.path(); executables = 1; break; }
      executable = entry.path();
    }
    if (executables == 1) {
      const std::string name = directory == root ? Tr("Jogo em ") + root.filename().string() : directory.filename().string();
      add({name, executable, "XEX", directory.string()}, {});
      if (directory != root) return;  // Nothing below a game is another game.
    }
    for (const auto& entry : entries) {
      const fs::path& path = entry.path();
      const std::string extension = Lower(path.extension().string());
      if (entry.is_directory(error)) {
        // A package's data folder sits beside its header file.
        if (depth < 6 && extension != ".data") scan(path, depth + 1, root);
      } else if (extension == ".iso") {
        GameEntry game{path.stem().string(), path, "ISO", directory.string()};
        game.size = SizeText(entry.file_size(error));
        add(std::move(game), {});
      } else if (extension.empty() || extension == ".xbla" || extension == ".god") {
        Package package;
        if (!ReadPackage(path, package) || !package.game) continue;
        GameEntry game{package.name.empty() ? path.filename().string() : package.name, path, "GOD/STFS", directory.string()};
        game.title_id = package.title_id;
        add(std::move(game), package.thumbnail);
      }
    }
  };
  for (const auto& configured : settings_.game_paths) {
    const fs::path root(configured);
    std::error_code error;
    const auto identity = NormalizeGamePath(root.generic_string());
    if (identity.empty() || !seen_roots.insert(identity).second) continue;
    const bool visible = fs::is_directory(root, error);
    XELOGI("LAUNCHER root {} {}", root.string(), visible ? "visible" : "not visible");
    if (visible) scan(root, 0, root);
  }
  recents_.clear();
  std::ifstream input(kStorage / "recent.txt");
  for (std::string line; std::getline(input, line);) if (!line.empty()) recents_.push_back(line);
  // Recently played first, in that order; the rest by name.
  const auto recency = [&](const GameEntry& game) {
    const auto at = std::find(recents_.begin(), recents_.end(), game.path.string());
    return at == recents_.end() ? recents_.size() : size_t(at - recents_.begin());
  };
  std::stable_sort(games_.begin(), games_.end(), [&](const GameEntry& a, const GameEntry& b) {
    const size_t ra = recency(a), rb = recency(b);
    return ra != rb ? ra < rb : Lower(a.name) < Lower(b.name);
  });
  ApplyFilter();
  XELOGW("LAUNCHER found {} games", games_.size());
}

const GameEntry* Launcher::Selected() const {
  return view_.empty() ? nullptr : &games_[size_t(view_[size_t(std::clamp(selected_, 0, int(view_.size()) - 1))])];
}

void Launcher::ApplyFilter() {
  const GameEntry* before = Selected();
  const std::string keep = before ? before->path.string() : std::string();
  view_.clear();
  for (int n = 0; n < int(games_.size()); ++n) {
    const GameEntry& game = games_[size_t(n)];
    const bool recent = std::find(recents_.begin(), recents_.end(), game.path.string()) != recents_.end();
    const bool shown = filter_ == 0 || (filter_ == 1 && recent) || (filter_ == 2 && game.kind == "XEX") ||
                       (filter_ == 3 && game.kind == "ISO") || (filter_ == 4 && game.kind == "GOD/STFS");
    if (shown) view_.push_back(n);
  }
  selected_ = 0;
  for (int n = 0; n < int(view_.size()); ++n) if (games_[size_t(view_[size_t(n)])].path.string() == keep) selected_ = n;
  scroll_ = float(selected_);
  SelectionChanged();
}

// Covers for the identified games that have none of their own yet.
void Launcher::StartCoverDownload() {
  std::vector<std::string> ids;
  std::error_code error;
  for (const auto& game : games_) {
    if (game.title_id.empty() || game.title_id == "00000000" || fs::exists(CoverFile(game.title_id), error) ||
        fs::exists("/app0/assets/covers/" + game.title_id + ".jpg", error)) continue;
    const fs::path beside = game.kind == "XEX" ? game.path.parent_path() / "cover" : fs::path(game.path).replace_extension();
    if (fs::exists(beside.string() + ".jpg", error) || fs::exists(beside.string() + ".png", error)) continue;
    if (std::find(ids.begin(), ids.end(), game.title_id) == ids.end()) ids.push_back(game.title_id);
  }
  covers_.Start(std::move(ids));
}

std::vector<fs::path> Launcher::GamePaths() const {
  std::vector<fs::path> paths;
  for (const auto& game : games_) paths.push_back(game.path);
  std::sort(paths.begin(), paths.end());
  return paths;
}

// The patches of the selected game: the files for its title id whose hash
// list has this executable (or all of them while the hash is not known yet).
void Launcher::SelectionChanged() {
  patch_files_.clear();
  patch_rows_.clear();
  patch_summary_.clear();
  other_version_ = false;
  patch_row_ = 0;
  if (!Selected()) return;
  const GameEntry& game = *Selected();
  if (game.title_id.empty()) return;
  const uint32_t title_id = uint32_t(std::strtoul(game.title_id.c_str(), nullptr, 16));
  int enabled = 0;
  auto patch_candidates = LoadPatchFiles(title_id);
  for (const auto& file : patch_candidates)
    if (game.hash && std::find(file.hashes.begin(), file.hashes.end(), game.hash) == file.hashes.end()) other_version_ = true;
  for (auto& file : SelectPatchFiles(std::move(patch_candidates), game.hash)) {
    if (game.hash && std::find(file.hashes.begin(), file.hashes.end(), game.hash) == file.hashes.end()) {
      other_version_ = true;
      continue;
    }
    patch_files_.push_back(std::move(file));
  }
  for (size_t f = 0; f < patch_files_.size(); ++f)
    for (size_t p = 0; p < patch_files_[f].patches.size(); ++p) {
      patch_rows_.push_back({f, p});
      enabled += patch_files_[f].patches[p].enabled;
    }
  if (!patch_rows_.empty()) {
    char text[64];
    std::snprintf(text, sizeof(text), Tr("%d de %d patches ligados"), enabled, int(patch_rows_.size()));
    patch_summary_ = text;
  }
}

void Launcher::RecordLaunch(const GameEntry& game, uint32_t title_id, const std::string& title_name,
                            const std::vector<uint8_t>& icon, uint64_t hash) {
  std::error_code error;
  fs::create_directories(kStorage / "library", error);
  const std::string path = game.path.string();
  const fs::path cache = kStorage / "library" / CacheKey(path);
  if (!title_name.empty() || title_id) {
    char text[40];
    std::snprintf(text, sizeof(text), "%08X\n%016llX\n", title_id, static_cast<unsigned long long>(hash));
    std::ofstream(cache.string() + ".txt", std::ios::trunc) << title_name << "\n" << text;
  }
  if (!icon.empty()) {
    std::ofstream output(cache.string() + ".png", std::ios::trunc | std::ios::binary);
    output.write(reinterpret_cast<const char*>(icon.data()), std::streamsize(icon.size()));
  }
  std::vector<std::string> recents{path};
  for (const auto& other : recents_) if (other != path && recents.size() < 12) recents.push_back(other);
  recents_ = recents;
  std::ofstream output(kStorage / "recent.txt", std::ios::trunc);
  for (const auto& line : recents_) output << line << "\n";
}

bool Launcher::TakeLaunch(GameEntry& game) {
  if (!launch_pending_) return false;
  launch_pending_ = false;
  game = launch_;
  return true;
}

void Launcher::OpenGameSheet() {
  if (!Selected()) return;
  SelectionChanged();
  mode_ = Mode::game;
  sheet_ = 0.0f;
}

void Launcher::Press(Key key) {
  if (!loading_.empty()) return;
  message_.clear();
  const int count = int(view_.size());
  switch (mode_) {
    case Mode::shelf: {
      const int before = selected_;
      if (key == Key::left && count) selected_ = std::max(0, selected_ - 1);
      if (key == Key::right && count) selected_ = std::min(count - 1, selected_ + 1);
      if (key == Key::l1 || key == Key::r1) {
        const int filters = int(std::size(kFilters));
        filter_ = (filter_ + (key == Key::r1 ? 1 : filters - 1)) % filters;
        ApplyFilter();
        break;
      }
      if (selected_ != before) SelectionChanged();
      if (key == Key::cross && count) { launch_ = *Selected(); launch_pending_ = true; loading_ = launch_.name; }
      if (key == Key::triangle || key == Key::down) OpenGameSheet();
      if (key == Key::square) { mode_ = Mode::settings; sheet_ = 0.0f; settings_row_ = 0; }
      break;
    }
    case Mode::game: {
      const int rows = int(patch_rows_.size());
      if (key == Key::up && rows) patch_row_ = (patch_row_ + rows - 1) % rows;
      if (key == Key::down && rows) patch_row_ = (patch_row_ + 1) % rows;
      if (key == Key::cross && rows) {
        const PatchRow row = patch_rows_[size_t(patch_row_)];
        GamePatch& patch = patch_files_[row.file].patches[row.patch];
        patch.enabled = !patch.enabled;
        if (patch.enabled) {
          for (auto& file : patch_files_) for (auto& other : file.patches)
            if (&other != &patch && other.enabled && PatchWritesConflict(patch, other)) {
              other.enabled = false;
              SavePatchChoice(file.title_id, other.name, false);
            }
        }
        SavePatchChoice(patch_files_[row.file].title_id, patch.name, patch.enabled);
        const int keep = patch_row_;
        SelectionChanged();
        patch_row_ = keep;
      }
      if (key == Key::circle || key == Key::triangle) mode_ = Mode::shelf;
      break;
    }
    case Mode::settings: {
      const int rows = 13;
      if (key == Key::up) settings_row_ = (settings_row_ + rows - 1) % rows;
      if (key == Key::down) settings_row_ = (settings_row_ + 1) % rows;
      if (key == Key::left || key == Key::right || key == Key::cross) {
        const int step = key == Key::left ? -1 : 1;
        if (settings_row_ == 0) {
          if (key == Key::cross) { RefreshProfiles(); mode_ = Mode::profiles; profile_row_ = 0; }
        } else if (settings_row_ == 1) {
          const int total = int(std::size(kLanguages));
          int index = 0;
          for (int n = 0; n < total; ++n) if (kLanguages[n].id == settings_.interface_language) index = n;
          settings_.language = kLanguages[(index + step + total) % total].id;
          settings_.interface_language = settings_.language;
          settings_.Apply(); // Update interface text immediately while the sheet is open.
        } else if (settings_row_ == 2) settings_.mute = !settings_.mute;
        else if (settings_row_ == 3) settings_.image_filter = (settings_.image_filter + step + 3) % 3;
        else if (settings_row_ == 4) settings_.touchpad_menu = !settings_.touchpad_menu;
        else if (settings_row_ == 5) settings_.detailed_logs = !settings_.detailed_logs;
        else if (settings_row_ == 6) settings_.show_fps = !settings_.show_fps;
        else if (settings_row_ == 7) { settings_.vsync = !settings_.vsync; restart_needed_ = settings_.vsync != cvars::vsync; }
        else if (settings_row_ == 8 && key == Key::cross) StartCoverDownload();
        else if (settings_row_ == 9 && key == Key::cross) { mode_ = Mode::paths; path_row_ = 0; path_error_.clear(); }
        else if (settings_row_ == 10 && key == Key::cross) Scan();
        else if (settings_row_ == 11 && key == Key::cross) { RefreshSaves(); mode_ = Mode::saves; }
        else if (settings_row_ == 12 && key == Key::cross) { settings_.Save(); restart_ = true; }
        if (settings_row_ >= 1 && settings_row_ < 8) settings_changed_ = true;
      }
      if (key == Key::circle || key == Key::square) {
        if (settings_changed_) { settings_.Save(); settings_.Apply(); settings_changed_ = false; }
        // The rendering resolution is set up with the emulator: start again.
        if (restart_needed_) restart_ = true;
        mode_ = Mode::shelf;
      }
      break;
    }
    case Mode::saves: {
      if (key == Key::up) save_row_ = std::max(0, save_row_ - 1);
      if (key == Key::down) save_row_ = std::min(std::max(0, int(save_titles_.size()) - 1), save_row_ + 1);
      if (key == Key::square) RefreshSaves();
      if (key == Key::circle) mode_ = Mode::settings;
      break;
    }
    case Mode::paths: {
      const int rows = int(settings_.game_paths.size());
      if (key == Key::up && rows) path_row_ = (path_row_ + rows - 1) % rows;
      if (key == Key::down && rows) path_row_ = (path_row_ + 1) % rows;
      if (key == Key::square || key == Key::cross) {
        browser_path_ = key == Key::cross && rows ? fs::path(settings_.game_paths[size_t(path_row_)]) : fs::path("/mnt");
        std::error_code error;
        if (!fs::is_directory(browser_path_, error)) browser_path_ = "/";
        RefreshFolders(); mode_ = Mode::folders;
      }
      if (key == Key::triangle && rows) {
        auto changed = settings_.game_paths;
        changed.erase(changed.begin() + path_row_);
        if (WriteGamePaths(kStorage / "game_paths.txt", changed)) {
          settings_.game_paths = std::move(changed);
          path_row_ = std::max(0, std::min(path_row_, int(settings_.game_paths.size()) - 1));
          Scan();
        } else path_error_ = Tr("Não foi possível salvar as pastas.");
      }
      if (key == Key::circle) mode_ = Mode::settings;
      break;
    }
    case Mode::folders: {
      const int rows = int(browser_folders_.size());
      if (key == Key::up && rows) folder_row_ = (folder_row_ + rows - 1) % rows;
      if (key == Key::down && rows) folder_row_ = (folder_row_ + 1) % rows;
      if (key == Key::cross && rows) { browser_path_ = browser_folders_[size_t(folder_row_)]; RefreshFolders(); }
      if (key == Key::circle) {
        if (browser_path_ == "/") mode_ = Mode::paths;
        else { browser_path_ = browser_path_.parent_path(); RefreshFolders(); }
      }
      if (key == Key::square) mode_ = Mode::paths;
      if (key == Key::triangle) {
        const auto path = NormalizeGamePath(browser_path_.string());
        if (path.empty()) { path_error_ = Tr("Selecione uma pasta de jogos."); break; }
        auto changed = settings_.game_paths;
        if (std::find(changed.begin(), changed.end(), path) == changed.end()) changed.push_back(path);
        if (WriteGamePaths(kStorage / "game_paths.txt", changed)) {
          settings_.game_paths = std::move(changed); path_error_.clear(); mode_ = Mode::paths; Scan();
        } else path_error_ = Tr("Não foi possível salvar as pastas.");
      }
      break;
    }
    case Mode::profiles: {
      // The profiles, then "create a new one".
      const int rows = int(profiles_.size()) + 1;
      if (key == Key::up) profile_row_ = (profile_row_ + rows - 1) % rows;
      if (key == Key::down) profile_row_ = (profile_row_ + 1) % rows;
      if (key == Key::cross) {
        if (profile_row_ < int(profiles_.size())) {
          if (profile_hooks_.use) profile_hooks_.use(profiles_[size_t(profile_row_)].xuid);
          RefreshProfiles();
        } else {
          mode_ = Mode::name;
          new_name_.clear();
          name_error_.clear();
          key_row_ = key_column_ = 0;
        }
      }
      if (key == Key::circle) mode_ = Mode::settings;
      break;
    }
    case Mode::name: {
      // Four rows of nine keys: A-Z, then 0-9.
      const int columns = 9, key_rows = 4;
      if (key == Key::left) key_column_ = (key_column_ + columns - 1) % columns;
      if (key == Key::right) key_column_ = (key_column_ + 1) % columns;
      if (key == Key::up) key_row_ = (key_row_ + key_rows - 1) % key_rows;
      if (key == Key::down) key_row_ = (key_row_ + 1) % key_rows;
      if (key == Key::cross && new_name_.size() < 15) {
        const char symbol = kNameKeys[key_row_ * columns + key_column_];
        // A gamertag starts with a letter; the first is a capital, the rest small.
        if (std::isdigit(static_cast<unsigned char>(symbol))) {
          if (new_name_.empty()) name_error_ = Tr("O nome começa com uma letra.");
          else new_name_ += symbol;
        } else {
          new_name_ += new_name_.empty() ? symbol : char(std::tolower(static_cast<unsigned char>(symbol)));
          name_error_.clear();
        }
      }
      if (key == Key::square && !new_name_.empty()) new_name_.pop_back();
      if (key == Key::triangle) {
        if (new_name_.empty()) name_error_ = Tr("Digite um nome.");
        else if (profile_hooks_.create && profile_hooks_.create(new_name_)) {
          RefreshProfiles();
          mode_ = Mode::profiles;
          profile_row_ = 0;
          for (int n = 0; n < int(profiles_.size()); ++n) if (profiles_[size_t(n)].active) profile_row_ = n;
        } else name_error_ = Tr("Não foi possível criar este perfil.");
      }
      if (key == Key::circle) mode_ = Mode::profiles;
      break;
    }
  }
}

void Launcher::RefreshProfiles() {
  profiles_ = profile_hooks_.list ? profile_hooks_.list() : std::vector<ProfileEntry>();
}

// One frame's drawing surface in the 1920x1080 design space.
struct Launcher::Canvas {
  ImDrawList* list;
  float scale, alpha, dt;
  const Launcher::Fonts* fonts;
  ImU32 Color(uint32_t rgb, float a = 1.0f) const {
    return IM_COL32(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, int(255 * std::clamp(a * alpha, 0.0f, 1.0f)));
  }
  ImVec2 At(float x, float y) const { return ImVec2(x * scale, y * scale); }
  ImFont* Font(float size) const {
    ImFont* font = size <= 21 ? fonts->f20 : size <= 25 ? fonts->f24 : size <= 30 ? fonts->f28 : size <= 38 ? fonts->f36 : fonts->f48;
    return font ? font : ImGui::GetFont();
  }
  float Width(const std::string& text, float size) const {
    return Font(size)->CalcTextSizeA(size * scale, 1e9f, 0.0f, text.c_str()).x / scale;
  }
  void Fill(float x, float y, float w, float h, uint32_t rgb, float a = 1.0f, float round = 0.0f) const {
    list->AddRectFilled(At(x, y), At(x + w, y + h), Color(rgb, a), round * scale);
  }
  void Edge(float x, float y, float w, float h, uint32_t rgb, float a, float round, float thickness = 1.5f) const {
    list->AddRect(At(x, y), At(x + w, y + h), Color(rgb, a), round * scale, 0, thickness * scale);
  }
  // align: 0 left, 1 centre, 2 right. Text wider than `width` is cut with an ellipsis.
  void Text(const std::string& text, float x, float y, float size, uint32_t rgb, float a = 1.0f,
            int align = 0, float width = 0.0f) const {
    std::string shown = text;
    if (width > 0 && Width(shown, size) > width) {
      while (shown.size() > 1 && Width(shown + "...", size) > width) {
        size_t last = shown.size() - 1;
        while (last && (static_cast<unsigned char>(shown[last]) & 0xC0) == 0x80) --last;
        shown.resize(last); // Remove the whole UTF-8 character, including its lead byte.
      }
      shown += "...";
    }
    const float w = Width(shown, size);
    const float left = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
    list->AddText(Font(size), size * scale, At(left, y), Color(rgb, a), shown.c_str());
  }
  void Wrapped(const std::string& text, float x, float y, float size, uint32_t rgb, float width) const {
    list->AddText(Font(size), size * scale, At(x, y), Color(rgb), text.c_str(), nullptr, width * scale);
  }
  // A labelled pill, as wide as its text. Returns its width.
  float Chip(const std::string& text, float x, float y, uint32_t ink, bool right_aligned = false) const {
    const float width = Width(text, 20) + 36;
    const float left = right_aligned ? x - width : x;
    Fill(left, y, width, 40, kSurface, 0.85f, 20);
    Edge(left, y, width, 40, kWhite, 0.10f, 20, 1.0f);
    Text(text, left + 18, y + 9, 20, ink);
    return width;
  }
  // An on/off switch.
  void Switch(float x, float y, bool on) const {
    Fill(x, y, 64, 32, on ? kAccent : kSurfaceHigh, 1.0f, 16);
    list->AddCircleFilled(At(x + (on ? 48 : 16), y + 16), 11 * scale, Color(on ? kInk : kMuted), 24);
  }
  // A pad button drawn from lines, then its label. Returns where the next one goes.
  float Hint(char button, const std::string& label, float x, float y) const {
    const ImVec2 centre = At(x + 14, y + 14);
    const float r = 13 * scale, g = 6 * scale;
    list->AddCircle(centre, r, Color(kMuted, 0.9f), 24, 1.6f * scale);
    const ImU32 ink = Color(kText);
    if (button == 'x') {
      list->AddLine(ImVec2(centre.x - g, centre.y - g), ImVec2(centre.x + g, centre.y + g), ink, 2 * scale);
      list->AddLine(ImVec2(centre.x + g, centre.y - g), ImVec2(centre.x - g, centre.y + g), ink, 2 * scale);
    } else if (button == 'o') {
      list->AddCircle(centre, g, ink, 16, 2 * scale);
    } else if (button == 't') {
      list->AddTriangle(ImVec2(centre.x, centre.y - g - scale), ImVec2(centre.x + g, centre.y + g * 0.8f),
                        ImVec2(centre.x - g, centre.y + g * 0.8f), ink, 2 * scale);
    } else if (button == 's') {
      list->AddRect(ImVec2(centre.x - g, centre.y - g), ImVec2(centre.x + g, centre.y + g), ink, 0, 0, 2 * scale);
    } else {
      // The directional buttons: a pair of arrows, sideways ('h') or up and down.
      const float a = 3.5f * scale, b = 8 * scale, w = 4.5f * scale;
      for (float side : {-1.0f, 1.0f}) {
        if (button == 'h')
          list->AddTriangleFilled(ImVec2(centre.x + side * b, centre.y), ImVec2(centre.x + side * a, centre.y - w),
                                  ImVec2(centre.x + side * a, centre.y + w), ink);
        else
          list->AddTriangleFilled(ImVec2(centre.x, centre.y + side * b), ImVec2(centre.x - w, centre.y + side * a),
                                  ImVec2(centre.x + w, centre.y + side * a), ink);
      }
    }
    Text(label, x + 38, y + 2, 20, kMuted);
    return x + 38 + Width(label, 20) + 44;
  }
};

namespace {
// A game's picture, or a tile with its initial when it has none.
void DrawCover(const Launcher::Canvas& c, const std::vector<std::unique_ptr<xe::ui::ImmediateTexture>>& textures,
               const std::vector<CoverArt>& arts, const GameEntry& game, float x, float y, float size, float round,
               float brightness = 1.0f) {
  if (game.cover >= 0 && size_t(game.cover) < textures.size()) {
    const int level = int(255 * brightness);
    const CoverArt art = size_t(game.cover) < arts.size() ? arts[size_t(game.cover)] : CoverArt{};
    // Box art keeps its proportions; icons fill the square.
    float w = size, h = size;
    if (art.box) { h = size * 1.12f; w = h * art.aspect; }
    const float left = x + (size - w) / 2;
    c.list->AddImageRounded(reinterpret_cast<ImTextureID>(textures[size_t(game.cover)].get()), c.At(left, y),
                            c.At(left + w, y + h), ImVec2(art.front_u0, 0), ImVec2(art.front_u1, 1),
                            IM_COL32(level, level, level, int(255 * c.alpha)), round * c.scale);
    return;
  }
  // No picture: a tile in a colour of its own, with the game's initial.
  static constexpr uint32_t tints[] = {0x1f6f54, 0x275d8c, 0x7a4a21, 0x5b3a82, 0x8a2f43, 0x2f6f78};
  uint32_t pick = 0;
  for (unsigned char ch : game.name) pick = pick * 31 + ch;
  c.Fill(x, y, size, size, tints[pick % std::size(tints)], 0.9f * brightness, round);
  c.Fill(x, y + size * 0.62f, size, size * 0.38f, kInk, 0.35f, round);
  if (game.name.empty()) return;
  const std::string initial(1, char(std::toupper(static_cast<unsigned char>(game.name[0]))));
  const float text = size >= 200 ? 48.0f : 36.0f;
  c.Text(initial, x + size / 2, y + size * 0.36f - text / 2, text, kWhite, 0.9f * brightness, 1);
  if (size >= 200) c.Text(game.kind, x + size / 2, y + size * 0.74f, 20, kWhite, 0.75f * brightness, 1);
}
}

void Launcher::Draw(ImGuiIO& io) {
  const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
  time_ += dt;
  intro_ = std::min(1.0f, intro_ + dt / 0.45f);
  // Downloaded covers show up as soon as the download ends.
  if (covers_.TakeFinished() && loading_.empty()) Scan();
  // Missing covers are fetched as soon as the shelf shows.
  if (!covers_requested_ && !games_.empty() && loading_.empty()) {
    covers_requested_ = true;
    StartCoverDownload();
  }
  sheet_ = Smooth(sheet_, mode_ == Mode::shelf ? 0.0f : 1.0f, dt, 14);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
               ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus);
  Canvas c{ImGui::GetWindowDrawList(), io.DisplaySize.y / 1080.0f, 1.0f, dt, &fonts_};
  const auto draw_version = [&c] {
    c.alpha = 1.0f;
    c.Text(kBuildVersionLabel, 1824, 1040, 20, kMuted, 1.0f, 2);
  };
  c.list->AddRectFilledMultiColor(ImVec2(0, 0), io.DisplaySize, c.Color(0x0f1b2e), c.Color(0x0b1322),
                                  c.Color(kInk), c.Color(0x06070a));
  if (!loading_.empty()) {
    // While the game starts: its name over a line that fills and empties.
    const float phase = std::fmod(time_ * 0.9f, 1.0f);
    c.Text(loading_, 960, 470, 48, kText, 1.0f, 1, 1500);
    c.Text(Tr("Carregando o jogo"), 960, 548, 24, kMuted, 1.0f, 1);
    c.Fill(660, 620, 600, 3, kWhite, 0.10f, 2);
    c.Fill(660 + 600 * std::max(0.0f, phase - 0.35f) / 0.65f * (phase > 0.35f ? 1.0f : 0.0f), 620,
           600 * std::min(phase / 0.65f, 1.0f) - 600 * std::max(0.0f, phase - 0.35f) / 0.65f * (phase > 0.35f ? 1.0f : 0.0f),
           3, kAccent, 1.0f, 2);
    draw_version();
    ImGui::End();
    return;
  }
  c.alpha = intro_;
  DrawShelf(c);
  if (sheet_ > 0.01f) {
    // The shelf dims behind an open sheet.
    c.alpha = 1.0f;
    c.Fill(0, 0, 1920, 1080, 0x000000, 0.55f * sheet_);
    if (mode_ == Mode::settings) DrawSettingsSheet(c);
    else if (mode_ == Mode::saves) DrawSavesSheet(c);
    else if (mode_ == Mode::paths) DrawPaths(c);
    else if (mode_ == Mode::folders) DrawFolders(c);
    else if (mode_ == Mode::profiles) DrawProfilesSheet(c);
    else if (mode_ == Mode::name) DrawNameSheet(c);
    else DrawGameSheet(c);
  }
  draw_version();
  ImGui::End();
}

namespace {
struct Quad { ImVec2 tl,tr,br,bl; std::array<float,4> depth{1,1,1,1}; };
ImVec2 Mix(ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
// The part of a quad between two fractions of its width and of its height.
Quad Part(const Quad& q, float u0, float v0, float u1, float v1) {
  const auto depth=[&](float u,float v) {
    return (1-u)*(1-v)*q.depth[0]+u*(1-v)*q.depth[1]+u*v*q.depth[2]+(1-u)*v*q.depth[3];
  };
  const auto point=[&](float u,float v) {
    const float a=(1-u)*(1-v),b=u*(1-v),d=(1-u)*v,e=u*v,z=depth(u,v);
    return ImVec2((a*q.tl.x*q.depth[0]+b*q.tr.x*q.depth[1]+e*q.br.x*q.depth[2]+d*q.bl.x*q.depth[3])/z,
                  (a*q.tl.y*q.depth[0]+b*q.tr.y*q.depth[1]+e*q.br.y*q.depth[2]+d*q.bl.y*q.depth[3])/z);
  };
  return {point(u0,v0),point(u1,v0),point(u1,v1),point(u0,v1),
          {depth(u0,v0),depth(u1,v0),depth(u1,v1),depth(u0,v1)}};
}
Quad Projected(const Launcher::Canvas& c,const covers3d::Face& f) {
  return {c.At(f[0].x,f[0].y),c.At(f[1].x,f[1].y),c.At(f[2].x,f[2].y),c.At(f[3].x,f[3].y),
          {f[0].depth,f[1].depth,f[2].depth,f[3].depth}};
}
// Tessellation approximates perspective-correct UVs in ImGui's affine UI shader.
void CoverImage(ImDrawList* list,ImTextureID texture,const Quad& q,
                float u0,float v0,float u1,float v1,ImU32 color) {
  constexpr int columns=8,rows=12;
  for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
    const float a=float(x)/columns,b=float(x+1)/columns,d=float(y)/rows,e=float(y+1)/rows;
    const Quad tile=Part(q,a,d,b,e);
    list->AddImageQuad(texture,tile.tl,tile.tr,tile.br,tile.bl,
      ImVec2(u0+(u1-u0)*a,v0+(v1-v0)*d),ImVec2(u0+(u1-u0)*b,v0+(v1-v0)*d),
      ImVec2(u0+(u1-u0)*b,v0+(v1-v0)*e),ImVec2(u0+(u1-u0)*a,v0+(v1-v0)*e),color);
  }
}
// A game case in the shelf, drawn as a box: its front (the quad q, which may
// be slanted: the cases beside the selected one turn towards it) and the side
// that faces the middle of the shelf, `depth` design pixels deep. A case to
// the right of the middle (or the middle one) shows its spine on the left; one
// to the left shows the opening edge on the right. With a full case insert the
// front and the spine are the game's own; otherwise a dark retail case with a
// green band and the picture on its front.
// A quad with a colour of its own at each corner.
void Gradient(ImDrawList* list, ImVec2 tl, ImVec2 tr, ImVec2 br, ImVec2 bl, ImU32 ctl, ImU32 ctr, ImU32 cbr, ImU32 cbl) {
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  const unsigned int first = list->_VtxCurrentIdx;
  list->PrimReserve(6, 4);
  for (unsigned int index : {0u, 1u, 2u, 0u, 2u, 3u}) list->PrimWriteIdx(ImDrawIdx(first + index));
  list->PrimWriteVtx(tl, uv, ctl);
  list->PrimWriteVtx(tr, uv, ctr);
  list->PrimWriteVtx(br, uv, cbr);
  list->PrimWriteVtx(bl, uv, cbl);
}
void DrawCase(const Launcher::Canvas& c, const std::vector<std::unique_ptr<xe::ui::ImmediateTexture>>& textures,
              const std::vector<CoverArt>& arts, const GameEntry& game, const Quad& q, float light, bool label,
              bool spine_left, const Quad& side, const Quad& top) {
  const auto shade = [&](uint32_t rgb, float tone = 1.0f, float a = 1.0f) {
    const float k = light * tone;
    return IM_COL32(int((rgb >> 16 & 255) * k), int((rgb >> 8 & 255) * k), int((rgb & 255) * k),
                    int(255 * a * c.alpha));
  };
  const bool has_cover = game.cover >= 0 && size_t(game.cover) < textures.size();
  const CoverArt art = has_cover && size_t(game.cover) < arts.size() ? arts[size_t(game.cover)] : CoverArt{};
  const ImTextureID texture = has_cover ? reinterpret_cast<ImTextureID>(textures[size_t(game.cover)].get()) : ImTextureID();

  // The plastic shell shows as a green rim around the paper insert.
  const float rim = 4.0f * c.scale;
  c.list->AddQuadFilled(ImVec2(q.tl.x - rim, q.tl.y - rim), ImVec2(q.tr.x + rim, q.tr.y - rim),
                        ImVec2(q.br.x + rim, q.br.y + rim * 0.5f), ImVec2(q.bl.x - rim, q.bl.y + rim * 0.5f),
                        shade(kCaseGreen, 0.62f));
  c.list->AddLine(ImVec2(q.tl.x - rim, q.tl.y - rim), ImVec2(q.tr.x + rim, q.tr.y - rim), shade(0xd8ffc0, 1.0f, 0.55f),
                  1.5f * c.scale);

  // The side face: from the front's edge, back towards the vanishing point.
  {
    const ImVec2 outer_top=side.tl,inner_top=side.tr,inner_bottom=side.br,outer_bottom=side.bl;
    if (spine_left && art.box && texture) {
      // The game's own spine, its right edge against the front.
      CoverImage(c.list,texture,side,art.spine_u0,0,art.spine_u1,1,shade(0xffffff,0.78f));
    } else if (spine_left) {
      c.list->AddQuadFilled(outer_top, inner_top, inner_bottom, outer_bottom, shade(0x1b2027));
      const ImVec2 band_outer = Mix(outer_top, outer_bottom, 0.085f), band_inner = Mix(inner_top, inner_bottom, 0.085f);
      c.list->AddQuadFilled(outer_top, inner_top, band_inner, band_outer, shade(kCaseGreen, 0.75f));
    } else {
      // The opening edge: plastic, with the paper insert showing as a thin line.
      c.list->AddQuadFilled(outer_top, inner_top, inner_bottom, outer_bottom, shade(0x15191f));
      c.list->AddLine(Mix(outer_top, inner_top, 0.35f), Mix(outer_bottom, inner_bottom, 0.35f),
                      shade(0x9aa3b2, 0.6f, 0.6f), 1.2f * c.scale);
    }
    c.list->AddQuad(outer_top, inner_top, inner_bottom, outer_bottom, shade(0xffffff, 1.0f, 0.10f), 1.0f * c.scale);
  }

  // The front.
  Gradient(c.list,top.tl,top.tr,top.br,top.bl,shade(0x305622),shade(0x305622),
           shade(0x9ac96a),shade(0xc9e9aa));
  c.list->AddQuad(top.tl,top.tr,top.br,top.bl,shade(0xe1f5cf,1,0.4f),c.scale);
  if (art.box && texture) {
    CoverImage(c.list,texture,q,art.front_u0,0,art.front_u1,1,shade(0xffffff));
  } else {
    c.list->AddQuadFilled(q.tl, q.tr, q.br, q.bl, shade(0x20262f));
    const Quad band = Part(q, 0.0f, 0.0f, 1.0f, 0.085f);
    c.list->AddQuadFilled(band.tl, band.tr, band.br, band.bl, shade(kCaseGreen));
    Quad front = Part(q, 0.045f, 0.115f, 0.955f, 0.965f);
    if (texture) {
      // A square picture (a game's own icon) sits in the middle of the front; a front picture fills it.
      if (art.aspect > 0.85f) {
        c.list->AddQuadFilled(front.tl, front.tr, front.br, front.bl, shade(0x0f1319));
        front = Part(q, 0.045f, 0.24f, 0.955f, 0.24f + 0.91f * 0.72f * std::min(1.0f, 1.0f / art.aspect));
      }
      CoverImage(c.list,texture,front,art.front_u0,0,art.front_u1,1,shade(0xffffff));
    } else {
      static constexpr uint32_t tints[] = {0x1f6f54, 0x275d8c, 0x7a4a21, 0x5b3a82, 0x8a2f43, 0x2f6f78};
      uint32_t pick = 0;
      for (unsigned char ch : game.name) pick = pick * 31 + ch;
      c.list->AddQuadFilled(front.tl, front.tr, front.br, front.bl, shade(tints[pick % std::size(tints)]));
      if (label && !game.name.empty()) {
        // The front has no picture: the game's initial.
        const ImVec2 middle = Mix(Mix(front.tl, front.tr, 0.5f), Mix(front.bl, front.br, 0.5f), 0.42f);
        const std::string initial(1, char(std::toupper(static_cast<unsigned char>(game.name[0]))));
        const float size = 48 * c.scale * 1.6f;
        const ImVec2 extent = c.Font(48)->CalcTextSizeA(size, 1e9f, 0.0f, initial.c_str());
        c.list->AddText(c.Font(48), size, ImVec2(middle.x - extent.x / 2, middle.y - extent.y / 2),
                        shade(0xffffff, 1.0f, 0.92f), initial.c_str());
      }
    }
  }
  // Light on the plastic sleeve: bright at the top corner nearest the viewer,
  // gone by the bottom, and a darker foot where the case meets the floor.
  const int glow = int(46 * c.alpha * light);
  Gradient(c.list, q.tl, q.tr, q.br, q.bl, IM_COL32(255, 255, 255, spine_left ? glow : glow / 3),
           IM_COL32(255, 255, 255, spine_left ? glow / 3 : glow), IM_COL32(0, 0, 0, int(70 * c.alpha)),
           IM_COL32(0, 0, 0, int(70 * c.alpha)));
  c.list->AddQuad(q.tl, q.tr, q.br, q.bl, shade(0xffffff, 1.0f, 0.16f), 1.5f * c.scale);
}
}

void Launcher::DrawShelf(Canvas& c) {
  const int count = int(view_.size());
  // Top bar: the name, the filters and what is connected.
  c.Text("PS5X360", 96, 54, 28, kText);
  c.Fill(96, 96, 44, 4, kAccent, 1.0f, 2);
  float tab = 420;
  for (int n = 0; n < int(std::size(kFilters)); ++n) {
    const bool active = n == filter_;
    const float width = c.Width(Tr(kFilters[n]), 24);
    c.Text(Tr(kFilters[n]), tab, 58, 24, active ? kText : kFaint);
    if (active) c.Fill(tab, 96, width, 4, kAccent, 1.0f, 2);
    tab += width + 48;
  }
  char games[48];
  std::snprintf(games, sizeof(games), "%d %s", int(games_.size()), games_.size() == 1 ? Tr("jogo") : Tr("jogos"));
  float right = 1824;
  right -= c.Chip(games, right, 50, kText, true) + 12;
  right -= c.Chip(pad_connected_ ? Tr("Controle conectado") : Tr("Sem controle"), right, 50, pad_connected_ ? kAccent : kWarning, true) + 12;
  for (const auto& profile : profiles_) if (profile.active) c.Chip(profile.name, right, 50, kText, true);

  const float floor = 690;
  if (games_.empty()) {
    c.Text(Tr("Sua prateleira está vazia"), 960, 380, 48, kText, 1.0f, 1);
    c.Text(Tr("Copie cada jogo por FTP para uma pasta em"), 960, 470, 24, kMuted, 1.0f, 1);
    c.Text("/data/homebrew/PPSA50011/assets/roms/", 960, 510, 28, kAccent, 1.0f, 1);
    c.Text(Tr("Vale pasta extraída (com default.xex), imagem .iso ou pacote GOD/STFS."), 960, 570, 24, kMuted, 1.0f, 1);
    c.Text(Tr("Depois aperte Quadrado e escolha Atualizar lista de jogos."), 960, 610, 24, kMuted, 1.0f, 1);
  } else if (!count) {
    c.Text(Tr("Nenhum jogo neste filtro"), 960, 420, 36, kText, 1.0f, 1);
    c.Text(Tr("Use L1 e R1 para trocar de filtro."), 960, 480, 24, kMuted, 1.0f, 1);
  } else {
    scroll_ = Smooth(scroll_, float(selected_), c.dt, 11);
    const GameEntry& game = games_[size_t(view_[size_t(selected_)])];
    // The cases stand on a floor that mirrors them faintly. The selected one
    // faces the viewer; its neighbours turn towards it and step back.
    const auto place = [&](float distance) {
      return Projected(c,covers3d::Project(distance,floor).front);
    };
    std::vector<int> order;
    for (int n = 0; n < count; ++n) if (std::fabs(float(n) - scroll_) < 9.5f) order.push_back(n);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return std::fabs(a - scroll_) > std::fabs(b - scroll_); });
    // Reflections first, then the floor's fade over them, then the cases.
    for (int n : order) {
      const Quad q = place(float(n) - scroll_);
      const float left = (q.bl.y - q.tl.y) * 0.42f, right_side = (q.br.y - q.tr.y) * 0.42f;
      const Quad mirror{ImVec2(q.bl.x, q.bl.y + left), ImVec2(q.br.x, q.br.y + right_side),
                        ImVec2(q.br.x, q.br.y + 4 * c.scale), ImVec2(q.bl.x, q.bl.y + 4 * c.scale)};
      const float alpha = c.alpha;
      c.alpha = alpha * 0.22f;
      // Only the lower part of the case shows in the floor.
      const GameEntry& entry = games_[size_t(view_[size_t(n)])];
      const Quad lower{mirror.tl, mirror.tr, mirror.br, mirror.bl};
      c.list->AddQuadFilled(lower.tl, lower.tr, lower.br, lower.bl, c.Color(0x20262f));
      if (entry.cover >= 0 && size_t(entry.cover) < textures_.size()) {
        const CoverArt art = size_t(entry.cover) < arts_.size() ? arts_[size_t(entry.cover)] : CoverArt{};
        c.list->AddImageQuad(reinterpret_cast<ImTextureID>(textures_[size_t(entry.cover)].get()), lower.tl, lower.tr,
                             lower.br, lower.bl, ImVec2(art.front_u0, 0.55f), ImVec2(art.front_u1, 0.55f),
                             ImVec2(art.front_u1, 1), ImVec2(art.front_u0, 1), c.Color(kWhite));
      }
      c.alpha = alpha;
    }
    c.list->AddRectFilledMultiColor(c.At(0, floor), c.At(1920, floor + 260), c.Color(kInk, 0.25f), c.Color(kInk, 0.25f),
                                    c.Color(0x06080c, 1.0f), c.Color(0x06080c, 1.0f));
    c.Fill(0, floor, 1920, 2, kWhite, 0.06f);
    for (int n : order) {
      const float distance = float(n) - scroll_;
      const Quad q = place(distance);
      const bool chosen = n == selected_;
      // The case's shadow on the floor.
      const float reach = 34 * c.scale, spread = 26 * c.scale;
      const ImU32 dark = IM_COL32(0, 0, 0, int(150 * c.alpha)), clear = IM_COL32(0, 0, 0, 0);
      Gradient(c.list, ImVec2(q.bl.x - 6 * c.scale, q.bl.y), ImVec2(q.br.x + 6 * c.scale, q.br.y),
               ImVec2(q.br.x + spread, q.br.y + reach), ImVec2(q.bl.x - spread, q.bl.y + reach), dark, dark, clear, clear);
      if (chosen) {
        for (int glow = 5; glow >= 1; --glow) {
          const float g = glow * 4.0f * c.scale;
          c.list->AddQuad(ImVec2(q.tl.x - g, q.tl.y - g), ImVec2(q.tr.x + g, q.tr.y - g), ImVec2(q.br.x + g, q.br.y + g),
                          ImVec2(q.bl.x - g, q.bl.y + g), c.Color(kAccent, 0.07f), 4.0f * c.scale);
        }
      }
      // The spine shows on the middle case too, so the shelf reads as boxes.
      const auto box=covers3d::Project(distance,floor);
      DrawCase(c, textures_, arts_, games_[size_t(view_[size_t(n)])], q,
               1.0f - 0.5f * std::min(1.0f, std::fabs(distance)), true,box.spine,
               Projected(c,box.side),Projected(c,box.top));
      if (chosen) c.list->AddQuad(q.tl, q.tr, q.br, q.bl, c.Color(kWhite, 0.75f), 2.0f * c.scale);
    }
    // The selected game in words, at the lower left; its place in the list at the right.
    c.Text(game.name, 96, 790, 48, kText, 1.0f, 0, 1300);
    std::string line = game.kind;
    if (!game.size.empty()) line += "   ·   " + game.size;
    if (!game.title_id.empty()) line += "   ·   ID " + game.title_id;
    c.Text(line, 98, 856, 24, kMuted, 1.0f, 0, 900);
    if (!patch_summary_.empty()) c.Chip(patch_summary_, 98 + c.Width(line, 24) + 32, 850, kAccent);
    char position[32];
    std::snprintf(position, sizeof(position), "%d / %d", selected_ + 1, count);
    c.Text(position, 1824, 800, 36, kText, 1.0f, 2);
    c.Text(Tr(kFilters[filter_]), 1824, 850, 20, kFaint, 1.0f, 2);
  }
  if (!message_.empty()) c.Text(message_, 96, 916, 24, kWarning, 1.0f, 0, 1700);
  else if(Selected() && Lower(Selected()->name).find("garden warfare")!=std::string::npos)
    c.Text(Tr("Este jogo exige serviços online. Autenticação Xbox Live/EA não está disponível."),96,916,24,kWarning,1,0,1700);
  c.Fill(96, 964, 1728, 1, kWhite, 0.08f);
  float x = 96;
  if (count) {
    x = c.Hint('x', Tr("Jogar"), x, 990);
    x = c.Hint('t', Tr("Detalhes e patches"), x, 990);
  }
  x = c.Hint('s', Tr("Configurações"), x, 990);
  if (count > 1) x = c.Hint('h', Tr("Trocar de jogo"), x, 990);
  c.Text(Tr("L1 / R1   Filtro"), 1824, 992, 20, kMuted, 1.0f, 2);
}

void Launcher::DrawGameSheet(Canvas& c) {
  if (!Selected()) return;
  const GameEntry& game = *Selected();
  // The sheet slides in from the right.
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  DrawCover(c, textures_, arts_, game, x + 56, 64, 176, 8);
  c.Text(game.name, x + 260, 70, 36, kText, 1.0f, 0, 540);
  c.Text(Tr("Formato  ") + game.kind + (game.size.empty() ? "" : Tr("     Tamanho  ") + game.size), x + 260, 128, 20, kMuted, 1.0f, 0, 540);
  c.Text(Tr("ID do título  ") + (game.title_id.empty() ? std::string(Tr("ainda não identificado")) : game.title_id), x + 260, 160, 20, kMuted, 1.0f, 0, 540);
  c.Text(game.path.string(), x + 260, 192, 20, kFaint, 1.0f, 0, 540);

  c.Text(Tr("Patches"), x + 56, 286, 28, kText);
  const int rows = int(patch_rows_.size());
  if (!rows) {
    const char* reason = game.title_id.empty()
        ? Tr("Abra este jogo uma vez: o emulador identifica o título e os patches dele passam a aparecer aqui.")
        : other_version_ ? Tr("Há patches para este jogo, mas feitos para outra versão do executável. Não são aplicados.")
                         : Tr("Nenhum patch para este jogo na pasta de patches.");
    c.Wrapped(reason, x + 56, 340, 24, kMuted, 740);
    c.Wrapped(Tr("Arquivos .patch.toml (formato do Xenia Canary) ficam em /data/homebrew/PPSA50011/assets/patches/."),
              x + 56, 440, 20, kFaint, 740);
  } else {
    const int shown = 6;
    const int first = std::clamp(patch_row_ - shown / 2, 0, std::max(0, rows - shown));
    for (int n = first; n < rows && n < first + shown; ++n) {
      const GamePatch& patch = patch_files_[patch_rows_[size_t(n)].file].patches[patch_rows_[size_t(n)].patch];
      const float y = 336 + (n - first) * 78.0f;
      const bool focused = n == patch_row_;
      c.Fill(x + 40, y, 780, 68, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 12);
      if (focused) c.Edge(x + 40, y, 780, 68, kAccent, 0.9f, 12, 2.0f);
      c.Text(patch.name, x + 64, y + 20, 24, focused ? kText : kBody, 1.0f, 0, 620);
      c.Switch(x + 736, y + 18, patch.enabled);
    }
    if (rows > shown) {
      char position[32];
      std::snprintf(position, sizeof(position), Tr("%d de %d"), patch_row_ + 1, rows);
      c.Text(position, x + 820, 292, 20, kFaint, 1.0f, 2);
    }
    // What the focused patch does.
    const GamePatch& patch = patch_files_[patch_rows_[size_t(patch_row_)].file].patches[patch_rows_[size_t(patch_row_)].patch];
    c.Fill(x + 40, 820, 780, 1, kWhite, 0.10f);
    c.list->PushClipRect(c.At(x + 40, 830), c.At(x + 820, 980), true);
    c.Wrapped(patch.description.empty() ? Tr("Sem descrição.") : patch.description, x + 56, 836, 20, kBody, 750);
    c.list->PopClipRect();
    if (!patch.author.empty()) c.Text(Tr("Autor: ") + patch.author, x + 820, 286, 20, kFaint, 1.0f, 2, rows > shown ? 1.0f : 400.0f);
  }
  float hint = x + 56;
  if (rows) hint = c.Hint('x', Tr("Ligar ou desligar"), hint, 1004);
  hint = c.Hint('o', Tr("Fechar"), hint, 1004);
  if (rows > 1) c.Hint('v', Tr("Mover"), hint, 1004);
}

void Launcher::DrawSettingsSheet(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  c.Text(Tr("Configurações"), x + 56, 64, 36, kText);
  const char* language = "English";
  for (const auto& entry : kLanguages) if (entry.id == settings_.interface_language) language = Tr(entry.name);
  std::string profile = Tr("Nenhum");
  for (const auto& entry : profiles_) if (entry.active) profile = entry.name;
  struct Row { const char* label; std::string value; const char* about; };
  const Row rows[] = {
      {Tr("Perfil do jogador"), profile, Tr("O perfil (gamertag) conectado no console emulado. Os jogos gravam os saves e as conquistas no perfil; cada perfil tem os seus.")},
      {Tr("Idioma"), language, Tr("Automático usa o idioma do PS5. Sem tradução da interface, usa inglês. A seleção também vale para o próximo jogo; os idiomas disponíveis dependem de cada jogo.")},
      {Tr("Som"), settings_.mute ? Tr("Mudo") : Tr("Ligado"), Tr("Silencia a saída de áudio dos jogos.")},
      {Tr("Filtro de imagem"), Settings::FilterName(settings_.image_filter), Tr("Como a imagem do jogo é ampliada até a tela. Simples: mais leve. CAS: deixa a imagem mais nítida. FSR: upscale da AMD, bordas mais limpas. Quase não pesa.")},
      {Tr("Clique do touchpad"), settings_.touchpad_menu ? Tr("Abre o guia") : Tr("Botão Back"), Tr("Abre o guia: o touchpad chama o guia do emulador durante o jogo, e o botão Back do Xbox fica dentro do guia. Botão Back: o touchpad é o Back do Xbox, e o guia abre com OPTIONS + touchpad.")},
      {Tr("Registros detalhados"), settings_.detailed_logs ? Tr("Ligados") : Tr("Desligados"), Tr("Grava cada chamada do jogo ao sistema no log. Deixa os jogos mais lentos; use só para investigar um problema.")},
      {Tr("Mostrar FPS no jogo"), settings_.show_fps ? Tr("Ligado") : Tr("Desligado"), Tr("Mostra no canto da tela quantos quadros por segundo o jogo entrega.")},
      {Tr("VSync"), settings_.vsync ? Tr("Ligado") : Tr("Desligado"), Tr("Controla a sincronização vertical emulada. O ritmo automático permanece em 60 Hz mesmo quando desligado, para evitar acelerar o jogo. Ao fechar este painel, o emulador reinicia para aplicar a mudança.")},
      {Tr("Baixar capas"), covers_.Busy() ? Tr("Baixando...") : "", Tr("Baixa do XboxUnity (o mesmo serviço do Aurora) as capas dos jogos que ainda não têm uma. Precisa de internet no PS5. Uma imagem cover.jpg na pasta do jogo sempre tem prioridade.")},
      {Tr("Pastas de jogos"), std::to_string(settings_.game_paths.size()), Tr("Adicione várias pastas, inclusive em dispositivos externos. Pastas desconectadas continuam salvas. Apenas locais acessíveis ao aplicativo podem ser lidos.")},
      {Tr("Atualizar lista de jogos"), "", Tr("Procura de novo os jogos, as capas e os patches nas pastas.")},
      {Tr("Saves"), "", Tr("Veja os dados por jogo e o local de armazenamento. Perfis e conquistas são preservados junto com os saves.")},
      {Tr("Reiniciar o emulador"), "", Tr("Fecha e abre o emulador de novo.")}};
  for (int n = 0; n < 13; ++n) {
    const float y = 122 + n * 42.0f;
    const bool focused = n == settings_row_;
    c.Fill(x + 40, y, 780, 40, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 10);
    if (focused) c.Edge(x + 40, y, 780, 40, kAccent, 0.9f, 10, 2.0f);
    c.Text(rows[n].label, x + 64, y + 8, 24, focused ? kText : kBody);
    c.Text(rows[n].value, x + 796, y + 8, 24, focused ? kAccent : kMuted, 1.0f, 2, 420);
  }
  c.Wrapped(rows[settings_row_].about, x + 56, 684, 20, kBody, 750);
  const std::string covers = covers_.Status();
  if (settings_row_ == 8 && !covers.empty()) c.Text(covers, x + 56, 770, 20, kAccent, 1.0f, 0, 750);
  c.Fill(x + 40, 808, 780, 1, kWhite, 0.10f);
  c.Text(Tr("Sobre"), x + 56, 820, 28, kText);
  c.Wrapped(std::string(Tr("PS5X360: emulador experimental de Xbox 360 para PlayStation 5.")) +
            " Xenia. " + Tr("Vídeo RADV (PS5_Vulkan), áudio XMA (FFmpeg) e patches Xenia Canary. Nenhum jogo, BIOS ou chave acompanha o emulador."),
            x + 56, 860, 20, kMuted, 750);
  float hint = c.Hint('x', Tr("Alterar"), x + 56, 1004);
  hint = c.Hint('o', Tr("Fechar"), hint, 1004);
  c.Hint('v', Tr("Mover"), hint, 1004);
}

void Launcher::RefreshSaves() {
  save_titles_.clear(); save_row_ = 0;
  std::error_code error;
  for (fs::directory_iterator profiles(save_root_, error), end; !error && profiles != end; profiles.increment(error)) {
    std::error_code entry_error;
    if (!profiles->is_directory(entry_error) || profiles->is_symlink(entry_error)) continue;
    const auto profile = profiles->path().filename().string();
    if (profile.size() != 16) continue;
    for (fs::directory_iterator titles(profiles->path(), entry_error), last; !entry_error && titles != last; titles.increment(entry_error)) {
      std::error_code type_error;
      if (!titles->is_directory(type_error) || titles->is_symlink(type_error)) continue;
      auto id = titles->path().filename().string();
      if (id.size() != 8 || id == "FFFE07D1") continue; // Dashboard profile package.
      std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
      std::string name = id;
      for (const auto& game : games_) if (game.title_id == id) { name = game.name; break; }
      save_titles_.push_back(name + "  [" + id + "]  / " + profile);
    }
  }
  std::sort(save_titles_.begin(), save_titles_.end());
  if (error) save_titles_.push_back(std::string(Tr("Não foi possível ler os saves.")) + " " + error.message());
}
void Launcher::DrawSavesSheet(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  c.Text(Tr("Saves"), x + 56, 64, 36, kText);
  c.Wrapped(save_root_.string(), x + 56, 125, 22, kAccent, 750);
  c.Wrapped(Tr("Cada perfil tem seus próprios dados. Para fazer um backup, copie toda a pasta saves com o emulador fechado, incluindo os perfis e conquistas."), x + 56, 200, 22, kBody, 750);
  if (save_titles_.empty()) c.Text(Tr("Nenhum dado de jogo encontrado."), x + 56, 340, 24, kMuted);
  const int first = std::max(0, save_row_ - 8);
  for (int n = first; n < std::min(int(save_titles_.size()), first + 9); ++n) {
    const float y = 330 + (n - first) * 60;
    c.Fill(x + 40, y, 780, 54, n == save_row_ ? kSurfaceHigh : kSurface, 0.9f, 10);
    c.Text(save_titles_[n], x + 56, y + 14, 20, n == save_row_ ? kAccent : kBody, 1, 0, 744);
  }
  float hint = c.Hint('o', Tr("Voltar"), x + 56, 1004);
  c.Hint('s', Tr("Atualizar lista"), hint, 1004);
}
void Launcher::RefreshFolders() {
  browser_folders_.clear(); folder_row_ = 0; path_error_.clear();
  std::error_code error;
  for (fs::directory_iterator it(browser_path_, error), end; !error && it != end; it.increment(error)) {
    std::error_code entry_error;
    if (it->is_directory(entry_error)) browser_folders_.push_back(it->path());
  }
  std::sort(browser_folders_.begin(), browser_folders_.end());
  if (error) path_error_ = Tr("Esta pasta não está acessível ao aplicativo.");
}
void Launcher::DrawPaths(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Text(Tr("Pastas de jogos"), x + 56, 64, 36, kText);
  c.Wrapped(Tr("Adicione várias pastas, inclusive em dispositivos externos. Pastas desconectadas continuam salvas. Apenas locais acessíveis ao aplicativo podem ser lidos."), x + 56, 122, 20, kBody, 750);
  const int count = int(settings_.game_paths.size()), first = std::max(0, path_row_ - 8);
  for (int n = first; n < std::min(count, first + 9); ++n) {
    const float y = 230 + (n - first) * 68.f;
    const bool focused = n == path_row_;
    c.Fill(x + 40, y, 780, 58, focused ? kSurfaceHigh : kSurface, 1, 8);
    if (focused) c.Edge(x + 40, y, 780, 58, kAccent, 0.9f, 8);
    c.Text(settings_.game_paths[size_t(n)], x + 56, y + 5, 20, kText, 1, 0, 744);
    std::error_code error;
    const bool visible = fs::is_directory(settings_.game_paths[size_t(n)], error);
    c.Text(visible ? Tr("Disponível") : Tr("Desconectada ou indisponível"), x + 56, y + 31, 20, visible ? kAccent : kMuted);
  }
  if (!count) c.Text(Tr("Nenhuma pasta configurada."), x + 56, 240, 24, kMuted);
  if (!path_error_.empty()) c.Wrapped(path_error_, x + 56, 868, 20, kWarning, 750);
  c.Wrapped(Tr("Remover um local não apaga os jogos. Logs: /download0/xbox360ps5/LOGS"), x + 56, 920, 20, kMuted, 750);
  float hint = c.Hint('s', Tr("Adicionar"), x + 56, 1004);
  hint = c.Hint('t', Tr("Remover"), hint, 1004);
  c.Hint('o', Tr("Voltar"), hint, 1004);
}
void Launcher::DrawFolders(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Text(Tr("Escolher pasta"), x + 56, 64, 36, kText);
  c.Wrapped(browser_path_.string(), x + 56, 122, 24, kAccent, 750);
  const int count = int(browser_folders_.size()), first = std::max(0, folder_row_ - 8);
  for (int n = first; n < std::min(count, first + 9); ++n) {
    const float y = 230 + (n - first) * 62.f;
    const bool focused = n == folder_row_;
    c.Fill(x + 40, y, 780, 52, focused ? kSurfaceHigh : kSurface, 1, 8);
    if (focused) c.Edge(x + 40, y, 780, 52, kAccent, 0.9f, 8);
    c.Text(browser_folders_[size_t(n)].filename().string(), x + 56, y + 12, 24, kText, 1, 0, 744);
  }
  if (!count && path_error_.empty()) c.Text(Tr("Sem subpastas. Você pode adicionar a pasta atual."), x + 56, 240, 20, kMuted);
  if (!path_error_.empty()) c.Wrapped(path_error_, x + 56, 860, 20, kWarning, 750);
  c.Wrapped(Tr("Triângulo adiciona a pasta atual. Círculo sobe um nível."), x + 56, 920, 20, kBody, 750);
  float hint = c.Hint('x', Tr("Abrir"), x + 56, 1004);
  hint = c.Hint('t', Tr("Usar pasta"), hint, 1004);
  c.Hint('s', Tr("Cancelar"), hint, 1004);
}
void Launcher::DrawProfilesSheet(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  c.Text(Tr("Perfis"), x + 56, 64, 36, kText);
  c.Wrapped(Tr("Cada perfil guarda os seus próprios saves e conquistas. O perfil em uso vale para o próximo jogo iniciado."),
            x + 56, 118, 20, kMuted, 750);
  const int rows = int(profiles_.size()) + 1;
  const int shown = 9;
  const int first = std::clamp(profile_row_ - shown / 2, 0, std::max(0, rows - shown));
  for (int n = first; n < rows && n < first + shown; ++n) {
    const float y = 200 + (n - first) * 78.0f;
    const bool focused = n == profile_row_;
    c.Fill(x + 40, y, 780, 68, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 12);
    if (focused) c.Edge(x + 40, y, 780, 68, kAccent, 0.9f, 12, 2.0f);
    if (n < int(profiles_.size())) {
      const ProfileEntry& profile = profiles_[size_t(n)];
      c.Text(profile.name, x + 64, y + 20, 24, focused ? kText : kBody, 1.0f, 0, 520);
      if (profile.active) c.Text(Tr("Em uso"), x + 796, y + 20, 24, kAccent, 1.0f, 2);
    } else {
      c.Text(Tr("Criar novo perfil"), x + 64, y + 20, 24, focused ? kText : kBody);
      c.Text("+", x + 796, y + 16, 28, kAccent, 1.0f, 2);
    }
  }
  float hint = c.Hint('x', profile_row_ < int(profiles_.size()) ? Tr("Usar este perfil") : Tr("Criar"), x + 56, 1004);
  hint = c.Hint('o', Tr("Voltar"), hint, 1004);
  c.Hint('v', Tr("Mover"), hint, 1004);
}

void Launcher::DrawNameSheet(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  c.Text(Tr("Novo perfil"), x + 56, 64, 36, kText);
  c.Wrapped(Tr("Escolha o nome do perfil (gamertag): até 15 letras e números, começando por uma letra."),
            x + 56, 118, 20, kMuted, 750);
  // The name so far, with a caret.
  c.Fill(x + 40, 200, 780, 84, kSurface, 0.9f, 12);
  c.Edge(x + 40, 200, 780, 84, kAccent, 0.6f, 12, 2.0f);
  const bool caret = std::fmod(time_, 1.0f) < 0.5f;
  c.Text(new_name_ + (caret ? "_" : ""), x + 68, 222, 36, kText, 1.0f, 0, 720);
  char count[16];
  std::snprintf(count, sizeof(count), "%d / 15", int(new_name_.size()));
  c.Text(count, x + 820, 296, 20, kFaint, 1.0f, 2);
  if (!name_error_.empty()) c.Text(name_error_, x + 56, 296, 20, kWarning);
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 9; ++column) {
      const float kx = x + 40 + column * 87.0f, ky = 350 + row * 96.0f;
      const bool focused = row == key_row_ && column == key_column_;
      c.Fill(kx, ky, 78, 84, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 12);
      if (focused) c.Edge(kx, ky, 78, 84, kAccent, 0.9f, 12, 2.0f);
      c.Text(std::string(1, kNameKeys[row * 9 + column]), kx + 39, ky + 22, 36, focused ? kText : kBody, 1.0f, 1);
    }
  }
  float hint = c.Hint('x', Tr("Digitar"), x + 56, 1004);
  hint = c.Hint('s', Tr("Apagar"), hint, 1004);
  hint = c.Hint('t', Tr("Criar perfil"), hint, 1004);
  c.Hint('o', Tr("Cancelar"), hint, 1004);
}
}
