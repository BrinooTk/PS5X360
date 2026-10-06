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
#include "xbox360ps5/display_surface.hpp"
#include "xbox360ps5/dualsense_input.hpp"
#include "xbox360ps5/autotest.hpp"
#include "xbox360ps5/launcher.hpp"
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
  FpsOverlay(xe::ui::ImGuiDrawer* drawer,const float& fps,const bool& shown,const bool& stalled,
             const bool& touchpad_menu)
      : ImGuiDialog(drawer),fps_(fps),shown_(shown),stalled_(stalled),touchpad_menu_(touchpad_menu) {}
 protected:
  void OnDraw(ImGuiIO& io) override {
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
  const bool& shown_;
  const bool& stalled_;
  const bool& touchpad_menu_;
};
// The emulator's guide over a running game, after the Xbox 360's: a light
// panel that unfolds from the middle of the screen, a page of actions and a page
// of settings. The loop that reads the pad drives it; the dialog only draws.
enum class GuideAction { resume, shelf, back_button, quit, fps, sound, filter, scale, touchpad, measure, window, lock_free, gpu_core, unwatched };
struct GuideItem { GuideAction action; std::string label, value; };
struct GuideState {
  bool open = false;
  int page = 0, row = 0;
  float openness = 0.0f;  // 0 closed, 1 fully revealed.
  float hint = 7.0f;  // Seconds the reminder of how to open it stays up.
};
constexpr const char* kGuidePages[] = {"Jogo", "Configurações"};
std::vector<GuideItem> GuideItems(int page, const xbox360ps5::Settings& settings) {
  using xbox360ps5::Settings;
  if (page == 0) {
    std::vector<GuideItem> items = {{GuideAction::resume, Tr("Continuar jogo"), ""},
                                    {GuideAction::shelf, Tr("Voltar para o menu de jogos"), ""}};
    if (settings.touchpad_menu) items.push_back({GuideAction::back_button, Tr("Apertar BACK no jogo"), ""});
    items.push_back({GuideAction::quit, Tr("Fechar o emulador"), ""});
    return items;
  }
  return {{GuideAction::fps, Tr("Mostrar FPS"), settings.show_fps ? Tr("Ligado") : Tr("Desligado")},
          {GuideAction::sound, Tr("Som"), settings.mute ? Tr("Mudo") : Tr("Ligado")},
          {GuideAction::filter, Tr("Filtro de imagem"), Settings::FilterName(settings.image_filter)},
          {GuideAction::touchpad, Tr("Clique do touchpad"), settings.touchpad_menu ? Tr("Abre o guia") : Tr("Botão Back")},
          {GuideAction::measure, Tr("Medir desempenho (5 s)"), ""}};
}
// In an experimental build the settings page also chooses the invalidation
// window, so that sizes are compared in the same scene of the same session.
const char* WindowName(uint32_t bytes) { return bytes == 0x4000 ? "16 KiB" : bytes == 0x10000 ? "64 KiB" : "256 KiB"; }
std::vector<GuideItem> GuideItemsWithExperiments(int page, const xbox360ps5::Settings& settings) {
  auto items = GuideItems(page, settings);
  if (page == 1 && xbox360ps5::gpu_diag::enabled)
  {
    items.push_back({GuideAction::window, Tr("Atualização de memória"),
                     WindowName(xbox360ps5::gpu_diag::invalidation_window)});
    // The lock-free answer stays on (98% of requests took it); its place in the
    // list goes to the arrangement being compared now.
    items.push_back({GuideAction::unwatched, Tr("Otimizar buffers dinâmicos"),
                     xbox360ps5::gpu_diag::unwatched_pages ? Tr("Ligado") : Tr("Desligado")});
  }
  return items;
}
class Guide final : public xe::ui::ImGuiDialog {
 public:
  Guide(xe::ui::ImGuiDrawer* drawer, GuideState& state, const xbox360ps5::Settings& settings, const std::string& profile,
        const std::string& game, const float& fps, ImFont* small, ImFont* medium, ImFont* large)
      : ImGuiDialog(drawer), state_(state), settings_(settings), profile_(profile), game_(game), fps_(fps),
        small_(small), medium_(medium), large_(large) {}
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
    const auto items = GuideItemsWithExperiments(state_.page, settings_);
    for (int n = 0; n < int(items.size()); ++n) {
      const float y = top + n * 63.0f;
      const bool focused = n == state_.row;
      if (focused)
        list->AddRectFilledMultiColor(at(page_left + strip, y), at(page_right, y + 62), fade(126, 190, 20), fade(126, 190, 20),
                                      fade(78, 150, 6), fade(78, 150, 6));
      list->AddLine(at(page_left + strip, y + 62), at(page_right, y + 62), fade(170, 175, 181), 1.5f * scale);
      const ImU32 ink = focused ? fade(255, 255, 255) : fade(44, 48, 54);
      list->AddText(large, 34 * scale, at(page_left + strip + 24, y + 12), ink, items[size_t(n)].label.c_str());
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
};
// How a game's picture is stretched to the screen.
void ApplyImageFilter(const xbox360ps5::Settings& settings, xe::ui::Presenter* presenter) {
  if (!presenter) return;
  using Config = xe::ui::Presenter::GuestOutputPaintConfig;
  Config config;
  config.SetEffect(settings.image_filter == 2 ? Config::Effect::kFsr
                   : settings.image_filter == 1 ? Config::Effect::kCas : Config::Effect::kBilinear);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);
}
// When a game stops showing frames: what each of its threads is at, twice, and
// a few seconds of every system call it makes. For the log of a console where
// no debugger can be attached.
void ReportStall(xe::Emulator& emulator, int pass) {
  const auto threads = emulator.kernel_state()->object_table()->GetObjectsByType<xe::kernel::XThread>();
  XELOGW("STALL pass {}: {} threads", pass, threads.size());
  for (const auto& thread : threads) {
    if (!thread->thread_state()) continue;
    const auto* context = thread->thread_state()->context();
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
    (void)sceUserServiceInitialize(nullptr);
    if (sceUserServiceGetInitialUser(&user) < 0) {
      std::array<int32_t, 4> users{-1, -1, -1, -1};
      if (sceUserServiceGetLoginUserIdList(users.data()) >= 0) {
        for (auto id : users) if (id >= 0) { user = id; break; }
      }
    }
    if (scePadInit() >= 0) handle = scePadOpen(user, 0, 0, nullptr);
    std::printf("NATIVE PAD user=%08x handle=%08x\n", unsigned(user), unsigned(handle));
  }
  ~NativePad() { if (handle >= 0) scePadClose(handle); }
  xbox360ps5::PadSample Read() {
    xbox360ps5::PadSample state;
    alignas(16) std::array<uint8_t, 256> bytes{};
    if (handle < 0 || scePadReadState(handle, bytes.data()) < 0) return state;
    state.buttons = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
        uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    state.lx = bytes[4]; state.ly = bytes[5]; state.rx = bytes[6]; state.ry = bytes[7];
    state.l2 = bytes[8]; state.r2 = bytes[9]; state.connected = bytes[76] != 0;
    return state;
  }
  int32_t user = 0xff;
 private:
  int32_t handle = -1;
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
  // Before the title has a second thread or holds a lock: the core's mutexes
  // spin before they block (see xenia/base/mutex.h).
  xe::ps5_spin_mutexes = true;
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
  xbox360ps5::Settings settings;
  settings.Load();
  settings.Apply();
  // Canary caches the vblank period at graphics setup. Apply only on startup;
  // the library saves changes and restarts rather than mutating a live flag.
  cvars::vsync = settings.vsync;
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
  xbox360ps5::gpu_diag::enabled = true;
  XELOGW("Performance: memory optimizations and pipeline-cache guards enabled; API wall time is not GPU utilization");
  XELOGW("Logging: {} (level {}), platform stdout disabled", settings.detailed_logs ? "detailed" : "normal", cvars::log_level);
  if (log_ready && log_root == storage / "LOGS")
    XELOGW("Installation folder is not writable; desktop log downloader exports sessions to /data/homebrew/PPSA50011/logs");
  std::filesystem::path game;
  if (argc > 1 && argv[1]) game = argv[1];
  else {
    std::ifstream input(storage / "game.txt");
    std::string line;
    if (std::getline(input, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); game = line; }
  }
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
  xbox360ps5::Launcher launcher(fonts, settings);
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
    auto status = emulator.Setup(&window, &drawer, true,
      [](xe::cpu::Processor* cpu) -> std::unique_ptr<xe::apu::AudioSystem> {
        return std::make_unique<xbox360ps5::CanaryAudioSystem>(cpu);
      },
      []() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
        return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
      },
      [&input](xe::ui::Window*) {
        std::vector<std::unique_ptr<xe::hid::InputDriver>> drivers;
        auto driver = std::make_unique<xbox360ps5::DualSenseInput>();
        input = driver.get(); drivers.push_back(std::move(driver)); return drivers;
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
      xbox360ps5::StartUiSounds();
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
      std::string profile_name;
      const auto refresh_name = [&profile_name, profiles, signed_in] {
        const auto& accounts = *profiles->GetAccounts();
        const auto found = accounts.find(signed_in());
        profile_name = found == accounts.end() ? std::string() : found->second.GetGamertagString();
      };
      refresh_name();
      XELOGW("Profile: {} of {} signed in", profile_name.empty() ? "none" : profile_name, profiles->GetAccountCount());
      xbox360ps5::ProfileHooks profile_hooks;
      profile_hooks.list = [profiles] {
        std::vector<xbox360ps5::ProfileEntry> entries;
        for (const auto& [xuid, account] : *profiles->GetAccounts())
          entries.push_back({account.GetGamertagString(), xuid, profiles->GetUserIndexAssignedToProfile(xuid) == 0});
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return entries;
      };
      profile_hooks.use = [sign_in, refresh_name](uint64_t xuid) {
        sign_in(xuid);
        SaveProfile(xuid);
        refresh_name();
        XELOGW("Profile: switched to {:016X}", xuid);
      };
      profile_hooks.create = [profiles, signed_in, refresh_name](const std::string& name) {
        if (!xe::kernel::xam::ProfileManager::IsGamertagValid(name)) return false;
        for (const auto& [xuid, account] : *profiles->GetAccounts())
          if (account.GetGamertagString() == name) return false;
        // The new profile signs in to the first free slot: free the first.
        const uint64_t before = signed_in();
        if (before) profiles->Logout(0, false);
        const bool created = profiles->CreateProfile(name, true);
        if (!signed_in() && before) profiles->Login(before, 0, false);
        if (created && signed_in()) SaveProfile(signed_in());
        refresh_name();
        XELOGW("Profile: {} {}", name, created ? "created" : "could not be created");
        return created;
      };
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
      while (!launched && !restart && !context.HasQuitFromUIThread()) {
        xbox360ps5::LauncherDialog* dialog = nullptr;
        if (game.empty()) {
          // The launcher: pick a game with the pad.
          Stage("BOOT launcher");
          launcher.Scan();
          dialog = new xbox360ps5::LauncherDialog(&drawer, launcher);
          uint32_t held = ~0u;  // Buttons already down when the screen opens do not count.
          auto repeat_at = std::chrono::steady_clock::now();
          while (!launcher.TakeLaunch(chosen) && !context.HasQuitFromUIThread()) {
            if (launcher.TakeRestart()) { restart = true; break; }
            const auto sample = pad.Read();
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
              {0x8000, Key::square}, {0x400, Key::l1}, {0x800, Key::r1}};
            for (const auto& [button, key] : keys) if (pressed & button) launcher.Press(key);
            // The interface's sounds: moving about, choosing, going back.
            xbox360ps5::MuteUiSounds(settings.mute);
            if (pressed & 0x2000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::back);
            else if (pressed & 0xD000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::select);
            else if (pressed & 0xCF0) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::move);
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
        new Guide(&drawer, guide, settings, profile_name, game_name, fps, fonts.f24, fonts.f28, fonts.f36);
        ApplyImageFilter(settings, emulator.graphics_system()->presenter());
        bool trigger_was = true, swallow = false, quit = false;
        uint32_t guide_held = ~0u;
        auto back_until = std::chrono::steady_clock::time_point::min();
        auto measure_at = std::chrono::steady_clock::time_point::max();
        bool auto_measured = false;
        int slow_summaries = 0;
        // The watch for a game that stops showing frames.
        const auto watch_started=std::chrono::steady_clock::now();
        xbox360ps5::FrameWatch frame_watch(emulator.graphics_system()->command_processor()->refreshed_output_count(),0);
        const int32_t usual_log_level = cvars::log_level;
        auto sample_at = std::chrono::steady_clock::now();
        uint64_t sampled_frames = emulator.graphics_system()->command_processor()->refreshed_output_count();
        uint64_t sampled_swaps = emulator.graphics_system()->command_processor()->swap_count();
        int samples = 0;
        float fps_sum = 0.0f, fps_low = 1e9f, swaps_sum = 0.0f;
        const auto game_started = std::chrono::steady_clock::now();
        double next_shot = 5;
        while (!context.HasQuitFromUIThread()) {
          auto sample = pad.Read();
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
            const uint32_t pressed = buttons & ~guide_held;
            guide_held = buttons;
            xbox360ps5::MuteUiSounds(settings.mute);
            if (pressed & 0x2000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::back);
            else if (pressed & 0x4000) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::select);
            else if (pressed & 0xCF0) xbox360ps5::PlayUiSound(xbox360ps5::UiSound::move);
            auto items = GuideItemsWithExperiments(guide.page, settings);
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
                case GuideAction::fps: settings.show_fps = !settings.show_fps; break;
                case GuideAction::sound: settings.mute = !settings.mute; break;
                case GuideAction::filter: settings.image_filter = (settings.image_filter + step + 3) % 3; break;
                case GuideAction::scale: settings.resolution_scale = (settings.resolution_scale - 1 + step + 3) % 3 + 1; break;
                case GuideAction::touchpad: settings.touchpad_menu = !settings.touchpad_menu; trigger_was = true; break;
                case GuideAction::measure:
                  guide.open = false;
                  changed = false;
                  measure_at = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                  break;
                case GuideAction::window: {
                  // 16 KiB, 64 KiB, the core's 256 KiB block, and round again.
                  static constexpr uint32_t kWindows[] = {0x4000, 0x10000, 0};
                  int index = 0;
                  for (int n = 0; n < 3; ++n) if (kWindows[n] == xbox360ps5::gpu_diag::invalidation_window) index = n;
                  const uint32_t chosen = kWindows[(index + step + 3) % 3];
                  xbox360ps5::gpu_diag::invalidation_window = chosen;
                  changed = false;
                  XELOGW("Performance window: {} selected in the Guide at {:.1f} FPS", WindowName(chosen), fps);
                  break;
                }
                case GuideAction::gpu_core: {
                  xbox360ps5::SetGpuCoreDedicated(!xbox360ps5::GpuCoreDedicated());
                  // Every guest and emulator thread moves to the new arrangement.
                  for (auto& thread : emulator.kernel_state()->object_table()->GetObjectsByType<xe::kernel::XThread>())
                    if (thread->thread()) xbox360ps5::PlaceThread(thread->thread()->native_handle());
                  xbox360ps5::PlaceCallingThread(false);
                  changed = false;
                  XELOGW("Performance gpu-core: {} in the Guide at {:.1f} FPS",
                         xbox360ps5::GpuCoreDedicated() ? "dedicated" : "shared", fps);
                  break;
                }
                case GuideAction::unwatched:
                  xbox360ps5::gpu_diag::unwatched_pages = !xbox360ps5::gpu_diag::unwatched_pages;
                  changed = false;
                  XELOGW("Performance unwatched: switched {} in the Guide at {:.1f} FPS",
                         xbox360ps5::gpu_diag::unwatched_pages ? "on" : "off", fps);
                  break;
                case GuideAction::lock_free:
                  xbox360ps5::gpu_diag::lock_free_valid = !xbox360ps5::gpu_diag::lock_free_valid;
                  changed = false;
                  XELOGW("Performance lock-free: switched {} in the Guide at {:.1f} FPS",
                         xbox360ps5::gpu_diag::lock_free_valid ? "on" : "off", fps);
                  break;
              }
              if (changed) {
                settings.Save();
                cvars::mute = settings.mute;
                ApplyImageFilter(settings, emulator.graphics_system()->presenter());
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
