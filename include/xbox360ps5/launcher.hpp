// SPDX-License-Identifier: MIT
// The title's launcher, drawn with the emulator's ImGui drawer: a shelf of
// game cases in a cover flow (after the Aurora dashboard of the Xbox 360), a
// sheet with a game's details and patches, and a settings sheet.
#pragma once
#include "third_party/imgui/imgui.h"
#include "xbox360ps5/covers.hpp"
#include "xbox360ps5/game_paths.hpp"
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/qr_code.hpp"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/immediate_drawer.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace xbox360ps5 {
struct GameEntry {
  std::string name;
  std::filesystem::path path;  // What Emulator::LaunchPath receives.
  std::string kind;            // "XEX", "ISO" or "GOD/STFS".
  std::string folder;
  std::string title_id;        // From the executable, the disc image or the package.
  std::string size;
  uint64_t hash = 0;           // Executable hash, known after the first launch.
  int cover = -1;              // Index into the launcher's textures.
};
// Options kept in /download0/xbox360ps5/settings.txt. Every choice is a small
// integer. Options() describes the ones the menus show: the settings sheet's
// sub-menus, a game's own sheet and the guide over a running game all draw
// from it, and so do the files.
struct Settings {
  std::vector<std::string> game_paths = DefaultGamePaths();
  int language = 0;     // Game language: 0 follows the console, 1 = English.
  int interface_language = 0; // 0 follows PS5; separate from legacy game preference.
  int console_language = 1;   // Resolved Xbox language ID, refreshed at startup.
  int GameLanguage() const { return language > 0 && language <= 17 ? language : console_language; }
  int mute = 0;
  int detailed_logs = 0;
  int show_fps = 0;       // A frame counter over the game.
  int vsync = 1;          // Applied before graphics setup; changes restart the title.
  int resolution_scale = 1;   // Games render at 1, 2 or 3 times their resolution. Read when the title starts.
  int image_filter = 0;       // How a game's picture is stretched to the screen: 0 plain, 1 CAS, 2 FSR.
  int touchpad_menu = 1;  // The touchpad click opens the emulator's guide (else it is the Back button).
  // Picture.
  int sharpness = 1;            // Of CAS and FSR: 0 soft, 1 as designed, 2 strong.
  int dither = 0;
  int anisotropic = 0;          // 0 the game's own, then 2x, 4x, 8x, 16x.
  int internal_resolution = 0;  // The emulated console's display mode: 0 720p, 1 1080p, 2 848x480, 3 960x540.
  int stretch = 0;              // The picture fills the screen instead of keeping its proportions.
  int video_clock = 0;          // The emulated console's refresh: 0 60 Hz, 1 120 Hz.
  int gamma = 0;                // What the console says its display is: 0 HDTV, 1 sRGB, 2 linear.
  int depth_precision = 0;      // 24-bit depth: 0 converted on copy, 1 in the pixel shader, 2 there and rounded.
  int decal_bias = 0;           // Polygon offset of decals through shader depth.
  int mulsc = 0;                // Shader multiply rounds toward zero (Volition's engine).
  // Performance.
  int fast_locks = 0;           // The core's mutexes spin before they block (new in v0.5.6).
  int memory_boost = 0;         // v0.5.6's shared-memory, protection and cache changes.
  int dynamic_buffers = 1;      // Rewritten vertex data compared by content instead of watched.
  int memory_window = 0;        // What a guest write invalidates: 0 16 KiB, 1 64 KiB, 2 256 KiB.
  int occlusion = 0;            // 0 fast, 1 fake, 2 fast-alt, 3 strict.
  int readback = 0;             // 0 none, 1 fast, 2 full.
  int memexport = 0;
  int async_shaders = 1;
  int clear_pages = 0;
  int released_memory = 0;      // Draws may read guest memory the game has released.
  int guest_yield = 0;          // The guest's pause instruction gives the CPU away.
  int threaded_driver = 0;      // The driver records commands on a worker thread (a test).
  int pipeline_threads = 0;     // Threads that compile pipelines: 0 automatic, then 2, 4, 6.
  int no_promotion = 0;         // The recompiler's context promotion is off.
  int fast_dot = 0;             // Less exact dot products.
  int zero_page = 0;            // Guest address zero can be read and written.
  // Sound.
  int volume = 3;               // 25%, 50%, 75%, 100%.
  int ui_sounds = 1;
  int motion_phone = 0;        // Research receiver only; no Kinect device advertised.
  int ui_sound_volume = 2;      // 0%, 10%, 20%, 35%, 50%, 100%.
  int xma_decoder = 0;          // 0 new, 1 old, 2 master.
  int xma_inline = 0;           // XMA is decoded without a thread of its own.
  // Controller.
  int vibration = 1;
  int deadzone_left = 0, deadzone_right = 0;  // In steps of 5%.
  // System.
  int achievement_toasts = 1;
  int web_page = 1;             // The settings page for a phone on the same network.
  int arcade_full = 0;          // Arcade packages run with their full-version licence.
  static const char* ScaleName(int scale);
  static const char* FilterName(int filter);
  void Load();
  void Save() const;
  void Apply() const;   // Sets the emulator's configuration variables.
};
// When a changed option takes effect.
enum class OptionWhen { live, launch, start };
struct Option {
  const char* key;          // In the settings files.
  int Settings::*field;
  int category;             // Index into kOptionCategories.
  OptionWhen when;
  int recommended;
  bool per_game;            // A game may have its own value.
  const char* label;        // Portuguese; Tr() gives the other languages.
  const char* about;
  std::vector<const char*> choices;  // The value is the index.
};
const std::vector<Option>& Options();
inline constexpr const char* kOptionCategories[] = {"Vídeo", "Desempenho", "Áudio", "Controles", "Sistema"};
// What one game changes: option key to value, in game-settings/<TITLE ID>.txt.
using GameOverrides = std::map<std::string, int>;
GameOverrides LoadGameOverrides(const std::string& title_id);
bool SaveGameOverrides(const std::string& title_id, const GameOverrides& overrides);
Settings WithOverrides(Settings settings, const GameOverrides& overrides);
// What is recommended for one game: values confirmed on a console, built in,
// and any the title folder adds (assets/presets.txt: a [TITLE ID] line, then
// key=value lines). They are in force for that game unless it has its own.
GameOverrides GamePreset(const std::string& title_id);
// The options in force for a game: the general ones, its preset, its own.
Settings ForGame(const Settings& general, const std::string& title_id, const GameOverrides& own);
// Whether an option that is only read as the emulator starts differs.
bool StartOptionsDiffer(const Settings& a, const Settings& b);
// Where the front and the spine are in a cover texture. A full case insert
// (back, spine, front, as XboxUnity has them) gives a box with its real spine;
// any other picture is a front only. aspect: the front's width over height.
struct CoverArt { float front_u0 = 0, front_u1 = 1, spine_u0 = 0, spine_u1 = 0, aspect = 1; bool box = false; };
// The emulated console's player profiles (gamertags), as the launcher shows
// them. The emulator core owns them: the title's entry point gives the
// launcher these three operations.
struct ProfileEntry { std::string name; uint64_t xuid = 0; bool active = false; int player = -1; };
struct ProfileHooks {
  std::function<std::vector<ProfileEntry>()> list;
  std::function<bool(const std::string&)> create;  // Creates it and signs it in.
  std::function<bool(uint64_t, uint32_t)> use_player;
  std::function<bool(const std::string&, uint32_t)> create_player;
  std::function<void(uint64_t)> use;               // Signs it in on the first controller.
};
struct AchievementEntry {
  std::string name, description;
  uint32_t id = 0, score = 0;
  bool unlocked = false;
};
enum class Key { up, down, left, right, cross, circle, triangle, square, l1, r1, options };
// What the game folders hold, as one number: the same folders, files and
// sizes the shelf is built from, without opening any of them.
uint64_t LibrarySignature(const std::vector<std::string>& roots);
// Looks at the game folders from a thread of its own while the shelf is on
// screen, so that a game copied to the console appears without being asked
// for. A change counts once two looks in a row find the same thing: a copy
// that is still running keeps changing the folders.
class LibraryWatch {
 public:
  ~LibraryWatch();
  // What the shelf was just built from.
  void Known(std::vector<std::string> roots, uint64_t signature);
  // Called for every frame the shelf is drawn; the thread rests otherwise,
  // so nothing reads the disk while a game runs.
  void Shown();
  // True once the folders have settled into something else than what is known.
  bool TakeChanged() { return changed_.exchange(false); }
 private:
  void Run();
  std::thread thread_;
  std::mutex mutex_;
  std::vector<std::string> roots_;
  uint64_t known_ = 0;
  std::atomic<bool> stop_{false}, changed_{false};
  std::atomic<long long> shown_at_{0};
};
class Launcher {
 public:
  struct Canvas;  // One frame's drawing surface.
  struct Fonts { ImFont* f20 = nullptr; ImFont* f24 = nullptr; ImFont* f28 = nullptr; ImFont* f36 = nullptr; ImFont* f48 = nullptr; };
  // Loads the launcher's fonts into the atlas before its texture is made.
  static Fonts LoadFonts(ImGuiIO& io);
  Launcher(Fonts fonts, Settings& settings) : fonts_(fonts), settings_(settings) {}
  // The drawer makes the cover textures; they must be released before it goes.
  void SetDrawer(xe::ui::ImmediateDrawer* drawer) { drawer_ = drawer; if (!drawer) textures_.clear(); }
  void Scan();
  // Every game found, ordered by path (a stable order for unattended runs).
  std::vector<std::filesystem::path> GamePaths() const;
  void Press(Key key);
  void SetPadConnected(bool connected) { pad_connected_ = connected; }
  void Draw(ImGuiIO& io);
  // The game the user chose, once.
  bool TakeLaunch(GameEntry& game);
  // True once when the user asked for the title to restart.
  bool TakeRestart() { const bool value = restart_; restart_ = false; return value; }
  void SetLoading(const std::string& name) { loading_ = name; }
  void SetMessage(const std::string& text) { message_ = text; }
  void SetProfiles(ProfileHooks hooks) { profile_hooks_ = std::move(hooks); RefreshProfiles(); }
  void SetAchievements(std::function<std::vector<AchievementEntry>(uint32_t, uint32_t)> list,
                       std::function<std::string(uint32_t)> player) { achievement_list_ = std::move(list); achievement_player_name_ = std::move(player); }
  void SetSaveRoot(std::filesystem::path root) { save_root_ = std::move(root); }
  // The options the emulator started with: a changed start option asks for a restart.
  void SetStarted(const Settings& started) { started_ = started; }
  // The settings page: its address with the key (for the QR code), without it, and the key.
  void SetWebPage(const std::string& url, const std::string& plain, const std::string& key);
  // The games the emulator has identified: title ID and name.
  std::vector<std::pair<std::string, std::string>> Games() const;
  // After a game started: remember it and keep its real name, icon and hash.
  void RecordLaunch(const GameEntry& game, uint32_t title_id, const std::string& title_name,
                    const std::vector<uint8_t>& icon, uint64_t hash);
 private:
  enum class Mode { shelf, game, settings, options, profiles, name, paths, folders, saves };
  struct PatchRow { size_t file, patch; };
  void SelectionChanged();
  void ApplyFilter();
  // Automatic: nothing is said when no game lacks a cover.
  void StartCoverDownload(bool automatic = false);
  // Builds the shelf again and says what changed: in the settings row when
  // the user asked, as a notice on the shelf when the folders changed.
  void RefreshLibrary(bool automatic);
  const GameEntry* Selected() const;
  void OpenGameSheet(int tab = 0);
  void FillGameRows();
  void RefreshPerGame();
  void CloseSettings();
  void DrawAchievements(Canvas& c, float x);
  void RefreshAchievements();
  std::function<std::vector<AchievementEntry>(uint32_t, uint32_t)> achievement_list_;
  std::function<std::string(uint32_t)> achievement_player_name_;
  std::vector<AchievementEntry> achievements_;
  int achievement_row_ = 0, achievement_player_ = 0;
  void DrawGamePlay(Canvas& c, float x);
  void DrawPerGame(Canvas& c, float x);
  void DrawShelf(Canvas& c);
  void DrawGameSheet(Canvas& c);
  void DrawSettingsSheet(Canvas& c);
  void DrawOptionsSheet(Canvas& c);
  void DrawGameOptions(Canvas& c, float x);
  void ChangeOption(const Option& option, int step);
  void ChangeGameOption(const Option& option, int step);
  void DrawSavesSheet(Canvas& c);
  void RefreshSaves();
  int save_row_ = 0;
  std::filesystem::path save_root_ = "/download0/xbox360ps5/content";
  std::vector<std::string> save_titles_;
  void DrawProfilesSheet(Canvas& c);
  void DrawNameSheet(Canvas& c);
  void RefreshProfiles();
  void DrawPaths(Canvas& c);
  void DrawFolders(Canvas& c);
  void RefreshFolders();
  int LoadCover(const std::vector<uint8_t>& bytes);
  Fonts fonts_;
  Settings& settings_;
  xe::ui::ImmediateDrawer* drawer_ = nullptr;
  std::vector<std::unique_ptr<xe::ui::ImmediateTexture>> textures_;
  std::vector<CoverArt> arts_;        // Front and spine of each texture.
  std::vector<int> view_;             // The games the current filter shows.
  int filter_ = 0;
  std::vector<GameEntry> games_;      // Recently played first, then by name.
  std::vector<std::string> recents_;  // Paths, most recent first.
  Mode mode_ = Mode::shelf;
  float time_ = 0.0f, scroll_ = 0.0f, sheet_ = 0.0f, intro_ = 0.0f;
  int selected_ = 0, patch_row_ = 0, settings_row_ = 0;
  std::vector<PatchFile> patch_files_;  // For the selected game.
  std::vector<PatchRow> patch_rows_;
  bool other_version_ = false;  // Patch files exist, but for another executable.
  std::string patch_summary_;
  bool pad_connected_ = false, restart_ = false, settings_changed_ = false, launch_pending_ = false;
  bool restart_needed_ = false;  // A changed setting is only read when the title starts.
  Settings started_;
  std::string web_url_, web_plain_, web_key_;
  QrCode web_qr_;
  bool restored_ = false;                   // "Restore the recommended" was just used.
  int category_ = 0, option_row_ = 0;      // The open sub-menu of the settings.
  std::vector<int> category_rows_;         // Its options, as indices into Options().
  // The game sheet: play, patches, or the game's own options (one subject at a time).
  int game_tab_ = 0, game_option_row_ = 0, game_category_ = 0;
  bool game_category_open_ = false;
  std::vector<int> game_rows_;             // The shown subject's options a game may own, as indices into Options().
  std::vector<int> game_categories_;       // The subjects that have any.
  Mode game_return_ = Mode::shelf;         // Where closing the game sheet goes back to.
  int settings_tab_ = 0, per_game_row_ = 0; // The settings sheet: general, or the list of games.
  std::vector<int> per_game_;              // One entry of games_ per identified title.
  GameOverrides overrides_;                // Of the selected game.
  GameEntry launch_;
  std::string loading_, message_;
  CoverDownloader covers_;
  bool covers_requested_ = false;  // The automatic download, once for each time the shelf is built from the folders.
  bool covers_busy_ = false;       // As of the last frame, to notice a download ending.
  LibraryWatch watch_;
  std::string scan_result_;        // Beside "Refresh game list" after it was used.
  std::string notice_;             // A line on the shelf for a few seconds.
  float notice_until_ = 0.0f;
  ProfileHooks profile_hooks_;
  std::vector<ProfileEntry> profiles_;
  int profile_player_ = 0;
  int profile_row_ = 0, key_row_ = 0, key_column_ = 0;
  std::string new_name_, name_error_;
  int path_row_ = 0, folder_row_ = 0;
  std::filesystem::path browser_path_ = "/mnt";
  std::vector<std::filesystem::path> browser_folders_;
  std::string path_error_;
};
class LauncherDialog final : public xe::ui::ImGuiDialog {
 public:
  LauncherDialog(xe::ui::ImGuiDrawer* drawer, Launcher& launcher)
      : ImGuiDialog(drawer), launcher_(launcher) {}
  // The dialog deletes itself on the next draw.
  void Dismiss() { Close(); }
 protected:
  void OnDraw(ImGuiIO& io) override { launcher_.Draw(io); }
 private:
  Launcher& launcher_;
};
}
