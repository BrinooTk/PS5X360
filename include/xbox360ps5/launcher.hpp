// SPDX-License-Identifier: MIT
// The title's launcher, drawn with the emulator's ImGui drawer: a shelf of
// game cases in a cover flow (after the Aurora dashboard of the Xbox 360), a
// sheet with a game's details and patches, and a settings sheet.
#pragma once
#include "third_party/imgui/imgui.h"
#include "xbox360ps5/covers.hpp"
#include "xbox360ps5/game_paths.hpp"
#include "xbox360ps5/game_patches.hpp"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/immediate_drawer.h"
#include <cstdint>
#include <filesystem>
#include <functional>
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
// Options kept in /download0/xbox360ps5/settings.txt.
struct Settings {
  std::vector<std::string> game_paths = DefaultGamePaths();
  int language = 0;     // Game language: 0 follows the console, 1 = English.
  int interface_language = 0; // 0 follows PS5; separate from legacy game preference.
  int console_language = 1;   // Resolved Xbox language ID, refreshed at startup.
  int GameLanguage() const { return language > 0 && language <= 17 ? language : console_language; }
  bool mute = false;
  bool detailed_logs = false;
  bool show_fps = false;  // A frame counter over the game.
  bool vsync = true;     // Applied before graphics setup; changes restart the title.
  int resolution_scale = 1;   // Games render at 1, 2 or 3 times their resolution. Read when the title starts.
  int image_filter = 0;       // How a game's picture is stretched to the screen: 0 plain, 1 CAS, 2 FSR.
  bool touchpad_menu = true;  // The touchpad click opens the emulator's guide (else it is the Back button).
  static const char* ScaleName(int scale);
  static const char* FilterName(int filter);
  void Load();
  void Save() const;
  void Apply() const;   // Sets the emulator's configuration variables.
};
// Where the front and the spine are in a cover texture. A full case insert
// (back, spine, front, as XboxUnity has them) gives a box with its real spine;
// any other picture is a front only. aspect: the front's width over height.
struct CoverArt { float front_u0 = 0, front_u1 = 1, spine_u0 = 0, spine_u1 = 0, aspect = 1; bool box = false; };
// The emulated console's player profiles (gamertags), as the launcher shows
// them. The emulator core owns them: the title's entry point gives the
// launcher these three operations.
struct ProfileEntry { std::string name; uint64_t xuid = 0; bool active = false; };
struct ProfileHooks {
  std::function<std::vector<ProfileEntry>()> list;
  std::function<bool(const std::string&)> create;  // Creates it and signs it in.
  std::function<void(uint64_t)> use;               // Signs it in on the first controller.
};
enum class Key { up, down, left, right, cross, circle, triangle, square, l1, r1 };
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
  void SetSaveRoot(std::filesystem::path root) { save_root_ = std::move(root); }
  // After a game started: remember it and keep its real name, icon and hash.
  void RecordLaunch(const GameEntry& game, uint32_t title_id, const std::string& title_name,
                    const std::vector<uint8_t>& icon, uint64_t hash);
 private:
  enum class Mode { shelf, game, settings, profiles, name, paths, folders, saves };
  struct PatchRow { size_t file, patch; };
  void SelectionChanged();
  void ApplyFilter();
  void StartCoverDownload();
  const GameEntry* Selected() const;
  void OpenGameSheet();
  void DrawShelf(Canvas& c);
  void DrawGameSheet(Canvas& c);
  void DrawSettingsSheet(Canvas& c);
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
  GameEntry launch_;
  std::string loading_, message_;
  CoverDownloader covers_;
  bool covers_requested_ = false;  // The automatic download, once per session.
  ProfileHooks profile_hooks_;
  std::vector<ProfileEntry> profiles_;
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
