// SPDX-License-Identifier: MIT
// The title's entry point on the Xenia Canary core: Canary's emulator with the
// console's video output (KHR_display), pad, audio port and the launcher.
#include "xenia/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xbox360ps5/i18n.hpp"
#include "xbox360ps5/build_version.hpp"
#include "xbox360ps5/gpu_diagnostics.hpp"
#include "xenia/base/mutex.h"
#include "xbox360ps5/thread_place.hpp"
#include "xbox360ps5/frame_watch.hpp"
#include "xbox360ps5/session_log.hpp"
#include "xbox360ps5/crash_report.hpp"
#include "xbox360ps5/ui_sounds.hpp"
#include "xbox360ps5/motion_input.hpp"
#include "xbox360ps5/display_surface.hpp"
#include "xbox360ps5/dualsense_input.hpp"
#include "xbox360ps5/autotest.hpp"
#include "xbox360ps5/launcher.hpp"
#include "xbox360ps5/achievement_hook.hpp"
#include "xbox360ps5/notify.hpp"
#include "xbox360ps5/web_settings.hpp"
#include "xbox360ps5/save_storage.hpp"
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/patch_runtime.hpp"
#include "xenia/kernel/xam/xam_module.h"
#include "xbox360ps5/utility_cache.hpp"
#include "xbox360ps5/canary_audio.hpp"
#include "xenia/base/cvar.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xthread.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/util/object_table.h"
#include "xenia/kernel/xam/xam_state.h"
#include "third_party/imgui/imgui.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <csetjmp>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <unistd.h>
DECLARE_path(log_file);
DECLARE_bool(vsync);
DECLARE_int32(log_level);
DECLARE_bool(headless);
// The launcher's language setting. Canary declares this variable in its kernel
// without defining it (the language comes from the signed-in profile there).
DECLARE_bool(mute);
DEFINE_int32(user_language, 1, "Xbox 360 language id told to games.", "XConfig");
extern "C" {
int sceUserServiceInitialize(const void*);
int sceUserServiceGetInitialUser(int32_t*);
int sceUserServiceGetLoginUserIdList(int32_t*);
int scePadInit();
int scePadOpen(int32_t, int32_t, int32_t, const void*);
int scePadReadState(int32_t, void*);
int scePadClose(int32_t);
int scePadSetVibration(int32_t, const void*);
int sceKernelDebugOutText(int, const char*);
int sceSystemServiceLoadExec(const char*, const char**);
int sceSystemServiceHideSplashScreen();
}
namespace xbox360ps5 {
extern std::atomic<unsigned long long> protect_calls, protect_syscalls, protect_nanoseconds, fault_count;
extern std::atomic<unsigned long long> open_syscalls, open_nanoseconds, protect_pages;
}
namespace {
using xbox360ps5::Tr;
// The frame counter drawn over a game when the setting asks for it.
class FpsOverlay final : public xe::ui::ImGuiDialog {
 public:
  FpsOverlay(xe::ui::ImGuiDrawer* drawer,const float& fps,const int& shown,const bool& stalled,
             const int& touchpad_menu)
      : ImGuiDialog(drawer),fps_(fps),shown_(shown),stalled_(stalled),touchpad_menu_(touchpad_menu) {}
 protected:
  void OnDraw(ImGuiIO& io) override {
    if (xbox360ps5::motion::enabled.load()) {
      const auto pose = xbox360ps5::motion::Snapshot();
      auto* draw = ImGui::GetForegroundDrawList();
      const float scale = io.DisplaySize.y / 1080.0f;
      draw->AddRectFilled(ImVec2(1550*scale,100*scale),ImVec2(1900*scale,640*scale),IM_COL32(0,0,0,150),8*scale);
      draw->AddText(ImVec2(1570*scale,115*scale), IM_COL32(130,240,170,255), pose.tracked ? "Motion: tracking (research)" : "Motion: waiting / body lost");
      draw->AddText(ImVec2(1570*scale,140*scale), IM_COL32(220,220,220,255), xbox360ps5::motion::guest_skeleton_bound.load() ? "Game skeleton: from this pose" : xbox360ps5::motion::guest_camera_open.load() ? "Guest camera: virtual depth" : "Guest camera: not opened");
      if (pose.tracked) {
        const auto point = [&](int n) { return ImVec2((1725 + pose.joints[n].x*170)*scale,(405-pose.joints[n].y*170)*scale); };
        const int bones[][2]={{0,1},{1,2},{2,3},{2,4},{4,5},{5,6},{6,7},{2,8},{8,9},{9,10},{10,11},{0,12},{12,13},{13,14},{14,15},{0,16},{16,17},{17,18},{18,19}};
        draw->PushClipRect(ImVec2(1550*scale,170*scale),ImVec2(1900*scale,640*scale),true);
        for (const auto& bone : bones) if (pose.joints[bone[0]].confidence >= .5f && pose.joints[bone[1]].confidence >= .5f) draw->AddLine(point(bone[0]),point(bone[1]),IM_COL32(100,240,160,255),2*scale);
        for (int n=0;n<20;++n) if (pose.joints[n].confidence >= .5f) draw->AddCircleFilled(point(n),4*scale,IM_COL32(240,255,240,255));
        draw->PopClipRect();
      }
    }
    if (!shown_ && !stalled_) return;
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps_);
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const float scale = io.DisplaySize.y / 1080.0f;
    if(stalled_) {
      const char* notice=touchpad_menu_ ? Tr("Sem frames novos. Touchpad: guia") : Tr("Sem frames novos. OPTIONS + touchpad: guia");
      const ImVec2 size=ImGui::GetFont()->CalcTextSizeA(24*scale,4096,0,notice);
      list->AddRectFilled(ImVec2(24*scale,1000*scale),ImVec2((60+size.x/scale)*scale,1048*scale),IM_COL32(0,0,0,180),8*scale);
      list->AddText(ImGui::GetFont(),24*scale,ImVec2(42*scale,1010*scale),IM_COL32(245,204,137,255),notice);
    }
    if(!shown_) return;
    list->AddRectFilled(ImVec2(24 * scale, 24 * scale), ImVec2(150 * scale, 64 * scale), IM_COL32(0, 0, 0, 150), 8 * scale);
    list->AddText(ImGui::GetFont(), 28 * scale, ImVec2(36 * scale, 30 * scale), IM_COL32(120, 230, 140, 255), text);
  }
 private:
  const float& fps_;
  const int& shown_;
  const bool& stalled_;
  const int& touchpad_menu_;
};
// The emulator's guide over a running game, after the Xbox 360's: a light
// panel that unfolds from the middle of the screen, a page of actions and a page
// of settings. The loop that reads the pad drives it; the dialog only draws.
enum class GuideAction { resume, shelf, back_button, quit, option, measure };
struct GuideItem { GuideAction action; std::string label, value; const xbox360ps5::Option* option = nullptr; };
struct GuideState {
  bool open = false;
  int page = 0, row = 0;
  float openness = 0.0f;  // 0 closed, 1 fully revealed.
  float hint = 7.0f;  // Seconds the reminder of how to open it stays up.
};
constexpr const char* kGuidePages[] = {"Jogo", "Configurações"};
constexpr int kGuideRows = 6;  // Rows of the settings page on screen at once.
std::vector<GuideItem> GuideItems(int page, const xbox360ps5::Settings& settings) {
  if (page == 0) {
    std::vector<GuideItem> items = {{GuideAction::resume, Tr("Continuar jogo"), ""},
                                    {GuideAction::shelf, Tr("Voltar para o menu de jogos"), ""}};
    if (settings.touchpad_menu) items.push_back({GuideAction::back_button, Tr("Apertar BACK no jogo"), ""});
    items.push_back({GuideAction::quit, Tr("Fechar o emulador"), ""});
    return items;
  }
  // The options that take effect at once. (The level of logging is left to the
  // launcher: the watch for stalled games changes it for a while.)
  std::vector<GuideItem> items;
  for (const auto& option : xbox360ps5::Options()) {
    if (option.when != xbox360ps5::OptionWhen::live || !std::strcmp(option.key, "detailed_logs")) continue;
    items.push_back({GuideAction::option, Tr(option.label), Tr(option.choices[size_t(settings.*option.field)]), &option});
  }
  items.push_back({GuideAction::measure, Tr("Medir desempenho (5 s)"), ""});
  return items;
}
class Guide final : public xe::ui::ImGuiDialog {
 public:
  Guide(xe::ui::ImGuiDrawer* drawer, GuideState& state, const xbox360ps5::Settings& settings, const std::string& profile,
        const std::string& game, const float& fps, ImFont* small, ImFont* medium, ImFont* large, const std::string& web)
      : ImGuiDialog(drawer), state_(state), settings_(settings), profile_(profile), game_(game), fps_(fps),
        small_(small), medium_(medium), large_(large), web_(web) {}
 protected:
  // Text turned a quarter clockwise (reading downwards), as on the guide's blades.
  static void Sideways(ImDrawList* list, ImFont* font, float size, ImVec2 at, ImU32 colour, const char* text) {
    const int first = list->VtxBuffer.Size;
    list->AddText(font, size, at, colour, text);
    for (int n = first; n < list->VtxBuffer.Size; ++n) {
      ImVec2& v = list->VtxBuffer[n].pos;
      v = ImVec2(at.x - (v.y - at.y), at.y + (v.x - at.x));
    }
  }
  static void Button(ImDrawList* list, ImFont* font, float scale, float& x, float y, ImU32 colour, const char* glyph,
                     const char* label) {
    list->AddCircleFilled(ImVec2(x + 15 * scale, y + 15 * scale), 15 * scale, colour, 24);
    const ImVec2 size = font->CalcTextSizeA(20 * scale, 4096.0f, 0.0f, glyph);
    list->AddText(font, 20 * scale, ImVec2(x + 15 * scale - size.x / 2, y + 15 * scale - size.y / 2),
                  IM_COL32(255, 255, 255, 255), glyph);
    list->AddText(font, 24 * scale, ImVec2(x + 42 * scale, y + 1 * scale), IM_COL32(235, 238, 242, 255), label);
    x += 42 * scale + font->CalcTextSizeA(24 * scale, 4096.0f, 0.0f, label).x + 40 * scale;
  }
  void OnDraw(ImGuiIO& io) override {
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const float scale = io.DisplaySize.y / 1080.0f;
    ImFont* small = small_ ? small_ : ImGui::GetFont();
    ImFont* medium = medium_ ? medium_ : ImGui::GetFont();
    ImFont* large = large_ ? large_ : ImGui::GetFont();
    // Finite, reversible motion: quick opening with a soft finish, faster
    // closing. Input can reverse the animation at any point.
    const float step = std::max(0.0f, io.DeltaTime) / (state_.open ? 0.28f : 0.20f);
    state_.openness = state_.open ? std::min(1.0f, state_.openness + step)
                                : std::max(0.0f, state_.openness - step);
    if (!state_.open && state_.openness == 0.0f) {
      if (state_.hint <= 0.0f) return;
      state_.hint -= io.DeltaTime;
      const char* text = settings_.touchpad_menu ? Tr("Touchpad: guia do emulador") : Tr("OPTIONS + touchpad: guia do emulador");
      const float alpha = std::min(1.0f, state_.hint);
      const ImVec2 size = small->CalcTextSizeA(22 * scale, 4096.0f, 0.0f, text);
      const float x = io.DisplaySize.x - size.x - 60 * scale, y = 36 * scale;
      list->AddRectFilled(ImVec2(x - 18 * scale, y - 10 * scale), ImVec2(x + size.x + 18 * scale, y + size.y + 10 * scale),
                          IM_COL32(0, 0, 0, int(170 * alpha)), 20 * scale);
      list->AddText(small, 22 * scale, ImVec2(x, y), IM_COL32(235, 238, 245, int(255 * alpha)), text);
      return;
    }
    const float remaining = 1.0f - state_.openness;
    const float reveal = 1.0f - remaining * remaining * remaining;
    const auto fade = [reveal](int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, int(a * reveal)); };
    list->AddRectFilled(ImVec2(0, 0), io.DisplaySize, fade(0, 0, 0, 190));
    // Reveal the fixed-size guide outwards from its centre. Clipping keeps
    // text and icons crisp, rather than squeezing or stretching them.
    const ImVec2 centre(960 * scale, 540 * scale);
    const float half_width = 564 * scale * reveal;
    const float half_height = 316 * scale * reveal;
    list->PushClipRect(ImVec2(centre.x - half_width, centre.y - half_height),
                       ImVec2(centre.x + half_width, centre.y + half_height), true);
    const auto at = [&](float x, float y) { return ImVec2(x * scale, y * scale); };
    const float left = 412, right = 1508, top = 304, bottom = 778, blade = 66;
    // Header: the guide's name, who is signed in, what is running.
    list->AddText(large, 36 * scale, at(left + 136, 238), fade(245, 247, 250), Tr("Guia PS5X360"));
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%.0f FPS", fps_);
    const std::string who = profile_.empty() ? std::string(speed) : profile_ + "   ·   " + speed;
    const ImVec2 who_size = medium->CalcTextSizeA(28 * scale, 4096.0f, 0.0f, who.c_str());
    list->AddText(medium, 28 * scale, ImVec2(right * scale - who_size.x, 246 * scale), fade(210, 216, 224), who.c_str());
    // The page that is not open is a dark blade at its side of the panel.
    const float page_left = state_.page == 0 ? left : left + blade + 2;
    const float page_right = state_.page == 0 ? right - blade - 2 : right;
    const float other_left = state_.page == 0 ? right - blade : left;
    list->AddRectFilledMultiColor(at(other_left, top), at(other_left + blade, bottom), fade(96, 108, 124), fade(80, 92, 108),
                                  fade(72, 84, 100), fade(88, 100, 116));
    Sideways(list, medium, 28 * scale, at(other_left + blade - 14, top + 24), fade(238, 241, 245),
             Tr(kGuidePages[1 - state_.page]));
    // The open page: its name on a grey strip, then the list.
    const float strip = 94;
    list->AddRectFilledMultiColor(at(page_left, top), at(page_left + strip, bottom), fade(206, 209, 213), fade(196, 199, 204),
                                  fade(176, 180, 186), fade(186, 190, 196));
    Sideways(list, medium, 28 * scale, at(page_left + strip - 28, top + 24), fade(70, 76, 84), Tr(kGuidePages[state_.page]));
    list->AddRectFilledMultiColor(at(page_left + strip, top), at(page_right, bottom), fade(238, 240, 242), fade(238, 240, 242),
                                  fade(204, 208, 212), fade(204, 208, 212));
    const auto items = GuideItems(state_.page, settings_);
    // The settings page is longer than the panel: it scrolls with the focus.
    const int first_row = state_.page == 0 ? 0
        : std::clamp(state_.row - kGuideRows / 2, 0, std::max(0, int(items.size()) - kGuideRows));
    const int last_row = state_.page == 0 ? int(items.size()) : std::min(int(items.size()), first_row + kGuideRows);
    if (state_.page == 1) {
      char position[24];
      std::snprintf(position, sizeof(position), "%d / %d", state_.row + 1, int(items.size()));
      const ImVec2 size = small->CalcTextSizeA(22 * scale, 4096.0f, 0.0f, position);
      list->AddText(small, 22 * scale, ImVec2(page_right * scale - 24 * scale - size.x, (bottom - 44) * scale),
                    fade(96, 102, 110), position);
    }
    for (int n = first_row; n < last_row; ++n) {
      const float y = top + (n - first_row) * 63.0f;
      const bool focused = n == state_.row;
      if (focused)
        list->AddRectFilledMultiColor(at(page_left + strip, y), at(page_right, y + 62), fade(126, 190, 20), fade(126, 190, 20),
                                      fade(78, 150, 6), fade(78, 150, 6));
      list->AddLine(at(page_left + strip, y + 62), at(page_right, y + 62), fade(170, 175, 181), 1.5f * scale);
      const ImU32 ink = focused ? fade(255, 255, 255) : fade(44, 48, 54);
      list->AddText(large, (state_.page == 0 ? 34 : 30) * scale, at(page_left + strip + 24, y + (state_.page == 0 ? 12 : 15)), ink,
                    items[size_t(n)].label.c_str());
      if (!items[size_t(n)].value.empty()) {
        const ImVec2 size = medium->CalcTextSizeA(28 * scale, 4096.0f, 0.0f, items[size_t(n)].value.c_str());
        list->AddText(medium, 28 * scale, ImVec2(page_right * scale - 24 * scale - size.x, (y + 16) * scale),
                      focused ? fade(255, 255, 255) : fade(90, 96, 104), items[size_t(n)].value.c_str());
      }
    }
    if (!game_.empty()) {
      const std::string running = Tr("Em execução: ") + game_;
      list->AddText(small, 22 * scale, at(page_left + strip + 24, bottom - 44), fade(96, 102, 110), running.c_str());
    }
    // What the buttons do, in the pad's own colours.
    float x = (left + 136) * scale;
    const float y = 794 * scale;
    Button(list, small, scale, x, y, fade(86, 132, 214), "X", state_.page == 0 ? Tr("Selecionar") : Tr("Alterar"));
    Button(list, small, scale, x, y, fade(214, 74, 74), "O", Tr("Voltar ao jogo"));
    Button(list, small, scale, x, y, fade(110, 118, 130), state_.page == 0 ? "<>" : "L1", state_.page == 0 ? Tr("Configurações") : Tr("Jogo"));
    list->AddText(small, 20 * scale, at(left + 136, 835), fade(140, 146, 154), xbox360ps5::kBuildVersionLabel);
    if (!web_.empty()) {
      const ImVec2 size = small->CalcTextSizeA(20 * scale, 4096.0f, 0.0f, web_.c_str());
      list->AddText(small, 20 * scale, ImVec2(right * scale - size.x, 835 * scale), fade(140, 146, 154), web_.c_str());
    }
    list->PopClipRect();
  }
 private:
  GuideState& state_;
  const xbox360ps5::Settings& settings_;
  const std::string& profile_;
  const std::string& game_;
  const float& fps_;
  ImFont* small_;
  ImFont* medium_;
  ImFont* large_;
  const std::string& web_;
};
// How a game's picture is stretched to the screen.
void ApplyPicture(const xbox360ps5::Settings& settings, xe::ui::Presenter* presenter) {
  if (!presenter) return;
  using Config = xe::ui::Presenter::GuestOutputPaintConfig;
  Config config;
  config.SetEffect(settings.image_filter == 2 ? Config::Effect::kFsr
                   : settings.image_filter == 1 ? Config::Effect::kCas : Config::Effect::kBilinear);
  // Soft, as the effects were designed, strong.
  static constexpr float kCasSharpness[] = {Config::kCasAdditionalSharpnessMin, Config::kCasAdditionalSharpnessDefault,
                                            Config::kCasAdditionalSharpnessMax};
  static constexpr float kFsrReduction[] = {1.0f, Config::kFsrSharpnessReductionDefault, Config::kFsrSharpnessReductionMin};
  const int sharpness = std::clamp(settings.sharpness, 0, 2);
  config.SetCasAdditionalSharpness(kCasSharpness[sharpness]);
  config.SetFsrSharpnessReduction(kFsrReduction[sharpness]);
  config.SetDither(settings.dither != 0);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);
}
// What the settings page shows, as JSON: the options with the general values,
// the identified games with their own values, and the page's words in the
// interface's language.
std::string WebState(const xbox360ps5::Settings& global, std::vector<std::pair<std::string, std::string>> games,
                     const std::string& running_id, const std::string& running_name) {
  using xbox360ps5::JsonText;
  std::string json = "{\"version\":" + JsonText(XBOX360PS5_VERSION) + ",\"running\":";
  if (running_name.empty()) json += "null";
  else json += "{\"id\":" + JsonText(running_id) + ",\"name\":" + JsonText(running_name) + "}";
  json += ",\"categories\":[";
  for (size_t n = 0; n < std::size(xbox360ps5::kOptionCategories); ++n)
    json += std::string(n ? "," : "") + JsonText(Tr(xbox360ps5::kOptionCategories[n]));
  json += "],\"options\":[";
  bool first = true;
  for (const auto& option : xbox360ps5::Options()) {
    const char* when = option.when == xbox360ps5::OptionWhen::live ? "live"
                     : option.when == xbox360ps5::OptionWhen::launch ? "launch" : "start";
    json += std::string(first ? "" : ",") + "{\"key\":" + JsonText(option.key) + ",\"label\":" + JsonText(Tr(option.label)) +
            ",\"about\":" + JsonText(Tr(option.about)) + ",\"category\":" + std::to_string(option.category) + ",\"when\":\"" + when +
            "\",\"recommended\":" + std::to_string(option.recommended) + ",\"perGame\":" + (option.per_game ? "true" : "false") +
            ",\"value\":" + std::to_string(global.*option.field) + ",\"choices\":[";
    for (size_t n = 0; n < option.choices.size(); ++n) json += std::string(n ? "," : "") + JsonText(Tr(option.choices[n]));
    json += "]}";
    first = false;
  }
  json += "],\"games\":[";
  bool listed = running_id.empty();
  for (const auto& game : games) listed = listed || game.first == running_id;
  if (!listed) games.insert(games.begin(), {running_id, running_name});
  first = true;
  for (const auto& [id, name] : games) {
    json += std::string(first ? "" : ",") + "{\"id\":" + JsonText(id) + ",\"name\":" + JsonText(name);
    const std::pair<const char*, xbox360ps5::GameOverrides> maps[] = {{"own", xbox360ps5::LoadGameOverrides(id)},
                                                                      {"preset", xbox360ps5::GamePreset(id)}};
    for (const auto& [label, values] : maps) {
      json += std::string(",\"") + label + "\":{";
      bool first_value = true;
      for (const auto& [key, value] : values) {
        json += std::string(first_value ? "" : ",") + JsonText(key) + ":" + std::to_string(value);
        first_value = false;
      }
      json += "}";
    }
    json += "}";
    first = false;
  }
  json += "],\"text\":{\"all\":" + JsonText(Tr("Todos os jogos")) + ",\"general\":" + JsonText(Tr("Geral")) +
          ",\"recommended\":" + JsonText(Tr("Recomendado: ")) + ",\"running\":" + JsonText(Tr("Em execução: ")) +
          ",\"logs\":" + JsonText(Tr("Registros")) + ",\"saved\":" + JsonText(Tr("Salvo")) +
          ",\"zipAll\":" + JsonText(Tr("Baixar todos os registros (.zip)")) +
          ",\"zipGame\":" + JsonText(Tr("Baixar os registros de um jogo (.zip)")) +
          ",\"files\":" + JsonText(Tr("Arquivos mais recentes, um a um")) +
          ",\"none\":" + JsonText(Tr("Nenhuma opção desta página vale por jogo.")) + ",\"live\":" + JsonText(Tr("vale na hora")) +
          ",\"launch\":" + JsonText(Tr("vale ao abrir o jogo")) + ",\"start\":" + JsonText(Tr("vale ao reiniciar o emulador")) + "}}";
  return json;
}
// The options that are not at their recommended value, for the log of a session.
std::string ChangedOptions(const xbox360ps5::Settings& settings) {
  std::string text;
  for (const auto& option : xbox360ps5::Options()) {
    const int value = settings.*option.field;
    if (value == option.recommended) continue;
    text += (text.empty() ? "" : ", ") + std::string(option.key) + "=" + std::to_string(value);
  }
  return text.empty() ? "all recommended" : text;
}
// When a game stops showing frames: what each of its threads is at, twice, and
// a few seconds of every system call it makes. For the log of a console where
// no debugger can be attached.
// What a guest thread waits for, from the note it left (XThread::WaitNote).
std::string WaitText(const xe::kernel::XThread& thread) {
  static const char* const kinds[] = {"Undefined", "Enumerator", "Event", "File", "IOCompletion", "Module", "Mutant", "NotifyListener",
                                      "Semaphore", "Session", "Socket", "SymbolicLink", "Thread", "Timer", "Device"};
  const xe::kernel::XThread::WaitNote note = thread.wait_note();
  if (!note.count) return "not in a wait";
  std::string text = note.count == 1 ? "waits for" : note.all ? "waits for all of " + std::to_string(note.count) + ":" : "waits for any of " + std::to_string(note.count) + ":";
  for (uint32_t n = 0; n < std::min<uint32_t>(note.count, 4); ++n) {
    char item[80];
    std::snprintf(item, sizeof(item), " %s %08X", note.types[n] < std::size(kinds) ? kinds[note.types[n]] : "?", note.handles[n]);
    text += item;
    if (note.guest_objects[n]) { std::snprintf(item, sizeof(item), " (guest %08X)", note.guest_objects[n]); text += item; }
  }
  return text;
}
// The guest code that led a thread to where it is: the return addresses kept
// on its stack (each frame points to its caller's, which holds the return
// address eight bytes below). Read from a running thread, so only memory
// that is readable is followed, and the list can be cut short or stale.
std::string CallersText(xe::Memory* memory, uint32_t stack) {
  const auto readable = [memory](uint32_t address) {
    auto* heap = memory->LookupHeap(address);
    uint32_t protection = 0;
    return heap && heap->QueryProtect(address, &protection) && (protection & xe::kMemoryProtectRead);
  };
  std::string text;
  for (int depth = 0; depth < 8; ++depth) {
    if (!stack || (stack & 3) || !readable(stack)) break;
    const uint32_t caller = xe::load_and_swap<uint32_t>(memory->TranslateVirtual(stack));
    if (caller <= stack || caller - stack > 0x10000 || !readable(caller - 8)) break;
    char item[16];
    std::snprintf(item, sizeof(item), " %08X", xe::load_and_swap<uint32_t>(memory->TranslateVirtual(caller - 8)));
    text += item;
    stack = caller;
  }
  return text.empty() ? " none read" : text;
}
void ReportStall(xe::Emulator& emulator, int pass) {
  const auto threads = emulator.kernel_state()->object_table()->GetObjectsByType<xe::kernel::XThread>();
  XELOGW("STALL pass {}: {} threads", pass, threads.size());
  for (const auto& thread : threads) {
    if (!thread->thread_state()) continue;
    const auto* context = thread->thread_state()->context();
    XELOGW("STALL thread {:08X} {}; callers{}", thread->handle(), WaitText(*thread),
           thread->is_guest_thread() ? CallersText(emulator.memory(), uint32_t(context->r[1])) : std::string(" -"));
    XELOGW("STALL thread {:08X} id {} '{}' guest {} suspended {} lr {:08X} ctr {:08X} r1 {:08X} r3 {:08X} r4 {:08X} r5 {:08X}",
           thread->handle(), thread->thread_id(), thread->thread_name(), thread->is_guest_thread() ? 1 : 0,
           thread->suspend_count(), uint32_t(context->lr), uint32_t(context->ctr), uint32_t(context->r[1]),
           uint32_t(context->r[3]), uint32_t(context->r[4]), uint32_t(context->r[5]));
  }
  xe::FlushLog();
  for (const auto& thread : threads) {
    if (!thread->thread()) continue;
    char name[24];
    std::snprintf(name, sizeof(name), "%08X", unsigned(thread->handle()));
    xbox360ps5::ProbeThread(thread->thread()->native_handle(), name);
  }
}
// Where the time goes in a slow scene: every guest and emulator thread is
// sampled for a few seconds, and the log gets, per thread, the share spent in
// generated guest code, in the emulator and in system libraries, with the
// commonest places of each. Offsets are symbolized afterwards with the build's
// kept ELF. The picture freezes meanwhile; the game keeps running.
void MeasurePerformance(xe::Emulator& emulator, float fps) {
  struct Place { uint64_t key; uint32_t count; };
  struct Tally {
    xe::kernel::object_ref<xe::kernel::XThread> thread;
    uint32_t total = 0, guest = 0, title = 0, system = 0;
    uint64_t cpus_seen = 0;
    // Guest functions, title code, callers into system libraries, the import
    // slots those calls went through, and title code by megabyte.
    std::vector<Place> places[5];
    void Add(int kind, uint64_t key) {
      for (auto& place : places[kind]) if (place.key == key) { ++place.count; return; }
      places[kind].push_back({key, 1});
    }
  };
  std::vector<Tally> tallies;
  for (auto& thread : emulator.kernel_state()->object_table()->GetObjectsByType<xe::kernel::XThread>())
    if (thread->thread()) tallies.push_back({thread});
  auto* code_cache = emulator.processor()->backend()->code_cache();
  const auto started = std::chrono::steady_clock::now();
  const uint64_t swaps = emulator.graphics_system()->command_processor()->swap_count();
  const unsigned long long protects = xbox360ps5::protect_calls, syscalls = xbox360ps5::protect_syscalls,
                           spent = xbox360ps5::protect_nanoseconds, faults = xbox360ps5::fault_count,
                           opens = xbox360ps5::open_syscalls, open_spent = xbox360ps5::open_nanoseconds,
                           pages = xbox360ps5::protect_pages;
  int rounds = 0;
  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
    for (auto& tally : tallies) {
      xbox360ps5::ThreadSample sample;
      if (!xbox360ps5::SampleThread(tally.thread->thread()->native_handle(), &sample)) continue;
      ++tally.total;
      if (sample.cpu >= 0 && sample.cpu < 64) tally.cpus_seen |= uint64_t(1) << sample.cpu;
      if (sample.in_title) { ++tally.title; tally.Add(1, sample.rip & ~uint64_t(0x3F)); tally.Add(4, sample.rip >> 20); }
      else if (sample.rip >= 0x40000000 && sample.rip < 0x50000000) {
        ++tally.guest;
        auto* function = code_cache->LookupFunction(sample.rip);
        tally.Add(0, function ? function->address() : 0);
      } else { ++tally.system; tally.Add(2, sample.caller | (sample.caller2 << 32)); tally.Add(3, sample.slot); }
    }
    ++rounds;
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  XELOGW("MEASURE {}: {} rounds in {:.1f} s, {} threads, {:.1f} swaps/s during it, {:.0f} FPS before",
         emulator.title_name(), rounds, seconds, tallies.size(),
         double(emulator.graphics_system()->command_processor()->swap_count() - swaps) / seconds, fps);
  XELOGW("MEASURE memory: {:.0f} protect calls/s, {:.0f} kernel protections/s taking {:.1f} ms/s, {:.0f} faults/s",
         double(xbox360ps5::protect_calls - protects) / seconds, double(xbox360ps5::protect_syscalls - syscalls) / seconds,
         double(xbox360ps5::protect_nanoseconds - spent) / seconds / 1e6, double(xbox360ps5::fault_count - faults) / seconds);
  XELOGW("MEASURE memory: of those, {:.0f}/s open pages for writing taking {:.1f} ms/s; {:.0f} kernel pages/s changed",
         double(xbox360ps5::open_syscalls - opens) / seconds, double(xbox360ps5::open_nanoseconds - open_spent) / seconds / 1e6,
         double(xbox360ps5::protect_pages - pages) / seconds);
  {
    unsigned given = 0, not_given = 0;
    xbox360ps5::ThreadPlaceCounts(&given, &not_given);
    XELOGW("MEASURE threads: GPU core {}; {} placements accepted, {} refused",
           xbox360ps5::GpuCoreDedicated() ? "dedicated" : "shared", given, not_given);
  }
  for (auto& tally : tallies) {
    if (!tally.total) continue;
    std::string line = fmt::format("MEASURE thread {:08X} '{}' samples {} guest {}% title {}% system {}%",
                                   tally.thread->handle(), tally.thread->thread_name(), tally.total,
                                   100 * tally.guest / tally.total, 100 * tally.title / tally.total,
                                   100 * tally.system / tally.total);
    line += fmt::format(" | seen on cpus {:X} of {:X} priority {}", tally.cpus_seen,
                        xbox360ps5::ThreadCpus(tally.thread->thread()->native_handle()),
                        xbox360ps5::ThreadPriority(tally.thread->thread()->native_handle()));
    const char* const kinds[] = {" | guest", " | title", " | system-from", " | imports", " | title-MiB"};
    for (int kind = 0; kind < 5; ++kind) {
      auto& places = tally.places[kind];
      std::sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.count > b.count; });
      line += kinds[kind];
      for (size_t n = 0; n < places.size() && n < (kind >= 2 ? 12u : 6u); ++n) {
        // A system-library sample names its caller and the caller above it.
        if (kind == 2 && (places[n].key >> 32))
          line += fmt::format(" {:X}<{:X}:{}%", places[n].key & 0xFFFFFFFFu, places[n].key >> 32,
                              100 * places[n].count / tally.total);
        else
          line += fmt::format(" {:X}:{}%", places[n].key, 100 * places[n].count / tally.total);
      }
    }
    XELOGW("{}", line);
  }
  // The GPU command thread once more, by itself, with the sampler seated on its
  // CPU (xbox360ps5::SeatSampler): the shares above lean towards system calls.
  for (auto& tally : tallies) {
    if (tally.thread->thread_name().find("GPU Commands") == std::string::npos) continue;
    void* const handle = tally.thread->thread()->native_handle();
    const auto seat = xbox360ps5::SeatSampler(handle, 2);
    if (!seat.seated) { XELOGW("MEASURE focused: the system refused the sampler's seat"); break; }
    Tally focus{tally.thread};
    uint32_t missed = 0;
    const auto focus_started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - focus_started < std::chrono::seconds(3)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      xbox360ps5::ThreadSample sample;
      if (!xbox360ps5::SampleThread(handle, &sample)) { ++missed; continue; }
      ++focus.total;
      if (sample.cpu >= 0 && sample.cpu < 64) focus.cpus_seen |= uint64_t(1) << sample.cpu;
      if (sample.in_title) { ++focus.title; focus.Add(1, sample.rip & ~uint64_t(0x3F)); focus.Add(4, sample.rip >> 20); }
      else if (sample.rip >= 0x40000000 && sample.rip < 0x50000000) ++focus.guest;
      else { ++focus.system; focus.Add(2, sample.caller | (sample.caller2 << 32)); }
    }
    xbox360ps5::UnseatSampler(handle, seat);
    if (!focus.total) { XELOGW("MEASURE focused: no sample taken ({} missed)", missed); break; }
    std::string line = fmt::format("MEASURE focused '{}' samples {} missed {} seen on cpus {:X} | guest {}% title {}% system {}%",
                                   tally.thread->thread_name(), focus.total, missed, focus.cpus_seen,
                                   100 * focus.guest / focus.total, 100 * focus.title / focus.total,
                                   100 * focus.system / focus.total);
    const std::pair<int, const char*> kinds[] = {{4, " | title-MiB"}, {2, " | system-from"}, {1, " | title"}};
    for (const auto& [kind, name] : kinds) {
      auto& places = focus.places[kind];
      std::sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.count > b.count; });
      line += name;
      // Tenths of a percent: the title's buckets are many and small.
      uint32_t covered = 0;
      for (size_t n = 0; n < places.size() && n < (kind == 1 ? 48u : 12u); ++n) {
        covered += places[n].count;
        if (kind == 2 && (places[n].key >> 32))
          line += fmt::format(" {:X}<{:X}:{:.1f}", places[n].key & 0xFFFFFFFFu, places[n].key >> 32,
                              100.0 * places[n].count / focus.total);
        else
          line += fmt::format(" {:X}:{:.1f}", places[n].key, 100.0 * places[n].count / focus.total);
      }
      if (kind == 1) line += fmt::format(" (these are {:.0f}% of the samples)", 100.0 * covered / focus.total);
    }
    XELOGW("{}", line);
    break;
  }
  xe::FlushLog();
}
// The profile the user chose in the launcher, kept between runs.
const char kProfileFile[] = "/download0/xbox360ps5/profile.txt";
uint64_t SavedProfile() {
  std::ifstream file(kProfileFile);
  std::string text;
  file >> text;
  return text.empty() ? 0 : std::strtoull(text.c_str(), nullptr, 16);
}
void SaveProfile(uint64_t xuid) {
  char text[32];
  std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(xuid));
  std::ofstream(kProfileFile, std::ios::trunc) << text << "\n";
}
class NativeContext final : public xe::ui::WindowedAppContext {
 public:
  void Tick() {
    ExecutePendingFunctionsFromUIThread();
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(2));
  }
 protected:
  void NotifyUILoopOfPendingFunctions() override { wake_.notify_all(); }
  void PlatformQuitFromUIThread() override { wake_.notify_all(); }
 private:
  std::mutex mutex_;
  std::condition_variable wake_;
};
class NativeWindow final : public xe::ui::Window {
 public:
  explicit NativeWindow(NativeContext& context) : Window(context, "PS5X360", 1920, 1080) {}
  ~NativeWindow() override { EnterDestructor(); }
  void Paint() { OnPaint(); }
  std::vector<uint8_t> TakeIcon() { return std::move(icon_); }
  void SubmitUiPad(const xbox360ps5::PadSample& pad) {
    const uint32_t current = pad.connected ? pad.buttons : 0;
    const uint32_t changed = current ^ ui_buttons_;
    using Key = xe::ui::VirtualKey;
    const std::pair<uint32_t, Key> keys[] = {{0x10, Key::kUp}, {0x20, Key::kRight},
      {0x40, Key::kDown}, {0x80, Key::kLeft}, {0x4000, Key::kReturn},
      {0x2000, Key::kEscape}, {0x8000, Key::kTab}};
    WindowDestructionReceiver receiver(this);
    for (const auto& [button, key] : keys) {
      if (!(changed & button)) continue;
      xe::ui::KeyEvent event(this, key, 1, bool(ui_buttons_ & button), false, false, false, false);
      if (current & button) OnKeyDown(event, receiver); else OnKeyUp(event, receiver);
    }
    ui_buttons_ = current;
  }
 protected:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1920, 1080, WindowResizeAction::kManual, receiver);
    OnFocusUpdate(true, receiver);
    OnDesiredFullscreenUpdate(true);
    return true;
  }
  void RequestCloseImpl() override {
    WindowDestructionReceiver receiver(this);
    OnBeforeClose(receiver);
    OnAfterClose();
    app_context().RequestDeferredQuit();
  }
  std::unique_ptr<xe::ui::Surface> CreateSurfaceImpl(xe::ui::Surface::TypeFlags allowed) override {
    if (!(allowed & xe::ui::Surface::kTypeFlag_KhrDisplay)) return nullptr;
    return std::make_unique<xbox360ps5::DisplaySurface>(1920, 1080);
  }
  void RequestPaintImpl() override {}  // The native event loop calls Paint.
  void LoadAndApplyIcon(const void* buffer, size_t size, bool) override {
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    icon_.assign(bytes, bytes + (buffer ? size : 0));
  }
 private:
  uint32_t ui_buttons_ = 0;
  std::vector<uint8_t> icon_;
};
class NativePad {
 public:
  NativePad() {
    owners_.fill(-1); handles_.fill(-1);
    (void)sceUserServiceInitialize(nullptr);
    if (sceUserServiceGetInitialUser(&user) < 0) {
      std::array<int32_t, 4> users{-1, -1, -1, -1};
      if (sceUserServiceGetLoginUserIdList(users.data()) >= 0)
        for (auto id : users) if (id >= 0) { user = id; break; }
    }
    initialized_ = scePadInit() >= 0;
    owners_[0] = user;
  }
  ~NativePad() { for (auto handle : handles_) if (handle >= 0) scePadClose(handle); }
  xbox360ps5::PadSample Read(size_t slot = 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    xbox360ps5::PadSample state;
    if (slot >= 4 || !initialized_) return state;
    const auto now = std::chrono::steady_clock::now();
    if (slot == 0 && now >= refresh_at_) {
      refresh_at_ = now + std::chrono::milliseconds(500);
      std::array<int32_t, 4> users{-1, -1, -1, -1};
      if (sceUserServiceGetLoginUserIdList(users.data()) >= 0) {
        for (size_t n = 1; n < 4; ++n) {
          if (owners_[n] >= 0 && std::find(users.begin(), users.end(), owners_[n]) == users.end()) {
            if (handles_[n] >= 0) scePadClose(handles_[n]);
            handles_[n] = owners_[n] = -1;
          }
        }
        for (auto id : users) if (id >= 0 && std::find(owners_.begin(), owners_.end(), id) == owners_.end()) {
          for (size_t n = 1; n < 4; ++n) if (owners_[n] < 0) { owners_[n] = id; break; }
        }
      }
      for (size_t n = 0; n < 4; ++n) if (owners_[n] >= 0 && handles_[n] < 0) {
        handles_[n] = scePadOpen(owners_[n], 0, 0, nullptr);
        std::printf("NATIVE PAD player=%zu user=%08x handle=%08x\n", n + 1, unsigned(owners_[n]), unsigned(handles_[n]));
      }
    }
    alignas(16) std::array<uint8_t, 256> bytes{};
    if (handles_[slot] < 0) return state;
    if (scePadReadState(handles_[slot], bytes.data()) < 0) {
      scePadClose(handles_[slot]); handles_[slot] = -1; return state;
    }
    state.buttons = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    state.lx = bytes[4]; state.ly = bytes[5]; state.rx = bytes[6]; state.ry = bytes[7];
    state.l2 = bytes[8]; state.r2 = bytes[9]; state.connected = bytes[76] != 0;
    return state;
  }
  bool Rumble(size_t slot, uint16_t left, uint16_t right) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot >= 4 || handles_[slot] < 0) return false;
    const uint8_t motors[2] = {uint8_t(left >> 8), uint8_t(right >> 8)};
    return scePadSetVibration(handles_[slot], motors) >= 0;
  }
  int32_t user = 0xff;
 private:
  std::array<int32_t, 4> owners_, handles_;
  std::mutex mutex_;
  bool initialized_ = false;
  std::chrono::steady_clock::time_point refresh_at_{};
};
struct DetachPresentation {
  NativeWindow& window;
  xe::ui::ImGuiDrawer& drawer;
  xe::ui::ImmediateDrawer& immediate;
  xbox360ps5::Launcher& launcher;
  ~DetachPresentation() {
    launcher.SetDrawer(nullptr);  // Cover textures go before their drawer.
    drawer.SetPresenterAndImmediateDrawer(nullptr, nullptr);
    immediate.SetPresenter(nullptr);
    window.SetPresenter(nullptr);
  }
};
}
// Canary's log lines also go to the network stream (everything) and to the
// console's kernel log (warnings and errors only: it is a small, slow ring).
class GameLogSink final : public xe::LogSink {
 public:
  xbox360ps5::SessionLog session;
  void Write(const char* text, size_t size) override { session.Write(text, size); }
  void Flush() override { session.Flush(); }
};
class ConsoleLogSink final : public xe::LogSink {
 public:
  void Write(const char* text, size_t size) override {
    // The logger hands over a line in pieces (its prefix, then the text).
    pending_.append(text, size);
    for (size_t end; (end = pending_.find('\n')) != std::string::npos; pending_.erase(0, end + 1)) {
      if (end) Line(pending_.data(), end);
    }
    if (pending_.size() > 4096) pending_.clear();
  }
  void Flush() override {}
 private:
  void Line(const char* text, size_t size) {
    char buffer[512];
    const int length = std::snprintf(buffer, sizeof(buffer), "[X360] %.*s\n", int(std::min<size_t>(size, 480)), text);
    if (length <= 0) return;
    // A game can repeat one warning thousands of times a second: past 30 lines
    // in a second the kernel log gets no more, so the game is not slowed down
    // and a crash report is not pushed out of the ring.
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (second_.exchange(now) != now) lines_ = 0;
    if ((text[0] == '!' || text[0] == 'w') && ++lines_ <= 30) sceKernelDebugOutText(0, buffer);
    xbox360ps5::NetLog(buffer, std::min<size_t>(size_t(length), sizeof(buffer) - 1));
  }
  std::string pending_;
  std::atomic<int64_t> second_{0};
  std::atomic<int> lines_{0};
};
int main(int argc, char** argv) {
  const std::filesystem::path storage = "/download0/xbox360ps5";
  std::error_code error;
  std::filesystem::create_directories(storage, error);
  if (error) { std::fprintf(stderr, "Writable storage unavailable: %s\n", error.message().c_str()); return 1; }
  std::error_code boot_log_error;
  std::filesystem::create_directories("/app0/logs", boot_log_error);
  xbox360ps5::InstallCrashReport(boot_log_error ? "/download0/xbox360ps5/boot.log" : "/app0/logs/boot.log");
  using xbox360ps5::Stage;
  Stage("BOOT main entered");
  {
    char place[96], line[160];
    xbox360ps5::DescribeThread(place, sizeof(place));
    std::snprintf(line, sizeof(line), "BOOT first thread: %s", place);
    Stage(line);
    // Off the GPU command thread's core; the threads it starts inherit that.
    xbox360ps5::PlaceCallingThread(false);
    xbox360ps5::DescribeThread(place, sizeof(place));
    std::snprintf(line, sizeof(line), "BOOT first thread placed: %s", place);
    Stage(line);
  }
  // Whether a C++ exception can be thrown and caught on this console. The
  // emulator core relies on it in places; if the unwinder does not work every
  // throw ends the title, and the boot log says so here.
  {
    static std::jmp_buf escape;
    const auto previous = std::set_terminate([] { std::longjmp(escape, 1); });
    bool caught = false;
    if (!setjmp(escape)) {
      try { throw std::runtime_error("probe"); } catch (const std::exception&) { caught = true; }
    }
    std::set_terminate(previous);
    Stage(caught ? "BOOT exceptions: thrown and caught" : "BOOT exceptions: NOT caught (unwinding fails)");
  }
  Stage("BOOT version " XBOX360PS5_VERSION);
  // Relative paths must resolve to "no such file" rather than a sandbox error.
  if (chdir("/download0/xbox360ps5")) Stage("BOOT chdir refused");
  xbox360ps5::ReportPlatformMemory();
  Stage(xbox360ps5::StartNetLog(9100) ? "BOOT log stream on TCP 9100" : "BOOT log stream unavailable");
  // The general options: what the launcher shows, changes and saves.
  xbox360ps5::Settings global;
  global.Load();
  // A start the launcher asked for, straight into one game: the game has
  // options of its own that are only read as the emulator starts.
  std::filesystem::path once_game;
  std::string once_title;
  {
    std::ifstream once(storage / "launch-once.txt");
    std::string line;
    if (std::getline(once, line) && !line.empty()) {
      once_game = line;
      std::getline(once, once_title);
    }
    once.close();
    std::error_code once_error;
    std::filesystem::remove(storage / "launch-once.txt", once_error);
  }
  std::string settings_title = once_title;
  xbox360ps5::GameOverrides overrides = xbox360ps5::LoadGameOverrides(settings_title);
  // The options in force: the general ones with the running game's own over them.
  xbox360ps5::Settings settings = xbox360ps5::ForGame(global, settings_title, overrides);
  // What v0.5.6 switched on for every game is now asked for, per game. The
  // title has made no core thread and holds no lock yet: the core's mutexes
  // may still be told to spin before they block (xenia/base/mutex.h).
  xe::ps5_spin_mutexes = settings.fast_locks != 0;
  // The driver's worker-thread recording is a test. A start with it on that
  // did not get as far as the launcher or a running game turns it off.
  bool driver_test_withdrawn = false;
  {
    std::error_code check_error;
    const auto check = storage / "start-check.txt";
    if (std::filesystem::exists(check, check_error)) {
      std::filesystem::remove(check, check_error);
      if (settings.threaded_driver) {
        global.threaded_driver = settings.threaded_driver = 0;
        global.Save();
        driver_test_withdrawn = true;
      }
    } else if (settings.threaded_driver) {
      std::ofstream(check, std::ios::trunc) << "threaded_driver\n";
      setenv("RADV_THREADED_RECORDING", "1", 1);
    }
  }
  settings.Apply();
  {
    std::error_code once_error;
    if (std::filesystem::remove("/app0/detailed-once.txt", once_error)) {
      settings.detailed_logs = 1;
      settings.Apply();
    }
    // One start that keeps the bytes of small resolves and textures, to
    // compare a picture that is wrong with the PC's: /app0/dump-once.txt.
    if (std::filesystem::remove("/app0/dump-once.txt", once_error)) {
      static const char folder[] = "/app0/logs/dump";
      std::filesystem::remove_all(folder, once_error);
      std::filesystem::create_directories(folder, once_error);
      xbox360ps5::gpu_diag::dump_folder = folder;
    }
  }
  // Canary caches the vblank period at graphics setup. Apply only on startup;
  // the library saves changes and restarts rather than mutating a live flag.
  cvars::vsync = settings.vsync != 0;
  const xbox360ps5::Settings started = settings;
  auto owned_game_log = std::make_unique<GameLogSink>();
  auto* game_log = owned_game_log.get();
  std::filesystem::path log_root;
  bool log_ready = false;
  // Prefer the visible installation folder. Some title mounts deny writes
  // outside download0, so preserve logging even in those environments.
  for (const auto& candidate : {std::filesystem::path("/data/homebrew/PPSA50011/logs"),
                                std::filesystem::path("/app0/logs"), storage / "LOGS"}) {
    if (game_log->session.Begin(candidate, "Launcher", "", "library", XBOX360PS5_VERSION)) {
      log_root = candidate;
      log_ready = true;
      break;
    }
  }
  // The dedicated sink owns file routing; the core's fixed file sink is unused.
  cvars::log_file = log_ready ? std::filesystem::path("/dev/null") : storage / "engine.log";
  // The kernel's dialogs (sign-in, messages) answer themselves for now.
  cvars::headless = true;
  xe::InitializeLogging("PS5X360");
  xe::AddLogSink(std::move(owned_game_log));
  xe::AddLogSink(std::make_unique<ConsoleLogSink>());
  if (log_ready) XELOGW("Logs: {}", game_log->session.Path().string());
  xbox360ps5::gpu_diag::enabled = settings.memory_boost != 0;
  XELOGW("Compatibility: spinning locks {}, video-memory optimizations {} (both off is the v0.5.5 behaviour)",
         settings.fast_locks ? "on" : "off", settings.memory_boost ? "on" : "off");
  XELOGW("Logging: {} (level {}), platform stdout disabled", settings.detailed_logs ? "detailed" : "normal", cvars::log_level);
  // The settings page for a phone on the same network. Its key is shown on
  // screen only: it does not go to the logs, which are shared.
  if (global.web_page && log_ready)
    XELOGW("Web settings: {}", xbox360ps5::StartWebSettings(log_root) ? "page server started" : "the page server could not start");
  if (log_ready && log_root == storage / "LOGS")
    XELOGW("Installation folder is not writable; desktop log downloader exports sessions to /data/homebrew/PPSA50011/logs");
  std::filesystem::path game;
  if (argc > 1 && argv[1]) game = argv[1];
  else {
    std::ifstream input(storage / "game.txt");
    std::string line;
    if (std::getline(input, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); game = line; }
  }
  if (game.empty() && !once_game.empty()) game = once_game;
  if (game.empty()) {
    std::ifstream input("/app0/assets/game.txt");
    std::string line;
    if (std::getline(input, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); game = line; }
  }
  // An explicit path starts that game at once; otherwise the library opens.
  if (!game.empty() && !std::filesystem::is_regular_file(game, error)) {
    XELOGE("Configured game {} does not exist; opening the library", game.string());
    game.clear();
  }
  XELOGI("GAME FILE {}", game.empty() ? "(library)" : game.string());
  Stage("BOOT window");
  NativeContext context;
  NativeWindow window(context);
  if (!window.Open()) return 3;
  Stage("BOOT imgui");
  xe::ui::ImGuiDrawer drawer(&window, 1);
  drawer.GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Text sized for a television; the emulator's own dialogs use it as well.
  const auto fonts = xbox360ps5::Launcher::LoadFonts(drawer.GetIO());
  if (fonts.f28) drawer.GetIO().FontDefault = fonts.f28;
  else { drawer.GetIO().FontGlobalScale = 2.5f; Stage("BOOT launcher font missing, using the built-in one"); }
  xbox360ps5::Launcher launcher(fonts, global);
  launcher.SetStarted(started);
  std::string web_hint;
  const auto show_web_address = [&launcher, &web_hint] {
    const std::string plain = xbox360ps5::WebSettingsPlainAddress(), key = xbox360ps5::WebSettingsKey();
    launcher.SetWebPage(xbox360ps5::WebSettingsAddress(), plain, key);
    web_hint = plain.empty() ? std::string() : std::string(Tr("Celular: ")) + plain + "    " + Tr("Código: ") + key;
  };
  show_web_address();
  if (driver_test_withdrawn)
    launcher.SetMessage(Tr("O teste do driver de vídeo em segunda thread foi desligado: o emulador não abriu com ele."));
  // Why the previous run could not start a game, when it left a note.
  {
    std::ifstream notice(storage / "notice.txt");
    std::string reason;
    if (std::getline(notice, reason) && !reason.empty()) {
      launcher.SetMessage(std::string(Tr("Não foi possível iniciar o jogo: ")) + reason);
      notice.close();
      std::error_code notice_error;
      std::filesystem::remove(storage / "notice.txt", notice_error);
    }
  }
  Stage("BOOT pad");
  NativePad pad;
  int result = 0;
  bool restart = false;
  {
    // The title's own storage (/download0) is an image of a few hundred
    // megabytes: enough for saves and profiles, not for shader and module
    // caches and the guest's cache partitions, which filled it. Once full, a
    // game not started before could not even make its cache folder and the
    // title aborted. What can be made again goes to the installation folder
    // when that is writable (the console's whole disk), and the copies in the
    // save storage are removed to give the room back to saves.
    std::filesystem::path bulk = storage;
    {
      std::error_code bulk_error;
      const std::filesystem::path candidate = "/app0/cache";
      std::filesystem::create_directories(candidate, bulk_error);
      bool writable = false;
      if (!bulk_error) {
        { std::ofstream probe(candidate / ".probe", std::ios::trunc); probe << "probe"; probe.flush(); writable = probe.good(); }
        std::filesystem::remove(candidate / ".probe", bulk_error);
      }
      if (writable) {
        bulk = candidate;
        for (const char* old : {"cache", "utility-cache"}) {
          std::error_code remove_error;
          const auto removed = std::filesystem::remove_all(storage / old, remove_error);
          if (!remove_error && removed) XELOGW("Storage: removed {} cache entries from {}", removed, (storage / old).string());
          else if (remove_error) XELOGW("Storage: could not clear {}: {}", (storage / old).string(), remove_error.message());
        }
      }
      // How much of the save storage is in use: its real size is not reported
      // by the system (it answers with the size of the disk).
      uint64_t used = 0;
      std::error_code walk_error;
      for (std::filesystem::recursive_directory_iterator entry(storage, walk_error), end; !walk_error && entry != end;
           entry.increment(walk_error)) {
        std::error_code size_error;
        if (entry->is_regular_file(size_error)) { const auto size = entry->file_size(size_error); if (!size_error) used += size; }
      }
      XELOGW("Storage: caches in {}; save storage {} holds {} KiB", bulk.string(), storage.string(), used / 1024);
    }
    Stage("BOOT emulator constructor");
    Stage("BOOT preparing homebrew saves (original content retained)");
    auto saves = xbox360ps5::PrepareSaveStorage(storage / "content", "/data/homebrew/PPSA50011/saves");
    if (saves.root == storage / "content") {
      XELOGW("Saves: {}; trying installation mount /app0/saves", saves.notice);
      saves = xbox360ps5::PrepareSaveStorage(storage / "content", "/app0/saves");
    }
    XELOGW("Saves: {}; {}", saves.root.string(), saves.notice);
    launcher.SetSaveRoot(saves.root);
    xe::Emulator emulator("", storage, saves.root, bulk / (bulk == storage ? "cache" : "core"));
    Stage("BOOT emulator setup");
    xbox360ps5::DualSenseInput* input = nullptr;
    std::array<xbox360ps5::DualSenseInput*, 4> player_inputs{};
    auto status = emulator.Setup(&window, &drawer, true,
      [](xe::cpu::Processor* cpu) -> std::unique_ptr<xe::apu::AudioSystem> {
        return std::make_unique<xbox360ps5::CanaryAudioSystem>(cpu);
      },
      []() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
        return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
      },
      [&input, &player_inputs, &pad](xe::ui::Window*) {
        std::vector<std::unique_ptr<xe::hid::InputDriver>> drivers;
        for (uint32_t slot = 0; slot < 4; ++slot) {
          auto driver = std::make_unique<xbox360ps5::DualSenseInput>(
              [&pad, slot](uint16_t left, uint16_t right) { return pad.Rumble(slot, left, right); }, slot);
          player_inputs[slot] = driver.get(); drivers.push_back(std::move(driver));
        }
        input = player_inputs[0]; return drivers;
      });
    XELOGI("ENGINE SETUP {:08X}", status);
    Stage("BOOT emulator setup returned");
    xbox360ps5::ReportPlatformMemory();
    if (status) { result = 4; }
    else {
      if (!xbox360ps5::MountUtilityCache(*emulator.file_system(), bulk)) {
        XELOGE("Required utility cache is unavailable; launch cancelled");
        return 7;
      }
      auto immediate = emulator.graphics_system()->provider()->CreateImmediateDrawer();
      if (!immediate || !emulator.graphics_system()->presenter()) {
        XELOGE("Native Vulkan presentation/UI initialization failed");
        return 6;
      }
      DetachPresentation detach{window, drawer, *immediate, launcher};
      window.SetPresenter(emulator.graphics_system()->presenter());
      // The drawer uploads its textures through the presenter.
      immediate->SetPresenter(emulator.graphics_system()->presenter());
      drawer.SetPresenterAndImmediateDrawer(emulator.graphics_system()->presenter(), immediate.get());
      launcher.SetDrawer(immediate.get());
      // The system keeps the title's start-up picture (sce_sys/pic0.png) on screen
      // until the title says it is ready to show its own.
      sceSystemServiceHideSplashScreen();
      xbox360ps5::motion::SetEnabled(settings.motion_phone != 0);
      xbox360ps5::SetUiSoundVolume(settings.ui_sound_volume);
      xbox360ps5::MuteUiSounds(settings.mute || !settings.ui_sounds);
      xbox360ps5::StartUiSounds();
      // Achievements a game unlocks show as console notifications.
      xbox360ps5::StartNotifier(pad.user);
      xbox360ps5::achievement_earned = [&settings, &emulator](const xbox360ps5::EarnedAchievement& earned) {
        XELOGW("Achievement: '{}' ({}G) earned in {:08X}", earned.name, earned.gamerscore, earned.title_id);
        if (!settings.achievement_toasts) return;
        xbox360ps5::Toast toast;
        toast.title = earned.name;
        const auto* profile = emulator.kernel_state()->xam_state()->profile_manager()->GetProfile(earned.xuid);
        toast.text = (profile ? profile->name() + "  -  " : std::string()) + std::to_string(earned.gamerscore) + "G  -  " + Tr("Conquista desbloqueada");
        toast.icon_path = xbox360ps5::SaveAchievementIcon(earned.title_id, earned.id, earned.icon);
        toast.trophy_sound = true;
        xbox360ps5::Notify(std::move(toast));
      };
      {
        // A notification without earning anything: a file put in the title's folder asks for one.
        std::error_code test_error;
        if (std::filesystem::remove("/app0/notify-test.txt", test_error))
          xbox360ps5::Notify({"PS5X360", Tr("Avisos de conquista funcionando"), "", true});
      }
      int toasts_were = global.achievement_toasts;
      // The settings page: what it shows, and the changes it asked for. A change
      // for every game goes to the general options; one for a game, to its own.
      unsigned web_tick = 0;
      const auto publish_web = [&](const std::string& running_id, const std::string& running_name) {
        if (xbox360ps5::WebSettingsKey().empty()) return;
        xbox360ps5::PublishWebState(WebState(global, launcher.Games(), running_id, running_name));
      };
      const auto apply_web = [&](bool in_game) {
        const auto changes = xbox360ps5::TakeWebChanges();
        if (changes.empty()) return false;
        for (const auto& change : changes) {
          const xbox360ps5::Option* option = nullptr;
          for (const auto& known : xbox360ps5::Options()) if (change.key == known.key) option = &known;
          if (!option || change.value >= int(option->choices.size())) continue;
          if (change.scope.empty()) {
            if (change.value < 0) continue;
            global.*option->field = change.value;
            global.Save();
          } else if (option->per_game) {
            auto own = xbox360ps5::LoadGameOverrides(change.scope);
            if (change.value < 0) own.erase(change.key);
            else own[change.key] = change.value;
            xbox360ps5::SaveGameOverrides(change.scope, own);
            if (in_game && change.scope == settings_title) overrides = own;
          }
          XELOGW("Web settings: {} set to {} for {}", change.key, change.value, change.scope.empty() ? "every game" : change.scope);
        }
        if (in_game) {
          settings = xbox360ps5::ForGame(global, settings_title, overrides);
          settings.Apply();
          ApplyPicture(settings, emulator.graphics_system()->presenter());
        } else {
          global.Apply();
        }
        return true;
      };
      publish_web("", "");
      emulator.on_exit.AddListener([&context] { context.RequestDeferredQuit(); });
      bool launched = false;
      xbox360ps5::GameEntry chosen;
      // An unattended compatibility run, when the title folder asks for one.
      xbox360ps5::AutoTest autotest;
      // Games give the controller to a signed-in profile: make one the first time.
      auto profiles = emulator.kernel_state()->xam_state()->profile_manager();
      if (!profiles->GetAccountCount()) {
        XELOGW("Profile: {}", profiles->CreateProfile("Player", true) ? "created" : "could not be created");
      }
      // The first controller's profile: the one chosen in the launcher, or the
      // first there is. A game's saves go to the profile signed in when it starts.
      const auto signed_in = [profiles]() -> uint64_t {
        for (const auto& [xuid, account] : *profiles->GetAccounts())
          if (profiles->GetUserIndexAssignedToProfile(xuid) == 0) return xuid;
        return 0;
      };
      const auto sign_in = [profiles, signed_in](uint64_t xuid) {
        if (signed_in() == xuid) return;
        // Out of any other controller's slot first, then into the first.
        const uint8_t slot = profiles->GetUserIndexAssignedToProfile(xuid);
        if (slot < 4) profiles->Logout(slot, false);
        if (signed_in()) profiles->Logout(0, false);
        profiles->Login(xuid, 0, false);
      };
      {
        const auto& accounts = *profiles->GetAccounts();
        const uint64_t saved = SavedProfile();
        if (accounts.count(saved)) sign_in(saved);
        else if (!signed_in() && !accounts.empty()) sign_in(accounts.begin()->first);
      }
      // Retain preferred accounts, but sign in extra users only for real pads.
      const auto player_profile_path = saves.root / "local-players.txt";
      std::array<uint64_t, 4> player_xuids{};
      { std::ifstream file(player_profile_path); for (auto& id : player_xuids) file >> std::hex >> id; }
      player_xuids[0] = signed_in();
      for (uint8_t slot = 1; slot < 4; ++slot) {
        if (const auto* current = profiles->GetProfile(slot)) player_xuids[slot] = current->xuid();
        profiles->Logout(slot, false); // Remove old persisted phantom sign-ins.
      }
      const auto connect_player = [&](uint8_t slot, bool notify) {
        if (!pad.Read(slot).connected) return;
        if (const auto* current = profiles->GetProfile(slot)) {
          player_xuids[slot] = current->xuid(); return;
        }
        uint64_t wanted = player_xuids[slot];
        if (!profiles->GetAccounts()->count(wanted) || profiles->GetUserIndexAssignedToProfile(wanted) < 4) {
          wanted = 0;
          for (const auto& [id, account] : *profiles->GetAccounts())
            if (account.GetGamertagString() == "Player " + std::to_string(slot + 1) && profiles->GetUserIndexAssignedToProfile(id) >= 4) wanted = id;
          if (!wanted && profiles->CreateProfile("Player " + std::to_string(slot + 1), false))
            for (const auto& [id, account] : *profiles->GetAccounts())
              if (account.GetGamertagString() == "Player " + std::to_string(slot + 1) && profiles->GetUserIndexAssignedToProfile(id) >= 4) wanted = id;
        }
        if (wanted) profiles->Login(wanted, slot, notify);
        const auto* current = profiles->GetProfile(slot);
        player_xuids[slot] = current ? current->xuid() : 0;
      };
      for (uint8_t slot = 1; slot < 4; ++slot) connect_player(slot, false);
      const auto save_players = [&] {
        const auto temporary = player_profile_path.string() + ".tmp";
        std::ofstream file(temporary, std::ios::trunc);
        for (uint8_t slot = 0; slot < 4; ++slot) {
          const auto* profile = profiles->GetProfile(slot);
          if (profile) player_xuids[slot] = profile->xuid();
          if (!profiles->GetAccounts()->count(player_xuids[slot])) player_xuids[slot] = 0;
          file << std::hex << player_xuids[slot] << "\n";
        }
        file.close(); if (file) { std::error_code error; std::filesystem::rename(temporary, player_profile_path, error); }
      };
      save_players();
      std::array<bool, 4> players_connected{};
      const auto poll_extra_players = [&] {
        for (size_t slot = 1; slot < 4; ++slot) {
          const auto sample = pad.Read(slot); player_inputs[slot]->Submit(sample);
          if (sample.connected != players_connected[slot]) {
            players_connected[slot] = sample.connected;
            if (sample.connected) connect_player(uint8_t(slot), true);
            else profiles->Logout(uint8_t(slot), true);
            save_players();
            const auto* profile = profiles->GetProfile(uint8_t(slot));
            const std::string name = profile ? profile->name() : "Player " + std::to_string(slot + 1);
            XELOGW("Local player {}: {} ({})", slot + 1, sample.connected ? "connected" : "disconnected", name);
            xbox360ps5::Notify({"PS5X360", name + " — " + Tr(sample.connected ? "Controle conectado" : "Controle desconectado"), "", false});
          }
        }
      };
      std::string profile_name;
      const auto refresh_name = [&profile_name, profiles, signed_in] {
        const auto& accounts = *profiles->GetAccounts();
        const auto found = accounts.find(signed_in());
        profile_name = found == accounts.end() ? std::string() : found->second.GetGamertagString();
      };
      refresh_name();
      unsigned active_profiles = 0;
      for (uint8_t slot = 0; slot < 4; ++slot) active_profiles += profiles->GetProfile(slot) != nullptr;
      XELOGW("Profile: primary {}, {} signed in, {} stored accounts", profile_name.empty() ? "none" : profile_name, active_profiles, profiles->GetAccountCount());
      xbox360ps5::ProfileHooks profile_hooks;
      profile_hooks.list = [profiles] {
        std::vector<xbox360ps5::ProfileEntry> entries;
        for (const auto& [xuid, account] : *profiles->GetAccounts())
          entries.push_back({account.GetGamertagString(), xuid, profiles->GetUserIndexAssignedToProfile(xuid) == 0, profiles->GetUserIndexAssignedToProfile(xuid) < 4 ? int(profiles->GetUserIndexAssignedToProfile(xuid)) : -1});
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return entries;
      };
      profile_hooks.use = [sign_in, refresh_name](uint64_t xuid) {
        sign_in(xuid);
        SaveProfile(xuid);
        refresh_name();
        XELOGW("Profile: switched to {:016X}", xuid);
      };
      profile_hooks.create_player = [&, profiles](const std::string& name, uint32_t slot) {
        if (slot >= 4 || !xe::kernel::xam::ProfileManager::IsGamertagValid(name)) return false;
        if (slot && !pad.Read(slot).connected) return false;
        for (const auto& [id, account] : *profiles->GetAccounts())
          if (account.GetGamertagString() == name) return false;
        if (!profiles->CreateProfile(name, false)) return false;
        for (const auto& [id, account] : *profiles->GetAccounts()) {
          if (account.GetGamertagString() != name) continue;
          profiles->Logout(uint8_t(slot), false);
          profiles->Login(id, uint8_t(slot), true);
          if (slot == 0) { SaveProfile(id); refresh_name(); }
          save_players(); return profiles->GetUserIndexAssignedToProfile(id) == slot;
        }
        return false;
      };
      profile_hooks.use_player = [&, profiles](uint64_t xuid, uint32_t slot) {
        if (slot >= 4 || !profiles->GetAccounts()->count(xuid)) return false;
        if (slot && !pad.Read(slot).connected) return false;
        const uint8_t assigned = profiles->GetUserIndexAssignedToProfile(xuid);
        if (assigned < 4 && assigned != slot) return false;
        if (assigned != slot) { profiles->Logout(uint8_t(slot), false); profiles->Login(xuid, uint8_t(slot), true); }
        if (slot == 0) { SaveProfile(xuid); refresh_name(); }
        save_players(); return profiles->GetUserIndexAssignedToProfile(xuid) == slot;
      };
      profile_hooks.remove = [&](uint64_t xuid) {
        if (launched || xuid == signed_in() || profiles->GetAccountCount() <= 1) return false;
        if (!profiles->ArchiveProfile(xuid)) return false;
        for (auto& id : player_xuids) if (id == xuid) id = 0;
        save_players(); refresh_name(); return true;
      };
      launcher.SetAchievements([&emulator, profiles](uint32_t title, uint32_t slot) {
        std::vector<xbox360ps5::AchievementEntry> list;
        auto* profile = slot < 4 ? profiles->GetProfile(uint8_t(slot)) : nullptr;
        if (!profile) return list;
        for (const auto& entry : emulator.kernel_state()->xam_state()->achievement_manager()->GetTitleAchievements(profile->xuid(), title))
          list.push_back({xe::to_utf8(entry.achievement_name), xe::to_utf8(entry.IsUnlocked() ? entry.unlocked_description : entry.locked_description), entry.achievement_id, entry.gamerscore, entry.IsUnlocked()});
        std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        return list;
      }, [profiles](uint32_t slot) { const auto* profile = slot < 4 ? profiles->GetProfile(uint8_t(slot)) : nullptr; return profile ? profile->name() : std::string(); });
      launcher.SetProfiles(std::move(profile_hooks));
      // A game that starts another executable (a collection's menu) or goes back
      // to the dashboard: Canary saves what to run next in launch_data.bin (in
      // the working folder) and this restarts the title, which carries on from it.
      auto xam = emulator.kernel_state()->GetKernelModule<xe::kernel::xam::XamModule>("xam.xex");
      xe::kernel::xam::XamModule::restart_hook = [] {
        XELOGW("Title switch: restarting the title");
        xe::FlushLog();
        xbox360ps5::DrainNetLog();
        const int refused = sceSystemServiceLoadExec("/app0/eboot.bin", nullptr);
        XELOGE("Title switch: restart refused {:08X}", unsigned(refused));
      };
      xam->LoadLoaderData();
      const bool switched = game.empty() && !xam->loader_data().host_path.empty();
      if (switched) {
        game = xam->loader_data().host_path;
        XELOGW("Title switch: starting {} ({} bytes of launch data)", game.string(),
               xam->loader_data().launch_data.size());
      }
      if (game.empty()) {
        launcher.Scan();
        const auto paths = launcher.GamePaths();
        if (autotest.Begin(paths)) {
          game = paths[size_t(autotest.index)];
          chosen = {game.parent_path().filename().string(), game, "XEX", game.parent_path().string()};
        }
      }
      if (!game.empty()) chosen = {game.extension() == ".xex" ? game.parent_path().filename().string() : game.stem().string(),
                                  game, "XEX", game.parent_path().string()};
      if (!once_game.empty() && game == once_game) chosen.title_id = once_title;
      const auto start_reached = [&storage] {
        std::error_code reached_error;
        std::filesystem::remove(storage / "start-check.txt", reached_error);
      };
      while (!launched && !restart && !context.HasQuitFromUIThread()) {
        xbox360ps5::LauncherDialog* dialog = nullptr;
        if (game.empty()) {
          // The launcher: pick a game with the pad.
          Stage("BOOT launcher");
          start_reached();
          launcher.Scan();
          dialog = new xbox360ps5::LauncherDialog(&drawer, launcher);
          uint32_t held = ~0u;  // Buttons already down when the screen opens do not count.
          auto repeat_at = std::chrono::steady_clock::now();
          while (!launcher.TakeLaunch(chosen) && !context.HasQuitFromUIThread()) {
            if (launcher.TakeRestart()) { restart = true; break; }
            const auto sample = pad.Read();
            poll_extra_players();
            launcher.SetPadConnected(sample.connected);
            uint32_t buttons = sample.connected ? sample.buttons : 0;
            if (sample.connected && sample.ly < 64) buttons |= 0x10;
            if (sample.connected && sample.ly > 192) buttons |= 0x40;
            if (sample.connected && sample.lx < 64) buttons |= 0x80;
            if (sample.connected && sample.lx > 192) buttons |= 0x20;
            uint32_t pressed = buttons & ~held;
            const auto now = std::chrono::steady_clock::now();
            // Holding a direction keeps moving.
            if (pressed & 0xF0) repeat_at = now + std::chrono::milliseconds(400);
            else if ((buttons & 0xF0) && now >= repeat_at) {
              pressed |= buttons & 0xF0;
              repeat_at = now + std::chrono::milliseconds(110);
            }
            held = buttons;
            using xbox360ps5::Key;
            const std::pair<uint32_t, Key> keys[] = {{0x10, Key::up}, {0x40, Key::down}, {0x80, Key::left},
              {0x20, Key::right}, {0x4000, Key::cross}, {0x2000, Key::circle}, {0x1000, Key::triangle},
              {0x8000, Key::square}, {0x400, Key::l1}, {0x800, Key::r1}, {0x8, Key::options}};
            for (const auto& [button, key] : keys) if (pressed & button) launcher.Press(key);
            // The interface's sounds: moving about, choosing, going back.
            xbox360ps5::MuteUiSounds(global.mute || !global.ui_sounds);
            xbox360ps5::motion::SetEnabled(global.motion_phone != 0);
            xbox360ps5::SetUiSoundVolume(global.ui_sound_volume);
            if (pressed & 0x2000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::back);
            else if (pressed & 0xD008) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::select);
            else if (pressed & 0xCF0) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::move);
            // Switching the achievement notifications on shows what one looks like.
            if (global.achievement_toasts && !toasts_were)
              xbox360ps5::Notify({"PS5X360", Tr("Avisos de conquista ligados"), "", true});
            toasts_were = global.achievement_toasts;
            if (apply_web(false) || ++web_tick % 90 == 0) { show_web_address(); publish_web("", ""); }
            context.Tick(); window.Paint();
          }
          if (restart || context.HasQuitFromUIThread()) { dialog->Dismiss(); break; }
          game = chosen.path;
          // Show the loading screen before the launch blocks this thread.
          for (int frame = 0; frame < 3; ++frame) { context.Tick(); window.Paint(); }
        }
        xe::FlushLog();
        std::string log_id = chosen.title_id;
        if (log_id.empty()) log_id = xbox360ps5::ReadXexTitleId(game);
        // This game's own options over the general ones.
        settings_title = log_id;
        overrides = xbox360ps5::LoadGameOverrides(settings_title);
        settings = xbox360ps5::ForGame(global, settings_title, overrides);
        if (!switched && xbox360ps5::StartOptionsDiffer(settings, started)) {
          // Some are only read as the emulator starts: start again, straight into this game.
          XELOGW("Settings: {} has start options of its own; restarting into it", chosen.name);
          std::ofstream(storage / "launch-once.txt", std::ios::trunc) << game.string() << "\n" << settings_title << "\n";
          restart = true;
          break;
        }
        settings.Apply();
        if (game_log->session.Begin(log_root, chosen.name, log_id, game.string(), XBOX360PS5_VERSION)) {
          const auto log_path = game_log->session.Path();
          if (!xbox360ps5::SetCrashReportFile(log_path.c_str())) XELOGW("Crash report remains in boot.log");
          XELOGW("Game log: {}", log_path.string());
        } else XELOGE("Could not create game log; retaining the current destination");
        std::error_code space_error;
        const auto save_space = std::filesystem::space(saves.root, space_error);
        XELOGW("Save storage: {} available {} bytes, capacity {} bytes, query error {}",
               saves.root.string(), space_error ? 0 : save_space.available,
               space_error ? 0 : save_space.capacity, space_error.value());
        if (!space_error && save_space.available < 16u * 1024 * 1024) {
          XELOGW("Save storage is low; game saves may fail. No user data was removed.");
        }
        const std::string launch_stage = "BOOT game " + chosen.name + " source " + game.string();
        Stage(launch_stage.c_str());
        Stage("BOOT launch");
        XELOGI("GAME FILE {}", game.string());
        window.TakeIcon();
        // Kept with a later launch request, so the restart finds the game again.
        xam->loader_data().host_path = xe::path_to_utf8(game);
        // Canary reads the emulated console's XConfig, rather than the old
        // user_language cvar. Set it in big-endian form before guest code runs.
        xe::be<uint32_t> game_language = uint32_t(settings.GameLanguage());
        emulator.kernel_state()->xconfig()->WriteSetting(
            xe::kernel::XCONFIG_USER_CATEGORY,
            xe::kernel::XCONFIG_USER_CATEGORY_ENTRIES::XCONFIG_USER_LANGUAGE, &game_language);
        XELOGI("Language: interface {}, game {}, console {}", xbox360ps5::ui_language.load(),
               settings.GameLanguage(), settings.console_language);
        try {
          xbox360ps5::motion::SetEnabled(settings.motion_phone != 0);
          status = emulator.LaunchPath(game);
        } catch (const std::exception& error) {
          // The core is half way into the game: only a fresh start is safe.
          // The reason is kept for the library to show.
          XELOGE("Launch of {} failed with an exception: {}", game.string(), error.what());
          Stage("BOOT launch threw; restarting");
          std::ofstream(storage / "notice.txt", std::ios::trunc) << chosen.name << ": " << error.what() << "\n";
          restart = true;
          break;
        }
        XELOGI("GAME LAUNCH {:08X}", status);
        XELOGW("Game: '{}' title {:08X}, launch status {:08X}", emulator.title_name(), emulator.title_id(), status);
        XELOGW("Graphics: emulated VSync {}", xbox360ps5::EffectiveVsync(settings.vsync) ? "on" : "off");
        XELOGW("Settings: {} of this game's own ({}); not at the recommended value: {}", overrides.size(),
               settings_title.empty() ? "title not identified yet" : settings_title, ChangedOptions(settings));
        if (!status) start_reached();
        Stage("BOOT launch returned");
        // After a switch the running executable is not the one the library lists.
        if (!status && !switched) launcher.RecordLaunch(chosen, emulator.title_id(), emulator.title_name(), window.TakeIcon(),
                                           emulator.kernel_state()->GetExecutableModule()->hash().value_or(0));
        launcher.SetLoading("");
        if (dialog) { dialog->Dismiss(); context.Tick(); window.Paint(); }
        if (!status) { launched = true; break; }
        if (!dialog) { result = 5; break; }
        char text[96];
        std::snprintf(text, sizeof(text), Tr("Não foi possível iniciar este jogo (erro %08X)."), unsigned(status));
        launcher.SetMessage(text);
        game.clear();
      }
      if (launched) {
        // The game's frame rate: shown when asked for, and in the log every 30 seconds.
        if (xbox360ps5::gpu_diag::enabled) {
          for (unsigned i = 0; i < unsigned(xbox360ps5::gpu_diag::Kind::count); ++i) {
            if (i >= unsigned(xbox360ps5::gpu_diag::Kind::cpu_draw) && !xbox360ps5::gpu_diag::stages_enabled) continue;
                  const auto timing = xbox360ps5::gpu_diag::counters[i].Take();
            XELOGW("Performance startup gpu-api {}: calls {} total {:.3f} ms worst {:.3f} ms errors {}", xbox360ps5::gpu_diag::names[i], timing.calls, double(timing.nanoseconds)/1e6, double(timing.worst)/1e6, timing.errors);
          }
        }
        xbox360ps5::gpu_diag::Reset();
        float fps = 0.0f;
        bool stalled_notice=false;
        new FpsOverlay(&drawer,fps,settings.show_fps,stalled_notice,settings.touchpad_menu);
        // The emulator's guide over the game.
        GuideState guide;
        if (autotest.active) guide.hint = 0.0f;
        const std::string game_name = emulator.title_name().empty() ? chosen.name : emulator.title_name();
        new Guide(&drawer, guide, settings, profile_name, game_name, fps, fonts.f24, fonts.f28, fonts.f36, web_hint);
        show_web_address();
        publish_web(settings_title, game_name);
        ApplyPicture(settings, emulator.graphics_system()->presenter());
        bool trigger_was = true, swallow = false, quit = false;
        uint32_t guide_held = ~0u;
        auto guide_repeat = std::chrono::steady_clock::now();
        auto back_until = std::chrono::steady_clock::time_point::min();
        auto measure_at = std::chrono::steady_clock::time_point::max();
        bool auto_measured = false;
        int slow_summaries = 0;
        // The watch for a game that stops showing frames.
        const auto watch_started=std::chrono::steady_clock::now();
        xbox360ps5::FrameWatch frame_watch(emulator.graphics_system()->command_processor()->refreshed_output_count(),0);
        const int32_t usual_log_level = cvars::log_level;
        // The last system calls of the game are kept in memory for the report
        // of a stop (see ReportStall); each game starts with none. Experimental
        // builds only: every call is turned into text, a cost that was never
        // measured. A public build's report has the waits and the callers.
#ifdef XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS
        xe::logging::KeepRecentLines(true);
#endif
        // A game that keeps drawing the same thing at one steady, low rate for
        // two minutes is probably waiting on a loading screen: reported once.
        float steady_rate = 0.0f;
        int steady_summaries = 0;
        bool steady_reported = false;
        auto sample_at = std::chrono::steady_clock::now();
        uint64_t sampled_frames = emulator.graphics_system()->command_processor()->refreshed_output_count();
        uint64_t sampled_swaps = emulator.graphics_system()->command_processor()->swap_count();
        int samples = 0;
        float fps_sum = 0.0f, fps_low = 1e9f, swaps_sum = 0.0f;
        const auto game_started = std::chrono::steady_clock::now();
        double next_shot = 5;
        while (!context.HasQuitFromUIThread()) {
          auto sample = pad.Read();
          poll_extra_players();
          if (autotest.active) {
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - game_started).count();
            sample.connected = true;
            sample.buttons |= autotest.Buttons(elapsed);
            if (elapsed >= next_shot) {
              next_shot += 10;
              xe::ui::RawImage image;
              if (emulator.graphics_system()->presenter()->CaptureGuestOutput(image))
                xbox360ps5::AutoTest::SendShot(image, autotest.index, std::to_string(int(elapsed)) + "s");
              XELOGW("AUTOTEST {} at {:.0f} s: {:.1f} FPS, {} frames", autotest.index + 1, elapsed, fps,
                     emulator.graphics_system()->command_processor()->swap_count());
            }
            if (elapsed >= autotest.seconds) {
              xbox360ps5::DrainNetLog();
              restart = true;
              break;
            }
          }
          // The touchpad click (or OPTIONS with it, when the touchpad is the
          // Back button) opens and closes the guide.
          uint32_t buttons = sample.connected ? sample.buttons : 0;
          const bool trigger = settings.touchpad_menu ? bool(buttons & 0x100000) : (buttons & 0x100008) == 0x100008;
          if (trigger && !trigger_was) {
            guide.open = !guide.open;
            xbox360ps5::PlayUiSound(guide.open ? xbox360ps5::UiSound::select : xbox360ps5::UiSound::back);
            guide.page = guide.row = 0;
            guide.hint = 0.0f;
            guide_held = ~0u;
            swallow = true;
          }
          trigger_was = trigger;
          if (guide.open) {
            if (sample.ly < 64) buttons |= 0x10;
            if (sample.ly > 192) buttons |= 0x40;
            if (sample.lx < 64) buttons |= 0x80;
            if (sample.lx > 192) buttons |= 0x20;
            uint32_t pressed = buttons & ~guide_held;
            guide_held = buttons;
            // Holding up or down keeps moving: the settings page is long.
            const auto guide_now = std::chrono::steady_clock::now();
            if (pressed & 0x50) guide_repeat = guide_now + std::chrono::milliseconds(400);
            else if ((buttons & 0x50) && guide_now >= guide_repeat) {
              pressed |= buttons & 0x50;
              guide_repeat = guide_now + std::chrono::milliseconds(110);
            }
            xbox360ps5::MuteUiSounds(settings.mute || !settings.ui_sounds);
            xbox360ps5::SetUiSoundVolume(settings.ui_sound_volume);
            if (pressed & 0x2000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::back);
            else if (pressed & 0x4000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::select);
            else if (pressed & 0xCF0) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::move);
            auto items = GuideItems(guide.page, settings);
            const int rows = int(items.size());
            if (pressed & 0x10) guide.row = (guide.row + rows - 1) % rows;
            if (pressed & 0x40) guide.row = (guide.row + 1) % rows;
            if (pressed & 0x2000) guide.open = false;
            // L1, R1: the other page. Left and right too, on the page of actions.
            if ((pressed & 0xC00) || (guide.page == 0 && (pressed & 0xA0))) { guide.page = 1 - guide.page; guide.row = 0; }
            else if (pressed & 0x40A0) {
              const int step = (pressed & 0x80) ? -1 : 1;
              bool changed = true;
              switch (items[size_t(guide.row)].action) {
                case GuideAction::resume: guide.open = false; changed = false; break;
                case GuideAction::shelf: XELOGW("Guide: back to the launcher"); restart = true; changed = false; break;
                case GuideAction::quit: quit = true; changed = false; break;
                case GuideAction::back_button:
                  guide.open = false;
                  changed = false;
                  back_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                  break;
                case GuideAction::option: {
                  const xbox360ps5::Option& option = *items[size_t(guide.row)].option;
                  const int count = int(option.choices.size());
                  int& value = settings.*option.field;
                  value = (value + step + count) % count;
                  // Kept where the value came from: this game's own options, or the general ones.
                  if (overrides.count(option.key) || xbox360ps5::GamePreset(settings_title).count(option.key)) {
                    overrides[option.key] = value;
                    xbox360ps5::SaveGameOverrides(settings_title, overrides);
                  } else {
                    global.*option.field = value;
                    global.Save();
                  }
                  if (option.field == &xbox360ps5::Settings::touchpad_menu) trigger_was = true;
                  XELOGW("Guide: {} set to {} at {:.1f} FPS", option.key, option.choices[size_t(value)], fps);
                  break;
                }
                case GuideAction::measure:
                  guide.open = false;
                  changed = false;
                  measure_at = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                  break;
              }
              if (changed) {
                settings.Apply();
                ApplyPicture(settings, emulator.graphics_system()->presenter());
                publish_web(settings_title, game_name);
              }
              if (restart || quit) break;
            }
          }
          // The game sees a pad at rest while the guide is up, and until the
          // buttons that closed it are let go.
          if (guide.open || swallow) {
            if (!guide.open && !buttons) swallow = false;
            sample = xbox360ps5::PadSample();
            sample.connected = true;
          }
          // The touchpad belongs to the guide; the game's Back comes from it.
          if (settings.touchpad_menu) sample.buttons &= ~0x100000u;
          if (std::chrono::steady_clock::now() < back_until) sample.buttons |= 0x100000;
          if (apply_web(true) || ++web_tick % 600 == 0) publish_web(settings_title, game_name);
          xbox360ps5::motion::SetEnabled(settings.motion_phone != 0);
          input->Submit(sample);
          window.SubmitUiPad(sample);
          context.Tick(); window.Paint();
          // A measurement asked for in the guide starts once the game is back in play.
          if (std::chrono::steady_clock::now() >= measure_at) {
            measure_at = std::chrono::steady_clock::time_point::max();
            MeasurePerformance(emulator, fps);
          }
          if (std::chrono::steady_clock::now() - sample_at >= std::chrono::seconds(1)) {
            const auto at = std::chrono::steady_clock::now();
            auto* processor = emulator.graphics_system()->command_processor();
            const uint64_t frames = processor->refreshed_output_count();
            const uint64_t swaps = processor->swap_count();
            const float elapsed = std::chrono::duration<float>(at - sample_at).count();
            fps = float(frames - sampled_frames) / elapsed;
            swaps_sum += float(swaps - sampled_swaps) / elapsed;
            sampled_swaps = swaps;
            sample_at = at; sampled_frames = frames;
            const double watch_seconds=std::chrono::duration<double>(at-watch_started).count();
            switch(frame_watch.Observe(frames,watch_seconds)) {
              case xbox360ps5::FrameWatch::capture:
                XELOGW("STALL title {:08X} '{}' after {:.0f}s, {} refreshed outputs, {} submitted swaps; no output is not proof of deadlock",
                       emulator.title_id(),game_name,watch_seconds,frames,swaps);
                ReportStall(emulator, 1);
                // What the game asked of the system before it stopped.
                XELOGW("STALL recent lines, oldest first:");
                xe::logging::WriteRecentLines();
                XELOGW("STALL recent lines end");
                cvars::log_level = 3;  // Every system call, for a moment.
                break;
              case xbox360ps5::FrameWatch::finish_capture:
                cvars::log_level = usual_log_level;
                ReportStall(emulator, 2);
                break;
              case xbox360ps5::FrameWatch::recovered:
                XELOGW("STALL over: frames again"); cvars::log_level=usual_log_level;
                break;
              default: break;
            }
            stalled_notice=frame_watch.Notice(watch_seconds);
            fps_sum += fps; fps_low = std::min(fps_low, fps);
            const int summary_samples =
#ifdef XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS
                xbox360ps5::gpu_diag::enabled ? 5 : 30;
#else
                30;
#endif
            if (++samples == summary_samples) {
              if (xbox360ps5::gpu_diag::enabled) {
                for (unsigned i = 0; i < unsigned(xbox360ps5::gpu_diag::Kind::count); ++i) {
                  if (i >= unsigned(xbox360ps5::gpu_diag::Kind::cpu_draw) && !xbox360ps5::gpu_diag::stages_enabled) continue;
                  const auto timing = xbox360ps5::gpu_diag::counters[i].Take();
                  XELOGW("Performance stage {}: calls {} total {:.3f} ms worst {:.3f} ms errors {}; inclusive wall time, stages overlap; not GPU utilization", xbox360ps5::gpu_diag::names[i], timing.calls, double(timing.nanoseconds)/1e6, double(timing.worst)/1e6, timing.errors);
                }
              }
              XELOGW("Performance: rendered output {:.1f}/s, minimum {:.0f}; submitted swaps {:.1f}/s over {} samples",
                     fps_sum / summary_samples, fps_low, swaps_sum / summary_samples, summary_samples);
              // What reached the console from the camera and what the game took.
              if (xbox360ps5::motion::enabled.load()) XELOGW("Motion: {}", xbox360ps5::motion::Status());
              // What the memory watches cost in the same half minute.
              {
                static unsigned long long closes = 0, opens = 0, spent = 0, open_spent = 0, faults = 0;
                const unsigned long long all = xbox360ps5::protect_syscalls, open = xbox360ps5::open_syscalls,
                                         time = xbox360ps5::protect_nanoseconds, open_time = xbox360ps5::open_nanoseconds,
                                         fault = xbox360ps5::fault_count;
                XELOGW("Performance: memory watches {:.0f} closes/s ({:.1f} ms/s), {:.0f} opens/s ({:.1f} ms/s), {:.0f} faults/s",
                       double(all - open - closes) / summary_samples, double(time - open_time - spent) / (summary_samples * 1e6), double(open - opens) / summary_samples,
                       double(open_time - open_spent) / (summary_samples * 1e6), double(fault - faults) / summary_samples);
                closes = all - open; opens = open; spent = time - open_time; open_spent = open_time; faults = fault;
              }
              // Draws made while their shader was still being compiled show
              // a stand-in: brief gaps in a frame, lasting ones in what a game
              // draws once and keeps.
              {
                static unsigned long long stand_ins = 0, waits = 0;
                const unsigned long long stand_in = xbox360ps5::gpu_diag::stand_in_draws, wait = xbox360ps5::gpu_diag::pipeline_waits;
                if (stand_in != stand_ins || wait != waits) {
                  XELOGW("Performance: shaders still compiling: {} draws used a stand-in, {} waited for theirs",
                         stand_in - stand_ins, wait - waits);
                }
                stand_ins = stand_in; waits = wait;
              }
              // An experimental build samples its threads by itself, once in a
              // session, after four slow summaries in a row (about twenty
              // seconds of play under 40 frames a second): the picture stands
              // still for the five seconds it takes, and the summary that
              // covers them reads lower than the game was.
#ifdef XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS
              if (xbox360ps5::gpu_diag::enabled && !auto_measured) {
                const float average = fps_sum / summary_samples;
                slow_summaries = average > 5.0f && average < 40.0f ? slow_summaries + 1 : 0;
                if (slow_summaries >= 4 && !guide.open) {
                  auto_measured = true;
                  XELOGW("Performance measure: sampling the threads once, {:.1f} FPS in the last summary", average);
                  measure_at = std::chrono::steady_clock::now();
                }
              }
#endif
              {
                const float average = fps_sum / summary_samples;
                const bool steady = average > 1.0f && average < 25.0f && fps_low > average - 1.5f &&
                                    steady_rate > 0.0f && std::abs(average - steady_rate) < 0.3f;
                steady_summaries = steady ? steady_summaries + 1 : 0;
                steady_rate = average;
                if (steady_summaries >= 3 && !steady_reported && summary_samples == 30) {
                  steady_reported = true;
                  XELOGW("WAITING title {:08X} '{}': {:.1f} frames a second, unchanged for two minutes; what its threads are at, then its recent system calls",
                         emulator.title_id(), game_name, average);
                  ReportStall(emulator, 1);
                  XELOGW("STALL recent lines, oldest first:");
                  xe::logging::WriteRecentLines();
                  XELOGW("STALL recent lines end");
                }
              }
              if (xbox360ps5::gpu_diag::enabled) xe::FlushLog();
              samples = 0; fps_sum = swaps_sum = 0.0f; fps_low = 1e9f;
            }
          }
        }
        if (quit) {
          // "Fechar o emulador": the system ends the title.
          XELOGW("Menu: closing the title");
          xe::FlushLog();
          xbox360ps5::DrainNetLog();
          sceSystemServiceLoadExec("exit", nullptr);
          for (;;) usleep(100000);
        }
        if (!restart && emulator.is_title_open()) emulator.TerminateTitle();
      }
      if (!restart) context.ExecutePendingFunctionsFromUIThread();
    }
    if (restart) {
      // A fresh process is the reliable way back to the launcher: the system
      // ends this one and runs the title's executable again.
      XELOGI("ENGINE RESTART");
      Stage("BOOT restarting the title");
      xe::ShutdownLogging();
      const int refused = sceSystemServiceLoadExec("/app0/eboot.bin", nullptr);
      char text[64];
      std::snprintf(text, sizeof(text), "BOOT restart refused %08x", unsigned(refused));
      Stage(text);
    }
  }
  XELOGI("ENGINE EXIT {}", result);
  xe::ShutdownLogging();
  return result;
}
// main returned: ask the system to end the title. The C library's exit() is
// answered with a signal in a native title, which the system reports as a crash.
extern "C" void Xbox360PS5LoaderStage(const char* stage) {
  xbox360ps5::Stage(stage);
}

extern "C" void catchReturnFromMain(int status) {
  char text[64];
  std::snprintf(text, sizeof(text), "BOOT main returned %d", status);
  xbox360ps5::Stage(text);
  sceSystemServiceLoadExec("exit", nullptr);
  for (;;) usleep(100000);
}
