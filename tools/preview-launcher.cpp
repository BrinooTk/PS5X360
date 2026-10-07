// SPDX-License-Identifier: MIT
// Software render of the launcher's actual ImGui draw list. Run inside a
// disposable container: /app0 contains generated visual fixtures, never ROMs.
#include "xbox360ps5/launcher.hpp"
#include "xenia/base/cvar.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"
DEFINE_int32(user_language, 1, "Preview language", "Preview");
DECLARE_bool(vsync);
DECLARE_int32(log_level);
DECLARE_bool(log_to_stdout);
namespace {
struct Texture : xe::ui::ImmediateTexture {
  std::vector<uint8_t> pixels;
  Texture(uint32_t w,uint32_t h,const uint8_t* data):ImmediateTexture(w,h),pixels(data,data+size_t(w)*h*4) {}
};
struct Drawer : xe::ui::ImmediateDrawer {
  std::unique_ptr<xe::ui::ImmediateTexture> CreateTexture(uint32_t w,uint32_t h,
      xe::ui::ImmediateTextureFilter,bool,const uint8_t* p) override {return std::make_unique<Texture>(w,h,p);}
  void BeginDrawBatch(const xe::ui::ImmediateDrawBatch&) override {}
  void Draw(const xe::ui::ImmediateDraw&) override {}
  void EndDrawBatch() override {}
};
float Edge(ImVec2 a,ImVec2 b,ImVec2 p) {return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
void Raster(const ImDrawData& data,const char* output) {
  constexpr int width=1920,height=1080;
  std::vector<uint8_t> image(size_t(width)*height*4,0);
  for(size_t i=3;i<image.size();i+=4) image[i]=255;
  for(int l=0;l<data.CmdListsCount;++l) {
    const ImDrawList& list=*data.CmdLists[l];
    for(const auto& command:list.CmdBuffer) {
      if(command.UserCallback) continue;
      const Texture* tex=reinterpret_cast<const Texture*>(command.TextureId);
      for(unsigned i=0;i+2<command.ElemCount;i+=3) {
        const ImDrawVert& a=list.VtxBuffer[list.IdxBuffer[command.IdxOffset+i]+command.VtxOffset];
        const ImDrawVert& b=list.VtxBuffer[list.IdxBuffer[command.IdxOffset+i+1]+command.VtxOffset];
        const ImDrawVert& c=list.VtxBuffer[list.IdxBuffer[command.IdxOffset+i+2]+command.VtxOffset];
        const float area=Edge(a.pos,b.pos,c.pos);
        if(std::fabs(area)<1e-6f) continue;
        const int x0=std::max(0,int(std::floor(std::max(command.ClipRect.x,std::min({a.pos.x,b.pos.x,c.pos.x})))));
        const int y0=std::max(0,int(std::floor(std::max(command.ClipRect.y,std::min({a.pos.y,b.pos.y,c.pos.y})))));
        const int x1=std::min(width,int(std::ceil(std::min(command.ClipRect.z,std::max({a.pos.x,b.pos.x,c.pos.x})))));
        const int y1=std::min(height,int(std::ceil(std::min(command.ClipRect.w,std::max({a.pos.y,b.pos.y,c.pos.y})))));
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
          const ImVec2 p(float(x)+.5f,float(y)+.5f);
          const float wa=Edge(b.pos,c.pos,p)/area,wb=Edge(c.pos,a.pos,p)/area,wc=1-wa-wb;
          if(wa<0 || wb<0 || wc<0) continue;
          float rgba[4];
          for(int ch=0;ch<4;++ch) rgba[ch]=(wa*((a.col>>(ch*8))&255)+wb*((b.col>>(ch*8))&255)+wc*((c.col>>(ch*8))&255))/255;
          if(tex) {
            const float u=wa*a.uv.x+wb*b.uv.x+wc*c.uv.x,v=wa*a.uv.y+wb*b.uv.y+wc*c.uv.y;
            const int tx=std::clamp(int(u*tex->width),0,int(tex->width)-1),ty=std::clamp(int(v*tex->height),0,int(tex->height)-1);
            for(int ch=0;ch<4;++ch) rgba[ch]*=tex->pixels[(size_t(ty)*tex->width+tx)*4+ch]/255.f;
          }
          const size_t at=(size_t(y)*width+x)*4;
          for(int ch=0;ch<3;++ch) image[at+ch]=uint8_t(std::clamp(rgba[ch]*rgba[3]*255+image[at+ch]*(1-rgba[3]),0.f,255.f));
        }
      }
    }
  }
  if(!stbi_write_png(output,width,height,4,image.data(),width*4)) throw std::runtime_error("PNG write failed");
}
}
int main(int argc,char** argv) {
  if(argc<2 || argc>3) return 2;
  namespace fs=std::filesystem;
  fs::create_directories("/app0/assets/roms");
  // Colourful grid inserts expose texture warping and seams on every face.
  for(int n=0;n<7;++n) {
    const fs::path game=fs::path("/app0/assets/roms")/("Visual sample "+std::to_string(n+1));
    fs::create_directories(game);
    std::ofstream(game/"default.xex").put(0);
    std::vector<uint8_t> insert(1000*700*4);
    for(int y=0;y<700;++y) for(int x=0;x<1000;++x) {
      const size_t at=(size_t(y)*1000+x)*4;
      const bool grid=x%50<3||y%50<3;
      insert[at]=uint8_t(grid?235:40+n*25);
      insert[at+1]=uint8_t(grid?235:60+(y*120/700));
      insert[at+2]=uint8_t(grid?235:180-n*15);
      insert[at+3]=255;
    }
    stbi_write_png((game/"cover.png").c_str(),1000,700,4,insert.data(),4000);
  }
  ImGui::CreateContext();
  ImGuiIO& io=ImGui::GetIO();io.IniFilename=nullptr;
  io.DisplaySize=ImVec2(1920,1080);io.DeltaTime=1.f/60;
  auto fonts=xbox360ps5::Launcher::LoadFonts(io);
  unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  Texture atlas(w,h,pixels);io.Fonts->TexID=reinterpret_cast<ImTextureID>(&atlas);
  {
    Drawer drawer;xbox360ps5::Settings settings;
    settings.game_paths = {"/app0/assets/roms", "/app0/assets/roms/Visual sample 1",
                           "/app0/assets/roms/Visual sample 2/../Visual sample 1/", "/mnt/offline/Games"};
    xbox360ps5::Launcher launcher(fonts,settings);launcher.SetDrawer(&drawer);launcher.Scan();launcher.SetPadConnected(true);
    if (launcher.GamePaths().size() != 7) throw std::runtime_error("Overlapping roots duplicated games");
    puts("PASS: seven different default.xex paths remain distinct; overlapping and normalized roots deduplicate");
    launcher.SetWebPage("http://192.168.0.19:8360/?k=1a2b3c", "http://192.168.0.19:8360/", "1a2b3c");
    for(int i=0;i<3;++i) launcher.Press(xbox360ps5::Key::right);
    uint32_t assigned_player = 99;
    if (argc == 3 && std::string(argv[2]) == "ui-volume") {
      fs::create_directories("/download0/xbox360ps5");
      std::ofstream("/download0/xbox360ps5/settings.txt") << "volume=3\nui_sounds=1\n";
      xbox360ps5::Settings legacy; legacy.Load();
      if (legacy.ui_sound_volume != 2 || legacy.volume != 3) throw std::runtime_error("Legacy settings must default interface volume to 20% and keep game volume");
      legacy.ui_sound_volume = 1; legacy.Save();
      xbox360ps5::Settings saved; saved.Load();
      if (saved.ui_sound_volume != 1 || saved.volume != 3) throw std::runtime_error("Independent UI volume must persist");
      settings = saved;
      launcher.Press(xbox360ps5::Key::square);
      for(int i=0;i<4;++i) launcher.Press(xbox360ps5::Key::down);
      launcher.Press(xbox360ps5::Key::cross);
      puts("PASS: legacy default 20%, independent UI volume persistence, game volume preserved");
    } else if (argc == 3 && std::string(argv[2]) == "profiles4") {
      xbox360ps5::ProfileHooks hooks;
      hooks.list = [] { return std::vector<xbox360ps5::ProfileEntry>{{"Player",1,true,0},{"Alex",2,false,1},{"Chris",3,false,2},{"Morgan",4,false,3},{"Guest",5,false,-1}}; };
      hooks.use_player = [&](uint64_t, uint32_t slot) { assigned_player = slot; return true; };
      launcher.SetProfiles(std::move(hooks));
      launcher.Press(xbox360ps5::Key::square); launcher.Press(xbox360ps5::Key::cross);
      launcher.Press(xbox360ps5::Key::r1); launcher.Press(xbox360ps5::Key::cross);
      if (assigned_player != 1) throw std::runtime_error("Profile selected for wrong player");
      puts("PASS: player selector routes profile association to player 2");
    } else if (argc == 3 && std::string(argv[2]) == "achievements") {
      const auto paths = launcher.GamePaths();
      for (const auto& path : paths) launcher.RecordLaunch({path.parent_path().filename().string(),path,"XEX",path.parent_path().string()},0x545407F2,path.parent_path().filename().string(),{},0);
      launcher.Scan();
      launcher.SetAchievements([](uint32_t, uint32_t slot) {
        return std::vector<xbox360ps5::AchievementEntry>{{"First steps","Complete the opening mission.",1,10,slot==0},{"Explorer","Visit all districts.",2,30,false},{"Finish line","Win your first race.",3,20,slot==0}};
      }, [](uint32_t slot){ return slot==0 ? "Player" : "Alex"; });
      launcher.Press(xbox360ps5::Key::cross);
      for(int i=0;i<3;++i) launcher.Press(xbox360ps5::Key::r1);
    } else if (argc == 3 && std::string(argv[2]) == "logging") {
      fs::create_directories("/download0/xbox360ps5");
      std::ofstream("/download0/xbox360ps5/settings.txt") << "detailed_logs=1\nvsync=0\n";
      xbox360ps5::Settings migrated; migrated.Load(); migrated.Apply();
      if (migrated.detailed_logs || migrated.vsync || cvars::log_level != 1 || cvars::log_to_stdout)
        throw std::runtime_error("Logging migration must disable traces and preserve VSync");
      migrated.detailed_logs = true; migrated.Save();
      xbox360ps5::Settings restored; restored.Load(); restored.Apply();
      if (!restored.detailed_logs || cvars::log_level != 3)
        throw std::runtime_error("Explicit detailed logging should persist after migration");
      std::ofstream("/app0/assets/debug.txt").put('1');
      restored.detailed_logs = false; restored.Save(); restored.Apply();
      if (cvars::log_level != 1)
        throw std::runtime_error("Debug sentinel must not override normal logging");
      puts("PASS: legacy traces reset once; explicit choices persist; VSync preserved; debug sentinel ignored");
    } else if (argc == 3 && std::string(argv[2]) == "vsync") {
      // VSync is the eighth option of the Video sub-menu, the third row of the settings.
      const auto to_vsync = [](xbox360ps5::Launcher& l) {
        l.Press(xbox360ps5::Key::square);
        for (int i=0;i<2;++i) l.Press(xbox360ps5::Key::down);
        l.Press(xbox360ps5::Key::cross);
        for (int i=0;i<7;++i) l.Press(xbox360ps5::Key::down);
      };
      fs::create_directories("/download0/xbox360ps5");
      cvars::vsync = true;
      launcher.SetStarted(settings);
      to_vsync(launcher);
      launcher.Press(xbox360ps5::Key::cross);
      launcher.Press(xbox360ps5::Key::circle);
      launcher.Press(xbox360ps5::Key::circle);
      if (settings.vsync || !launcher.TakeRestart() || !cvars::vsync)
        throw std::runtime_error("VSync toggle must request restart without changing the live flag");
      xbox360ps5::Settings restored; restored.Load();
      if (restored.vsync) throw std::runtime_error("VSync off was not persisted");
      xbox360ps5::Launcher cancel(fonts,settings);
      cancel.SetStarted(settings);
      cvars::vsync = false;
      to_vsync(cancel);
      cancel.Press(xbox360ps5::Key::cross);
      cancel.Press(xbox360ps5::Key::cross);
      cancel.Press(xbox360ps5::Key::circle);
      cancel.Press(xbox360ps5::Key::circle);
      if (cancel.TakeRestart() || settings.vsync)
        throw std::runtime_error("Reverting VSync should cancel the restart");
      puts("PASS: VSync off persists, restart requested, live flag unchanged, reverting cancels restart");
      to_vsync(launcher);
    } else if (argc == 3 && std::string(argv[2]) == "saves") {
      fs::create_directories("/data/homebrew/PPSA50011/saves/0009000000000001/454108CF");
      launcher.SetSaveRoot("/data/homebrew/PPSA50011/saves");
      launcher.Press(xbox360ps5::Key::square);
      for (int i=0;i<10;++i) launcher.Press(xbox360ps5::Key::down);
      launcher.Press(xbox360ps5::Key::cross);
    } else if (argc == 3 && std::string(argv[2]) == "game") {
      // A game the emulator has identified: its sheet's second page holds its own options.
      fs::create_directories("/download0/xbox360ps5");
      const auto paths = launcher.GamePaths();
      launcher.RecordLaunch({"Visual sample 1", paths[0], "XEX", paths[0].parent_path().string()}, 0x545407F2,
                            "Visual sample 1", {}, 0);
      launcher.Scan();
      for (int i=0;i<3;++i) launcher.Press(xbox360ps5::Key::left);  // Recently played comes first.
      launcher.Press(xbox360ps5::Key::triangle);        // Straight to the game's own settings.
      launcher.Press(xbox360ps5::Key::cross);           // Open Graphics.
      launcher.Press(xbox360ps5::Key::right);           // The image filter: this game's own.
      launcher.Press(xbox360ps5::Key::right);
      for (int i=0;i<5;++i) launcher.Press(xbox360ps5::Key::down);
      launcher.Press(xbox360ps5::Key::cross);           // And one more further down.
      const auto own = xbox360ps5::LoadGameOverrides("545407F2");
      if (own.size() != 2 || own.at("image_filter") != 1) throw std::runtime_error("A game's own options were not kept");
      xbox360ps5::Settings general;
      // (The second option changed above is now one of the preset's: checked with a map of its own below.)
      const auto in_force = xbox360ps5::ForGame(general, "545407F2", {{"image_filter", 1}});
      if (in_force.image_filter != 1 || general.image_filter != 0) throw std::runtime_error("Own options must not change the general ones");
      // The built-in preset of this title is in force without being asked for; other titles follow the general options.
      if (!in_force.fast_locks || !in_force.memory_boost || general.fast_locks || general.memory_boost ||
          xbox360ps5::ForGame(general, "4D5307E6", {}).fast_locks)
        throw std::runtime_error("A game's preset must apply to that game only");
      xbox360ps5::GameOverrides off = {{"fast_locks", 0}};
      if (xbox360ps5::ForGame(general, "545407F2", off).fast_locks) throw std::runtime_error("A game's own option must win over its preset");
      puts("PASS: a game's own options are kept apart, in its file, and laid over the general ones");
    } else if (argc == 3 && (std::string(argv[2]) == "play" || std::string(argv[2]) == "gameperf" ||
                             std::string(argv[2]) == "categories" || std::string(argv[2]) == "gamecontrols" ||
                             std::string(argv[2]) == "pergame" || std::string(argv[2]) == "pergame-open")) {
      // Two identified games: one with a built-in preset and a setting of its own, one with nothing.
      const std::string what = argv[2];
      fs::create_directories("/download0/xbox360ps5");
      const auto paths = launcher.GamePaths();
      launcher.RecordLaunch({"Visual sample 2", paths[1], "XEX", paths[1].parent_path().string()}, 0x4D5307E6,
                            "Visual sample 2", {}, 0);
      launcher.RecordLaunch({"Visual sample 1", paths[0], "XEX", paths[0].parent_path().string()}, 0x545407F2,
                            "Visual sample 1", {}, 0);
      xbox360ps5::SaveGameOverrides("545407F2", {{"image_filter", 2}, {"readback", 1}});
      launcher.Scan();
      for (int i=0;i<3;++i) launcher.Press(xbox360ps5::Key::left);
      xbox360ps5::GameEntry launched;
      if (what == "play") {
        // The shelf's X opens the sheet; it does not start the game. X on its first page does.
        launcher.Press(xbox360ps5::Key::cross);
        if (launcher.TakeLaunch(launched)) throw std::runtime_error("X on the shelf must open the game's sheet, not start it");
        xbox360ps5::Launcher second(fonts, settings); second.SetDrawer(&drawer); second.Scan();
        for (int i=0;i<3;++i) second.Press(xbox360ps5::Key::left);
        second.Press(xbox360ps5::Key::cross);
        second.Press(xbox360ps5::Key::cross);
        if (!second.TakeLaunch(launched) || launched.title_id != "545407F2")
          throw std::runtime_error("X on the Play page must start the selected game");
        second.SetDrawer(nullptr);
        puts("PASS: X opens the game's sheet; X on its Play page starts that game");
      } else if (what == "categories" || what == "gamecontrols") {
        launcher.Press(xbox360ps5::Key::triangle);
        if (what == "gamecontrols") {
          for (int i=0;i<3;++i) launcher.Press(xbox360ps5::Key::down);
          launcher.Press(xbox360ps5::Key::cross);
        }
      } else if (what == "gameperf") {
        launcher.Press(xbox360ps5::Key::triangle);
        launcher.Press(xbox360ps5::Key::down);       // Choose Performance.
        launcher.Press(xbox360ps5::Key::cross);      // Open its options.
        for (int i=0;i<5;++i) launcher.Press(xbox360ps5::Key::down);
      } else {
        launcher.Press(xbox360ps5::Key::square);
        launcher.Press(xbox360ps5::Key::r1);
        if (what == "pergame-open") {
          launcher.Press(xbox360ps5::Key::down);       // The second game in the list.
          launcher.Press(xbox360ps5::Key::cross);
          launcher.Press(xbox360ps5::Key::cross);      // Open Graphics.
          launcher.Press(xbox360ps5::Key::right);      // Its first video option becomes its own.
          launcher.Press(xbox360ps5::Key::right);
          const auto own = xbox360ps5::LoadGameOverrides("4D5307E6");
          if (own.size() != 1 || own.at("image_filter") != 1 || xbox360ps5::LoadGameOverrides("545407F2").size() != 2)
            throw std::runtime_error("The game opened from the settings' list must be the one that is changed");
          puts("PASS: a game opened from the settings' list keeps its own options, and only its own");
        }
      }
    } else if (argc == 3) {
      const std::string what = argv[2];
      launcher.Press(xbox360ps5::Key::square);
      // The sub-menus by the row that opens them.
      const std::pair<const char*, int> menus[] = {{"video", 2}, {"performance", 3}, {"audio", 4}, {"controls", 5},
                                                   {"system", 6}, {"paths", 8}, {"folders", 8}};
      for (const auto& [name, row] : menus) if (what == name) {
        for (int i=0;i<row;++i) launcher.Press(xbox360ps5::Key::down);
        launcher.Press(xbox360ps5::Key::cross);
        if (what == "folders") launcher.Press(xbox360ps5::Key::square);
        if (what == "performance") for (int i=0;i<2;++i) launcher.Press(xbox360ps5::Key::down);
      }
    }
    for(int i=0;i<180;++i) {ImGui::NewFrame();launcher.Draw(io);ImGui::Render();}
    Raster(*ImGui::GetDrawData(),argv[1]);
    launcher.SetDrawer(nullptr);
  }
  ImGui::DestroyContext();
}
