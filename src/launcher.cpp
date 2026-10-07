// SPDX-License-Identifier: MIT
#include "xbox360ps5/i18n.hpp"
#include "xbox360ps5/build_version.hpp"
#include "xbox360ps5/cover_geometry.hpp"
#include "xbox360ps5/launcher.hpp"
#include "xbox360ps5/canary_audio.hpp"
#include "xbox360ps5/gpu_diagnostics.hpp"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <chrono>
#include <functional>
#include <thread>
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
DECLARE_int32(anisotropic_override);
DECLARE_string(occlusion_query);
DECLARE_string(readback_resolve);
DECLARE_bool(readback_memexport);
DECLARE_bool(async_shader_compilation);
DECLARE_bool(clear_memory_page_state);
DECLARE_bool(delay_via_maybeyield);
DECLARE_int32(license_mask);
DECLARE_uint32(custom_internal_display_resolution_x);
DECLARE_uint32(custom_internal_display_resolution_y);
DECLARE_uint64(framerate_limit);
DECLARE_uint32(kernel_display_gamma_type);
DECLARE_bool(present_letterbox);
DECLARE_bool(depth_float24_convert_in_pixel_shader);
DECLARE_bool(depth_float24_round);
DECLARE_bool(depth_bias_shader_offset);
DECLARE_bool(mulsc_round_toward_zero);
DECLARE_int32(vulkan_pipeline_creation_threads);
DECLARE_bool(disable_context_promotion);
DECLARE_bool(use_fast_dot_product);
DECLARE_bool(protect_zero);
DECLARE_string(xma_decoder);
DECLARE_bool(use_dedicated_xma_thread);
// The core defines these three inside its input namespace.
namespace xe {
namespace hid {
DECLARE_bool(vibration);
DECLARE_double(left_stick_deadzone_percentage);
DECLARE_double(right_stick_deadzone_percentage);
}
}
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

const std::vector<Option>& Options() {
  using S = Settings;
  using W = OptionWhen;
  static const std::vector<const char*> off_on = {"Desligado", "Ligado"};
  static const std::vector<const char*> dead = {"0%", "5%", "10%", "15%", "20%", "25%"};
  static const std::vector<Option> options = {
      {"image_filter", &S::image_filter, 0, W::live, 0, true, "Filtro de imagem",
       "Como a imagem do jogo é ampliada até a tela. Simples: mais leve e fiel. CAS: deixa a imagem mais nítida. FSR: upscale da AMD, bordas mais limpas. Quase não pesa.",
       {"Simples", "CAS (nitidez)", "FSR (upscale)"}},
      {"sharpness", &S::sharpness, 0, W::live, 1, true, "Nitidez do filtro",
       "Força da nitidez do CAS e do FSR. Não muda nada com o filtro Simples.", {"Suave", "Padrão", "Forte"}},
      {"dither", &S::dither, 0, W::live, 0, false, "Suavizar degradês",
       "Mistura as cores na saída para o céu e as sombras não formarem faixas.", off_on},
      {"stretch", &S::stretch, 0, W::live, 0, true, "Proporção da imagem",
       "Manter: a imagem do jogo guarda a proporção original, com barras se preciso. Esticar: preenche a tela inteira, deformando jogos que não são 16:9.",
       {"Manter", "Esticar na tela"}},
      {"anisotropic", &S::anisotropic, 0, W::live, 0, true, "Filtro anisotrópico",
       "Deixa as texturas mais nítidas no chão e nas paredes vistas de lado. Do jogo: usa o que o jogo pede. Valores maiores quase não pesam, mas raros jogos mostram defeitos.",
       {"Do jogo", "2x", "4x", "8x", "16x"}},
      {"internal_resolution", &S::internal_resolution, 0, W::launch, 0, true, "Resolução do console emulado",
       "A resolução que o Xbox 360 emulado informa ao jogo. Em 480p ou 540p alguns jogos desenham menos pontos e ficam mais leves; em 1080p alguns desenham mais (mais pesado). Muitos jogos ignoram e continuam em 720p.",
       {"720p", "1080p", "480p (848x480)", "540p (960x540)"}},
      {"video_clock", &S::video_clock, 0, W::live, 0, true, "Ritmo do vídeo emulado",
       "Quantas vezes por segundo o console emulado avisa o jogo de um novo quadro. Em 120 Hz alguns jogos presos em 30 quadros passam a 60, mas outros ficam acelerados. É um teste por jogo; não garante 60 quadros.",
       {"60 Hz", "120 Hz (teste)"}},
      {"vsync", &S::vsync, 0, W::start, 1, true, "VSync",
       "Controla a sincronização vertical emulada. O ritmo automático permanece em 60 Hz mesmo quando desligado, para evitar acelerar o jogo.", off_on},
      {"gamma", &S::gamma, 0, W::launch, 0, true, "Gama do console emulado",
       "O tipo de tela que o console emulado informa ao jogo. Alguns jogos ficam com as sombras mais claras ou mais escuras conforme a escolha. TV HD é o padrão do Xbox 360.",
       {"TV HD", "sRGB", "Linear"}},
      {"depth_precision", &S::depth_precision, 0, W::start, 0, true, "Precisão da profundidade",
       "Como a profundidade de 24 bits do Xbox 360 é reproduzida. Rápida serve à maioria. Exata corrige objetos distantes piscando ou atravessando outros em alguns jogos, mas pesa mais.",
       {"Rápida", "Exata", "Exata e arredondada"}},
      {"decal_bias", &S::decal_bias, 0, W::launch, 0, true, "Correção de decalques",
       "Marcas no chão e nas paredes (sangue, faixas, sombras) que piscam em alguns jogos. Ligado, o deslocamento dessas marcas é calculado no shader.", off_on},
      {"mulsc", &S::mulsc, 0, W::launch, 0, true, "Arredondamento de geometria",
       "Uma conta dos shaders arredonda para zero. Corrige geometria deformada em jogos da Volition (Saints Row, Red Faction) e talvez outros. Deixe desligado nos demais.", off_on},
      {"fast_locks", &S::fast_locks, 1, W::start, 0, true, "Travas rápidas",
       "As travas internas do emulador giram um instante antes de dormir (novidade da 0.5.6). No GTA IV rendeu bem mais quadros por segundo. Se um jogo que funcionava parou de abrir ou trava, deixe desligado: é o comportamento da 0.5.5.",
       off_on},
      {"memory_boost", &S::memory_boost, 1, W::start, 0, true, "Memória de vídeo otimizada",
       "Menos cópias e menos avisos de escrita entre o processador e o vídeo (novidade da 0.5.6). Ajuda jogos pesados. Se um jogo que funcionava parou de abrir ou mostra defeitos, deixe desligado: é o comportamento da 0.5.5. As duas opções seguintes só valem com esta ligada.",
       off_on},
      {"dynamic_buffers", &S::dynamic_buffers, 1, W::live, 1, true, "Otimizar buffers dinâmicos",
       "Compara pelo conteúdo os dados que o jogo reescreve o tempo todo, em vez de vigiar a memória. Reduz trabalho do processador. Desligue só para comparar, se um jogo mostrar objetos piscando.", off_on},
      {"memory_window", &S::memory_window, 1, W::live, 0, true, "Atualização de memória",
       "Quanto de memória é reenviado ao vídeo quando o jogo escreve nela. 16 KiB reenviou menos nos testes; valores maiores trocam avisos de escrita por mais cópia.",
       {"16 KiB", "64 KiB", "256 KiB"}},
      {"occlusion", &S::occlusion, 1, W::start, 0, true, "Consultas de visibilidade",
       "Como o emulador responde quando o jogo pergunta se um objeto está visível (brilho do sol, objetos escondidos). Rápida: pergunta ao vídeo sem esperar. Falsa: responde sem perguntar, mais leve, alguns efeitos podem errar. Precisa: espera a resposta, mais lenta.",
       {"Rápida", "Falsa (mais leve)", "Rápida alternativa", "Precisa (mais lenta)"}},
      {"readback", &S::readback, 1, W::live, 0, true, "Leitura da imagem pelo jogo",
       "Alguns jogos leem de volta a imagem desenhada (foto do save, alguns efeitos). Desligada é o mais rápido. Use Rápida ou Completa só se um jogo mostrar imagens pretas ou efeitos faltando.",
       {"Desligada", "Rápida", "Completa (lenta)"}},
      {"memexport", &S::memexport, 1, W::live, 0, true, "Leitura de dados gerados no vídeo",
       "Poucos jogos leem no processador dados que o vídeo grava. Deixa o jogo mais lento; ligue só se faltarem personagens ou partículas.", off_on},
      {"async_shaders", &S::async_shaders, 1, W::live, 1, true, "Shaders em segundo plano",
       "Ligado: o jogo não trava enquanto um efeito novo é preparado, mas o efeito pode aparecer um instante depois. Desligado: espera cada efeito ficar pronto, com travadinhas na primeira vez.", off_on},
      {"clear_pages", &S::clear_pages, 1, W::live, 0, true, "Reler memória escrita pelo vídeo",
       "Correção do Xenia para jogos da Team Ninja em que personagens somem. Mais lento; deixe desligado nos outros jogos.", off_on},
      {"released_memory", &S::released_memory, 1, W::live, 0, true, "Desenhar com memória já liberada",
       "Alguns jogos mandam desenhar com dados de memória que já devolveram (o Left 4 Dead 2 faz isso várias vezes por segundo). Desligado, esses desenhos são descartados e pode faltar algo na tela. Ligado, são feitos enquanto a memória ainda puder ser lida.",
       off_on},
      {"guest_yield", &S::guest_yield, 1, W::launch, 0, true, "Ceder o processador nas esperas",
       "Quando o jogo fica girando à espera de outra tarefa, cede a vez no processador. Pode ajudar ou atrapalhar conforme o jogo.", off_on},
      {"threaded_driver", &S::threaded_driver, 1, W::start, 0, false, "Driver de vídeo em segunda thread (teste)",
       "O driver de vídeo grava os comandos em outra thread. É um teste: pode ajudar em jogos com muitos objetos na tela ou não mudar nada. Se algum jogo travar, desligue.", off_on},
      {"pipeline_threads", &S::pipeline_threads, 1, W::start, 0, true, "Threads de preparo de shaders",
       "Quantas threads preparam os efeitos em segundo plano. Automático usa a maior parte dos núcleos: prepara mais rápido, mas disputa o processador com o jogo. Menos threads podem dar um jogo mais estável enquanto os efeitos são preparados.",
       {"Automático", "2", "4", "6"}},
      {"fast_dot", &S::fast_dot, 1, W::launch, 0, true, "Produto escalar rápido (teste)",
       "Uma conta muito usada pelos jogos é feita de um jeito mais leve e menos exato. Pode dar alguns quadros a mais; se a física ou a geometria ficarem estranhas, desligue.", off_on},
      {"no_promotion", &S::no_promotion, 1, W::launch, 0, true, "Recompilador conservador",
       "Desliga uma otimização do recompilador (context promotion). Alguns jogos de esporte só funcionam certo assim. Deixa o jogo mais lento; use só se ele travar ou se comportar errado.", off_on},
      {"zero_page", &S::zero_page, 1, W::start, 0, true, "Permitir acesso ao endereço zero",
       "Alguns jogos leem ou escrevem no endereço zero da memória por engano e fecham sozinhos. Ligado, esse acesso é tolerado.", off_on},
      {"mute", &S::mute, 2, W::live, 0, false, "Som", "Silencia a saída de áudio dos jogos.", {"Ligado", "Mudo"}},
      {"volume", &S::volume, 2, W::live, 3, true, "Volume dos jogos",
       "Volume do som dos jogos. O volume da TV continua valendo por cima.", {"25%", "50%", "75%", "100%"}},
      {"ui_sounds", &S::ui_sounds, 2, W::live, 1, false, "Sons da interface",
       "Os sons ao mover, escolher e voltar nos menus do emulador.", {"Desligados", "Ligados"}},
      {"ui_sound_volume", &S::ui_sound_volume, 2, W::live, 2, false, "Volume da interface",
       "Volume dos efeitos de navegação dos menus, independente do som dos jogos.", {"0%", "10%", "20%", "35%", "50%", "100%"}},
      {"xma_decoder", &S::xma_decoder, 2, W::start, 0, true, "Decodificador de áudio",
       "Qual versão do decodificador de som do Xbox 360 (XMA) é usada. Troque se um jogo ficar sem som, com som picotado ou travar em vídeos.",
       {"Novo", "Antigo", "Original do Xenia"}},
      {"xma_inline", &S::xma_inline, 2, W::start, 0, true, "Áudio sem thread própria",
       "O som é decodificado no ritmo do próprio jogo, sem uma thread separada. Pode corrigir som fora de sincronia; pesa um pouco mais.", off_on},
      {"touchpad_menu", &S::touchpad_menu, 3, W::live, 1, false, "Clique do touchpad",
       "Abre o guia: o touchpad chama o guia do emulador durante o jogo, e o botão Back do Xbox fica dentro do guia. Botão Back: o touchpad é o Back do Xbox, e o guia abre com OPTIONS + touchpad.",
       {"Botão Back", "Abre o guia"}},
      {"vibration", &S::vibration, 3, W::live, 1, true, "Vibração",
       "A vibração que os jogos pedem vai para o controle.", {"Desligada", "Ligada"}},
      {"deadzone_left", &S::deadzone_left, 3, W::live, 0, true, "Zona morta do analógico esquerdo",
       "Ignora movimentos pequenos do analógico. Aumente se o personagem anda sozinho.", dead},
      {"deadzone_right", &S::deadzone_right, 3, W::live, 0, true, "Zona morta do analógico direito",
       "Ignora movimentos pequenos do analógico. Aumente se a câmera gira sozinha.", dead},
      {"motion_phone", &S::motion_phone, 3, W::live, 0, true, "Movimento por câmera (pesquisa)",
       "Recebe articulações pela rede local e mostra um diagnóstico. Ainda não fornece um sensor Kinect aos jogos. Requer a página de configurações ligada.", off_on},
      {"show_fps", &S::show_fps, 4, W::live, 0, false, "Mostrar FPS no jogo",
       "Mostra no canto da tela quantos quadros por segundo o jogo entrega.", off_on},
      {"achievement_toasts", &S::achievement_toasts, 4, W::live, 1, false, "Avisos de conquista",
       "Mostra uma notificação do PS5 quando um jogo libera uma conquista do Xbox 360.", {"Desligados", "Ligados"}},
      {"web_page", &S::web_page, 4, W::start, 1, false, "Página de configurações pelo celular",
       "Deixa um celular ou PC da mesma rede abrir a página de configurações do emulador e baixar os registros. Só funciona com o código mostrado na TV.",
       {"Desligada", "Ligada"}},
      {"arcade_full", &S::arcade_full, 4, W::launch, 0, true, "Jogos Arcade",
       "Jogos do Xbox Live Arcade abrem como demonstração ou como versão completa. Use a versão completa com os jogos que você comprou.",
       {"Demonstração", "Versão completa"}},
      {"detailed_logs", &S::detailed_logs, 4, W::live, 0, false, "Registros detalhados",
       "Grava cada chamada do jogo ao sistema no log. Deixa os jogos mais lentos; use só para investigar um problema.",
       {"Desligados", "Ligados"}},
  };
  return options;
}
namespace {
// "key=value" lines; anything else is skipped.
std::map<std::string, int> ReadValues(const fs::path& path, const std::string& section = {}) {
  std::map<std::string, int> values;
  std::ifstream input(path);
  std::string line;
  bool inside = section.empty();
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line.front() == '[') { inside = !section.empty() && Lower(line) == "[" + Lower(section) + "]"; continue; }
    const size_t split = line.find('=');
    if (!inside || split == std::string::npos || line.front() == '#') continue;
    values[line.substr(0, split)] = std::atoi(line.c_str() + split + 1);
  }
  return values;
}
const Option* FindOption(const std::string& key) {
  for (const auto& option : Options()) if (key == option.key) return &option;
  return nullptr;
}
// Keeps what the table knows, inside its range.
GameOverrides Checked(const std::map<std::string, int>& values) {
  GameOverrides overrides;
  for (const auto& [key, value] : values) {
    const Option* option = FindOption(key);
    if (option && option->per_game && value >= 0 && value < int(option->choices.size())) overrides[key] = value;
  }
  return overrides;
}
}
GameOverrides GamePreset(const std::string& title_id) {
  if (title_id.empty()) return {};
  // Confirmed on the development console. Grand Theft Auto IV: about 18 frames
  // a second without the two, 26 to 40 with them (v0.5.6-experimental.11/12).
  static const std::pair<const char*, GameOverrides> built_in[] = {
      {"545407F2", {{"fast_locks", 1}, {"memory_boost", 1}}},
      // Left 4 Dead 2: 546 draws refused in two minutes for one released page
      // (v0.5.7-experimental.7, development console).
      // and its text is drawn by the GPU and read back by the game: without
      // the readback every glyph of the menu is garbage (user's screenshot).
      // Its match loads with the alternative fast visibility queries only: of
      // the seventeen sessions kept on the development console, the one that
      // ran a match (nine minutes at 30 frames a second, experimental.10) had
      // them; the others stayed on the loading screen at 16 frames a second or
      // stopped. The core's own note on that mode names this title.
      {"454108D4", {{"released_memory", 1}, {"readback", 2}, {"occlusion", 2}}},
  };
  GameOverrides preset;
  for (const auto& [id, values] : built_in) if (Lower(title_id) == Lower(id)) preset = values;
  for (const auto& [key, value] : Checked(ReadValues("/app0/assets/presets.txt", title_id))) preset[key] = value;
  return preset;
}
Settings ForGame(const Settings& general, const std::string& title_id, const GameOverrides& own) {
  return WithOverrides(WithOverrides(general, GamePreset(title_id)), own);
}
GameOverrides LoadGameOverrides(const std::string& title_id) {
  return title_id.empty() ? GameOverrides() : Checked(ReadValues(kStorage / "game-settings" / (title_id + ".txt")));
}
bool SaveGameOverrides(const std::string& title_id, const GameOverrides& overrides) {
  if (title_id.empty()) return false;
  std::error_code error;
  const fs::path folder = kStorage / "game-settings", path = folder / (title_id + ".txt");
  if (overrides.empty()) { fs::remove(path, error); return true; }
  fs::create_directories(folder, error);
  std::ofstream output(path, std::ios::trunc);
  for (const auto& [key, value] : overrides) output << key << "=" << value << "\n";
  output.flush();
  return output.good();
}
Settings WithOverrides(Settings settings, const GameOverrides& overrides) {
  for (const auto& [key, value] : Checked(overrides)) settings.*(FindOption(key)->field) = value;
  return settings;
}
bool StartOptionsDiffer(const Settings& a, const Settings& b) {
  for (const auto& option : Options())
    if (option.when == OptionWhen::start && a.*option.field != b.*option.field) return true;
  return false;
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
  int logging_policy = 0;
  for (const auto& [key, value] : ReadValues(kStorage / "settings.txt")) {
    if (key == "language") language = value;
    else if (key == "interface_language") interface_language = value;
    else if (key == "logging_policy") logging_policy = value;
    // resolution_scale: not read. Scaled rendering takes the PS5's memory away
    // from the game's threads (every game crashed at its first frame).
    else if (const Option* option = FindOption(key))
      this->*option->field = std::clamp(value, 0, int(option->choices.size()) - 1);
  }
  if (language < 0 || language > 17) language = 0;
  if (interface_language < 0 || interface_language > 17) interface_language = 0;
  ui_language.store(SupportedUiLanguage(interface_language ? interface_language : console_language));
  // One-time migration: previous builds could enable continuous tracing via
  // a debug sentinel. Start with normal logging; explicit later choices persist.
  if (logging_policy < 1) { detailed_logs = 0; Save(); }
}
void Settings::Save() const {
  std::ofstream output(kStorage / "settings.txt", std::ios::trunc);
  output << "language=" << language << "\ninterface_language=" << interface_language
         << "\nlogging_policy=1\nresolution_scale=" << resolution_scale << "\n";
  for (const auto& option : Options()) output << option.key << "=" << this->*option.field << "\n";
}
void Settings::Apply() const {
  cvars::gpu_allow_invalid_fetch_constants = true;
  ui_language.store(SupportedUiLanguage(interface_language ? interface_language : console_language));
  cvars::user_language = GameLanguage();
  cvars::mute = mute != 0;
  cvars::draw_resolution_scale_x = cvars::draw_resolution_scale_y = resolution_scale;
  // Normal gameplay records warnings/errors plus explicit session metadata.
  // Do not duplicate every line into the platform stdout transport, and do
  // not let stale debug files silently override the user's visible setting.
  cvars::log_to_stdout = false;
  cvars::log_level = detailed_logs ? 3 : 1;
  // Read by the core each time they matter, so they also apply to a running game.
  cvars::anisotropic_override = anisotropic ? anisotropic + 1 : -1;  // 2x is 2 ... 16x is 5.
  static const char* const kOcclusion[] = {"fast", "fake", "fast-alt", "strict"};
  if (cvars::occlusion_query != kOcclusion[occlusion & 3]) cvars::occlusion_query = kOcclusion[occlusion & 3];
  static const char* const kReadback[] = {"none", "fast", "full"};
  if (cvars::readback_resolve != kReadback[readback % 3]) cvars::readback_resolve = kReadback[readback % 3];
  cvars::readback_memexport = memexport != 0;
  cvars::async_shader_compilation = async_shaders != 0;
  cvars::clear_memory_page_state = clear_pages != 0;
  gpu_diag::read_released_pages = released_memory != 0;
  xe::hid::cvars::vibration = vibration != 0;
  xe::hid::cvars::left_stick_deadzone_percentage = deadzone_left * 0.05;
  xe::hid::cvars::right_stick_deadzone_percentage = deadzone_right * 0.05;
  gpu_diag::unwatched_pages = dynamic_buffers != 0;
  static constexpr uint32_t kWindows[] = {0x4000, 0x10000, 0};
  gpu_diag::invalidation_window = kWindows[memory_window % 3];
  audio_volume = float(volume + 1) * 0.25f;
  // Read when a game starts.
  cvars::delay_via_maybeyield = guest_yield != 0;
  cvars::license_mask = arcade_full ? 1 : 0;
  static constexpr uint32_t kResolutions[][2] = {{0, 0}, {1920, 1080}, {848, 480}, {960, 540}};
  cvars::custom_internal_display_resolution_x = kResolutions[internal_resolution & 3][0];
  cvars::custom_internal_display_resolution_y = kResolutions[internal_resolution & 3][1];
  static constexpr uint32_t kGamma[] = {2, 1, 0};
  cvars::kernel_display_gamma_type = kGamma[gamma % 3];
  cvars::depth_bias_shader_offset = decal_bias != 0;
  cvars::mulsc_round_toward_zero = mulsc != 0;
  cvars::use_fast_dot_product = fast_dot != 0;
  cvars::disable_context_promotion = no_promotion != 0;
  // Read each time they matter.
  cvars::framerate_limit = video_clock ? 120 : 60;
  cvars::present_letterbox = stretch == 0;
  // Read as the emulator starts.
  cvars::depth_float24_convert_in_pixel_shader = depth_precision != 0;
  cvars::depth_float24_round = depth_precision == 2;
  static constexpr int32_t kPipelineThreads[] = {-1, 2, 4, 6};
  cvars::vulkan_pipeline_creation_threads = kPipelineThreads[pipeline_threads & 3];
  cvars::protect_zero = zero_page == 0;
  static const char* const kXma[] = {"new", "old", "master"};
  if (cvars::xma_decoder != kXma[xma_decoder % 3]) cvars::xma_decoder = kXma[xma_decoder % 3];
  cvars::use_dedicated_xma_thread = xma_inline == 0;
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

uint64_t LibrarySignature(const std::vector<std::string>& roots) {
  uint64_t hash = 1469598103934665603ull;
  const auto mix = [&hash](const std::string& text, uint64_t number) {
    for (unsigned char ch : text) { hash ^= ch; hash *= 1099511628211ull; }
    for (int n = 0; n < 8; ++n) { hash ^= uint8_t(number >> (n * 8)); hash *= 1099511628211ull; }
  };
  std::set<std::string> seen_roots;
  // The same walk as Launcher::Scan: a folder with one executable is a game
  // and is not looked into; images and packages count with their size, which
  // keeps changing while one is being copied.
  const std::function<void(const fs::path&, int, const fs::path&)> look =
      [&](const fs::path& directory, int depth, const fs::path& root) {
    std::error_code error;
    std::vector<fs::directory_entry> entries;
    for (fs::directory_iterator at(directory, error), end; !error && at != end; at.increment(error)) entries.push_back(*at);
    // The order a folder is read in is not always the same.
    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path() < b.path(); });
    int executables = 0;
    for (const auto& entry : entries) {
      if (Lower(entry.path().extension().string()) != ".xex" || entry.is_directory(error)) continue;
      ++executables;
      if (Lower(entry.path().filename().string()) == "default.xex") { executables = 1; break; }
    }
    if (executables == 1) {
      // How many things the game's folder holds: it grows during a copy.
      mix(directory.generic_string(), entries.size());
      if (directory != root) return;
    }
    for (const auto& entry : entries) {
      const fs::path& path = entry.path();
      const std::string extension = Lower(path.extension().string());
      if (entry.is_directory(error)) {
        if (depth < 6 && extension != ".data") look(path, depth + 1, root);
      } else if (extension == ".iso" || extension.empty() || extension == ".xbla" || extension == ".god") {
        mix(path.generic_string(), uint64_t(entry.file_size(error)));
      }
    }
  };
  for (const auto& configured : roots) {
    const fs::path root(configured);
    std::error_code error;
    const auto identity = NormalizeGamePath(root.generic_string());
    if (identity.empty() || !seen_roots.insert(identity).second) continue;
    const bool visible = fs::is_directory(root, error);
    mix(identity, visible ? 1 : 0);
    if (visible) look(root, 0, root);
  }
  return hash;
}

namespace {
long long SteadyMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
LibraryWatch::~LibraryWatch() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}
void LibraryWatch::Known(std::vector<std::string> roots, uint64_t signature) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    roots_ = std::move(roots);
    known_ = signature;
  }
  changed_ = false;
  if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
}
void LibraryWatch::Shown() { shown_at_ = SteadyMilliseconds(); }
void LibraryWatch::Run() {
  uint64_t last = 0;
  bool looked = false;
  while (!stop_) {
    // A look every four seconds, in short naps so that closing does not wait.
    for (int nap = 0; nap < 40 && !stop_; ++nap) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (stop_) break;
    if (SteadyMilliseconds() - shown_at_.load() > 1500) { looked = false; continue; }
    std::vector<std::string> roots;
    uint64_t known = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      roots = roots_;
      known = known_;
    }
    const uint64_t now = LibrarySignature(roots);
    if (now != known && looked && now == last) changed_ = true;
    last = now;
    looked = true;
  }
}

void Launcher::Scan() {
  // The game that is selected stays selected when the list is rebuilt. It is
  // found again by its path: the old positions mean nothing in the new list.
  const GameEntry* shown = Selected();
  const std::string keep = shown ? shown->path.string() : std::string();
  view_.clear();
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
  for (int n = 0; n < int(view_.size()); ++n) if (games_[size_t(view_[size_t(n)])].path.string() == keep) selected_ = n;
  scroll_ = float(selected_);
  SelectionChanged();
  if (mode_ == Mode::settings && settings_tab_ == 1) RefreshPerGame();
  watch_.Known(settings_.game_paths, LibrarySignature(settings_.game_paths));
  XELOGW("LAUNCHER found {} games", games_.size());
}

void Launcher::RefreshLibrary(bool automatic) {
  std::set<std::string> before;
  for (const auto& game : games_) before.insert(game.path.string());
  Scan();
  int added = 0;
  std::string first;
  for (const auto& game : games_) {
    if (before.count(game.path.string())) continue;
    if (!added++) first = game.name;
  }
  XELOGW("LAUNCHER list rebuilt ({}): {} games, {} new", automatic ? "the folders changed" : "asked for", games_.size(), added);
  scan_result_ = std::to_string(games_.size()) + " " + Tr(games_.size() == 1 ? "jogo" : "jogos");
  if (added) scan_result_ += ", " + std::to_string(added) + " " + Tr(added == 1 ? "novo" : "novos");
  notice_ = added == 1 ? Tr("Novo jogo encontrado: ") + first
            : added ? Tr("Novos jogos encontrados: ") + std::to_string(added)
                    : std::string(Tr("Lista de jogos atualizada")) + ": " + scan_result_;
  notice_until_ = time_ + 8.0f;
  // A new game gets its cover fetched like the ones found at the start.
  covers_requested_ = false;
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
void Launcher::StartCoverDownload(bool automatic) {
  std::vector<std::string> ids;
  std::error_code error;
  for (const auto& game : games_) {
    if (game.title_id.empty() || game.title_id == "00000000" || fs::exists(CoverFile(game.title_id), error) ||
        fs::exists("/app0/assets/covers/" + game.title_id + ".jpg", error)) continue;
    const fs::path beside = game.kind == "XEX" ? game.path.parent_path() / "cover" : fs::path(game.path).replace_extension();
    if (fs::exists(beside.string() + ".jpg", error) || fs::exists(beside.string() + ".png", error)) continue;
    if (std::find(ids.begin(), ids.end(), game.title_id) == ids.end()) ids.push_back(game.title_id);
  }
  if (automatic && ids.empty()) return;
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
  overrides_.clear();
  if (!Selected()) return;
  const GameEntry& game = *Selected();
  if (game.title_id.empty()) return;
  overrides_ = LoadGameOverrides(game.title_id);
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

void Launcher::ChangeOption(const Option& option, int step) {
  const int count = int(option.choices.size());
  int& value = settings_.*option.field;
  value = (value + step + count) % count;
  settings_.Save();
  settings_.Apply();
}
void Launcher::ChangeGameOption(const Option& option, int step) {
  // Round through "as the general option" and then each choice.
  const int count = int(option.choices.size());
  const auto found = overrides_.find(option.key);
  int value = (found == overrides_.end() ? -1 : found->second) + step;
  if (value >= count) value = -1;
  else if (value < -1) value = count - 1;
  if (value < 0) overrides_.erase(option.key);
  else overrides_[option.key] = value;
  if (Selected()) SaveGameOverrides(Selected()->title_id, overrides_);
}

void Launcher::SetWebPage(const std::string& url, const std::string& plain, const std::string& key) {
  if (url == web_url_) return;
  web_url_ = url;
  web_plain_ = plain;
  web_key_ = key;
  web_qr_ = url.empty() ? QrCode() : MakeQrCode(url);
}
std::vector<std::pair<std::string, std::string>> Launcher::Games() const {
  std::vector<std::pair<std::string, std::string>> games;
  for (const auto& game : games_) {
    if (game.title_id.empty()) continue;
    bool listed = false;
    for (const auto& other : games) listed = listed || other.first == game.title_id;
    if (!listed) games.emplace_back(game.title_id, game.name);
  }
  return games;
}

bool Launcher::TakeLaunch(GameEntry& game) {
  if (!launch_pending_) return false;
  launch_pending_ = false;
  game = launch_;
  return true;
}

void Launcher::OpenGameSheet(int tab) {
  if (!Selected()) return;
  SelectionChanged();
  game_tab_ = tab;
  game_option_row_ = game_category_ = 0;
  game_category_open_ = false;
  achievement_row_ = 0; RefreshAchievements();
  // The subjects a game has options of its own in.
  game_categories_.clear();
  for (int category = 0; category < int(std::size(kOptionCategories)); ++category)
    for (const auto& option : Options())
      if (option.per_game && option.category == category) { game_categories_.push_back(category); break; }
  FillGameRows();
  game_return_ = Mode::shelf;
  mode_ = Mode::game;
  sheet_ = 0.0f;
}
void Launcher::FillGameRows() {
  game_rows_.clear();
  if (game_categories_.empty()) return;
  const int category = game_categories_[size_t(game_category_) % game_categories_.size()];
  for (int n = 0; n < int(Options().size()); ++n)
    if (Options()[size_t(n)].per_game && Options()[size_t(n)].category == category) game_rows_.push_back(n);
}
// One entry per identified title, in the shelf's order.
void Launcher::RefreshPerGame() {
  per_game_.clear();
  for (int n = 0; n < int(games_.size()); ++n) {
    const std::string& id = games_[size_t(n)].title_id;
    if (id.empty() || id == "00000000") continue;
    bool listed = false;
    for (int other : per_game_) listed = listed || games_[size_t(other)].title_id == id;
    if (!listed) per_game_.push_back(n);
  }
  per_game_row_ = std::clamp(per_game_row_, 0, std::max(0, int(per_game_.size()) - 1));
}
void Launcher::CloseSettings() {
  if (settings_changed_) { settings_.Save(); settings_.Apply(); settings_changed_ = false; }
  // An option that is only read as the emulator starts: start again.
  if (StartOptionsDiffer(settings_, started_)) restart_ = true;
  restored_ = false;
  mode_ = Mode::shelf;
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
      // A game opens its sheet first: play, its patches, its own settings.
      if ((key == Key::cross || key == Key::down) && count) OpenGameSheet(0);
      if (key == Key::triangle && count) OpenGameSheet(2);
      if (key == Key::square) { mode_ = Mode::settings; sheet_ = 0.0f; settings_row_ = 0; settings_tab_ = 0; }
      if (key == Key::options) RefreshLibrary(false);
      break;
    }
    case Mode::game: {
      if (key == Key::l1 || key == Key::r1) { game_tab_ = (game_tab_ + (key == Key::r1 ? 1 : 3)) % 4; if (game_tab_ == 3) RefreshAchievements(); break; }
      // Closing goes back to where the sheet was opened from.
      const auto close = [this] { mode_ = game_return_; game_return_ = Mode::shelf; if (mode_ == Mode::settings) RefreshPerGame(); };
      if (game_tab_ == 0) {
        if (key == Key::cross && Selected()) {
          launch_ = *Selected(); launch_pending_ = true; loading_ = launch_.name;
          mode_ = Mode::shelf; game_return_ = Mode::shelf;
        }
        if (key == Key::right) game_tab_ = 1;
        if (key == Key::circle || key == Key::triangle) close();
        break;
      }
      if (game_tab_ == 3) {
        const int rows = int(achievements_.size());
        if (key == Key::up && rows) achievement_row_ = (achievement_row_ + rows - 1) % rows;
        if (key == Key::down && rows) achievement_row_ = (achievement_row_ + 1) % rows;
        if (key == Key::triangle) { achievement_player_ = (achievement_player_ + 1) % 4; achievement_row_ = 0; RefreshAchievements(); }
        if (key == Key::circle) close();
        break;
      }
      if (game_tab_ == 2) {
        // Choose a subject first, then open its options.
        if (!game_category_open_) {
          const int categories = int(game_categories_.size());
          if (key == Key::up && categories) game_category_ = (game_category_ + categories - 1) % categories;
          if (key == Key::down && categories) game_category_ = (game_category_ + 1) % categories;
          if (key == Key::cross && categories && Selected() && !Selected()->title_id.empty()) {
            FillGameRows(); game_option_row_ = 0; game_category_open_ = true;
          }
          if (key == Key::circle) close();
          break;
        }
        // The game's own options: each follows the general one until changed here.
        const int own_rows = Selected() && !Selected()->title_id.empty() ? int(game_rows_.size()) : 0;
        if (key == Key::up && own_rows) game_option_row_ = (game_option_row_ + own_rows - 1) % own_rows;
        if (key == Key::down && own_rows) game_option_row_ = (game_option_row_ + 1) % own_rows;
        if ((key == Key::left || key == Key::right || key == Key::cross) && own_rows)
          ChangeGameOption(Options()[size_t(game_rows_[size_t(game_option_row_)])], key == Key::left ? -1 : 1);
        if (key == Key::triangle && game_categories_.size() > 1) {
          // The next subject.
          game_category_ = (game_category_ + 1) % int(game_categories_.size());
          game_option_row_ = 0;
          FillGameRows();
        }
        if (key == Key::square && own_rows) {
          // Back to what is recommended for this game, or to the general options.
          overrides_.clear();
          SaveGameOverrides(Selected()->title_id, overrides_);
        }
        if (key == Key::circle) game_category_open_ = false;
        break;
      }
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
      if (key == Key::circle || key == Key::triangle) close();
      break;
    }
    case Mode::settings: {
      if (key == Key::l1 || key == Key::r1) {
        settings_tab_ = 1 - settings_tab_;
        if (settings_tab_ == 1) RefreshPerGame();
        break;
      }
      if (settings_tab_ == 1) {
        // Every identified game: its own settings open from here too.
        const int games = int(per_game_.size());
        if (key == Key::up && games) per_game_row_ = (per_game_row_ + games - 1) % games;
        if (key == Key::down && games) per_game_row_ = (per_game_row_ + 1) % games;
        if (key == Key::cross && games) {
          const int wanted = per_game_[size_t(per_game_row_)];
          filter_ = 0;
          ApplyFilter();
          for (int n = 0; n < int(view_.size()); ++n) if (view_[size_t(n)] == wanted) selected_ = n;
          scroll_ = float(selected_);
          OpenGameSheet(2);
          game_return_ = Mode::settings;
          sheet_ = 1.0f;  // One sheet turns into the other: no slide.
        }
        if (key == Key::circle || key == Key::square) CloseSettings();
        break;
      }
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
          settings_changed_ = true;
        } else if (settings_row_ >= 2 && settings_row_ <= 6) {
          // A sub-menu: the options of one subject.
          if (key != Key::left) {
            category_ = settings_row_ - 2;
            option_row_ = 0;
            category_rows_.clear();
            for (int n = 0; n < int(Options().size()); ++n)
              if (Options()[size_t(n)].category == category_) category_rows_.push_back(n);
            mode_ = Mode::options;
          }
        }
        else if (settings_row_ == 7 && key == Key::cross) StartCoverDownload();
        else if (settings_row_ == 8 && key == Key::cross) { mode_ = Mode::paths; path_row_ = 0; path_error_.clear(); }
        else if (settings_row_ == 9 && key == Key::cross) RefreshLibrary(false);
        else if (settings_row_ == 10 && key == Key::cross) { RefreshSaves(); mode_ = Mode::saves; }
        else if (settings_row_ == 11 && key == Key::cross) {
          for (const auto& option : Options()) settings_.*option.field = option.recommended;
          settings_.Save();
          settings_.Apply();
          restored_ = true;
        }
        else if (settings_row_ == 12 && key == Key::cross) { settings_.Save(); restart_ = true; }
      }
      if (key == Key::up || key == Key::down) { restored_ = false; scan_result_.clear(); }
      if (key == Key::circle || key == Key::square) CloseSettings();
      break;
    }
    case Mode::options: {
      const int rows = int(category_rows_.size());
      if (key == Key::up && rows) option_row_ = (option_row_ + rows - 1) % rows;
      if (key == Key::down && rows) option_row_ = (option_row_ + 1) % rows;
      if ((key == Key::left || key == Key::right || key == Key::cross) && rows)
        ChangeOption(Options()[size_t(category_rows_[size_t(option_row_)])], key == Key::left ? -1 : 1);
      if (key == Key::circle || key == Key::square) mode_ = Mode::settings;
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
      if (key != Key::square) profile_delete_pending_ = 0;
      if (key == Key::l1 || key == Key::r1) { profile_player_ = (profile_player_ + (key == Key::r1 ? 1 : 3)) % 4; RefreshProfiles(); }
      // The profiles, then "create a new one".
      const int rows = int(profiles_.size()) + 1;
      if (key == Key::up) profile_row_ = (profile_row_ + rows - 1) % rows;
      if (key == Key::down) profile_row_ = (profile_row_ + 1) % rows;
      if (key == Key::square && profile_row_ < int(profiles_.size())) {
        const auto& profile = profiles_[size_t(profile_row_)];
        if (profile.active) {
          message_ = Tr("Troque o perfil principal antes de excluir este perfil.");
        } else if (profile_delete_pending_ != profile.xuid) {
          profile_delete_pending_ = profile.xuid;
          message_ = Tr("Aperte Quadrado novamente para excluir. Saves e conquistas ficam no backup removed-profiles.");
        } else {
          const bool removed = profile_hooks_.remove && profile_hooks_.remove(profile.xuid);
          profile_delete_pending_ = 0;
          message_ = Tr(removed ? "Perfil excluído. Saves e conquistas preservados no backup." : "Não foi possível excluir. Encerre o jogo e mantenha um perfil principal.");
          RefreshProfiles(); profile_row_ = std::min(profile_row_, int(profiles_.size()));
        }
      }
      if (key == Key::cross) {
        if (profile_row_ < int(profiles_.size())) {
          if (profile_hooks_.use_player) {
            if (!profile_hooks_.use_player(profiles_[size_t(profile_row_)].xuid, uint32_t(profile_player_)))
              message_ = Tr("Conecte o controle deste jogador e escolha um perfil que não esteja em uso.");
          } else if (profile_hooks_.use) profile_hooks_.use(profiles_[size_t(profile_row_)].xuid);
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
        else if (profile_hooks_.create_player ? profile_hooks_.create_player(new_name_, uint32_t(profile_player_)) : (profile_hooks_.create && profile_hooks_.create(new_name_))) {
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
    } else if (button == 'm') {
      // OPTIONS, as printed on the controller: three short lines.
      for (float row : {-1.0f, 0.0f, 1.0f})
        list->AddLine(ImVec2(centre.x - g, centre.y + row * 4.5f * scale), ImVec2(centre.x + g, centre.y + row * 4.5f * scale), ink, 2 * scale);
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
  if (!covers_requested_ && !games_.empty() && loading_.empty() && !covers_.Busy()) {
    covers_requested_ = true;
    StartCoverDownload(true);
  }
  // A game copied to the console while the shelf is on screen shows up by
  // itself, once its folder has stopped changing. Not under an open sheet:
  // the list would change under what the user is doing there.
  if (loading_.empty()) watch_.Shown();
  if (mode_ == Mode::shelf && loading_.empty() && watch_.TakeChanged()) RefreshLibrary(true);
  // The end of a cover download is said on the shelf for a few seconds.
  if (covers_busy_ && !covers_.Busy()) { notice_ = covers_.Status(); notice_until_ = time_ + 8.0f; }
  covers_busy_ = covers_.Busy();
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
    else if (mode_ == Mode::options) DrawOptionsSheet(c);
    else if (mode_ == Mode::saves) DrawSavesSheet(c);
    else if (mode_ == Mode::paths) DrawPaths(c);
    else if (mode_ == Mode::folders) DrawFolders(c);
    else if (mode_ == Mode::profiles) DrawProfilesSheet(c);
    else if (mode_ == Mode::name) DrawNameSheet(c);
    else DrawGameSheet(c);
  }
  // What is happening by itself: covers being fetched, a game just found.
  // Under the name, on the left: the sheets cover the right side.
  c.alpha = 1.0f;
  if (covers_.Busy()) {
    const std::string status = covers_.Status();
    c.Chip(status.rfind(Tr("Baixando capas: "), 0) == 0 ? status : std::string(Tr("Baixando capas...")), 96, 112, kAccent);
  } else if (!notice_.empty() && time_ < notice_until_) {
    c.Chip(notice_, 96, 112, kText);
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
    c.Text(Tr("O jogo aparece aqui sozinho alguns segundos depois do fim da cópia."), 960, 610, 24, kMuted, 1.0f, 1);
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
  // The settings page's address and key, on the bottom line beside the version.
  if (!web_plain_.empty())
    c.Text(Tr("Celular ou computador: ") + web_plain_ + "    " + Tr("Código: ") + web_key_, 96, 1040, 20, kMuted, 1.0f, 0, 1500);
  c.Fill(96, 964, 1728, 1, kWhite, 0.08f);
  float x = 96;
  if (count) {
    x = c.Hint('x', Tr("Abrir"), x, 990);
    x = c.Hint('t', Tr("Configurações do jogo"), x, 990);
  }
  x = c.Hint('s', Tr("Configurações"), x, 990);
  if (count > 1) x = c.Hint('h', Tr("Trocar de jogo"), x, 990);
  x = c.Hint('m', Tr("Recarregar lista"), x, 990);
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

  // Three pages: play, the game's patches and its own settings.
  {
    const char* const tabs[] = {"Jogar", "Patches", "Configurações", "Conquistas"};
    float tab_x = x + 56;
    for (int n = 0; n < 4; ++n) {
      const float width = c.Width(Tr(tabs[n]), 28);
      c.Text(Tr(tabs[n]), tab_x, 272, 28, n == game_tab_ ? kText : kFaint);
      if (n == game_tab_) c.Fill(tab_x, 310, width, 3, kAccent, 0.9f, 2);
      tab_x += width + 26;
    }
    c.Text("L1 / R1", tab_x, 278, 20, kFaint);
  }
  if (game_tab_ == 0) { DrawGamePlay(c, x); return; }
  if (game_tab_ == 2) { DrawGameOptions(c, x); return; }
  if (game_tab_ == 3) { DrawAchievements(c, x); return; }
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

void Launcher::RefreshAchievements() {
  achievements_.clear();
  if (!achievement_list_ || !Selected() || Selected()->title_id.empty()) return;
  try { achievements_ = achievement_list_(uint32_t(std::stoul(Selected()->title_id, nullptr, 16)), uint32_t(achievement_player_)); }
  catch (const std::exception&) { achievements_.clear(); }
  achievement_row_ = std::clamp(achievement_row_, 0, std::max(0, int(achievements_.size()) - 1));
}
void Launcher::DrawAchievements(Canvas& c, float x) {
  const std::string profile = achievement_player_name_ ? achievement_player_name_(uint32_t(achievement_player_)) : "";
  c.Text(std::string(Tr("Jogador")) + " " + std::to_string(achievement_player_ + 1) + " · " + profile, x + 56, 334, 24, kAccent);
  unsigned unlocked = 0, earned = 0, total = 0;
  for (const auto& entry : achievements_) { total += entry.score; if (entry.unlocked) { ++unlocked; earned += entry.score; } }
  c.Text(std::to_string(unlocked) + " / " + std::to_string(achievements_.size()) + " · " + std::to_string(earned) + " / " + std::to_string(total) + "G", x + 796, 338, 20, kMuted, 1, 2);
  const int rows = int(achievements_.size()), shown = 7;
  const int first = std::clamp(achievement_row_ - shown / 2, 0, std::max(0, rows - shown));
  for (int n = first; n < std::min(rows, first + shown); ++n) {
    const auto& entry = achievements_[size_t(n)]; const float y = 388 + (n - first) * 56.f;
    c.Fill(x + 40, y, 780, 50, n == achievement_row_ ? kSurfaceHigh : kSurface, 1, 10);
    c.Text(entry.name, x + 64, y + 12, 22, entry.unlocked ? kAccent : kBody, 1, 0, 520);
    c.Text(std::string(Tr(entry.unlocked ? "Desbloqueada" : "Bloqueada")) + " · " + std::to_string(entry.score) + "G", x + 796, y + 12, 20, kMuted, 1, 2);
  }
  if (rows) c.Wrapped(achievements_[size_t(achievement_row_)].description, x + 56, 826, 20, kBody, 750);
  else c.Wrapped(Tr("Abra este jogo com este perfil para carregar as conquistas disponíveis."), x + 56, 400, 22, kMuted, 730);
  float hint = c.Hint('t', Tr("Jogador"), x + 56, 1004); c.Hint('o', Tr("Voltar"), hint, 1004);
}

void Launcher::DrawGameOptions(Canvas& c, float x) {
  const GameEntry& game = *Selected();
  float hint = x + 56;
  if (game.title_id.empty() || game_rows_.empty()) {
    c.Wrapped(Tr("Abra este jogo uma vez: o emulador identifica o título e os ajustes dele passam a aparecer aqui."),
              x + 56, 340, 24, kMuted, 740);
    c.Hint('o', Tr("Fechar"), hint, 1004);
    return;
  }
  const auto& options = Options();
  const GameOverrides preset = GamePreset(game.title_id);
  if (!game_category_open_) {
    for (int n = 0; n < int(game_categories_.size()); ++n) {
      const int category = game_categories_[size_t(n)];
      int count = 0, own = 0;
      for (const auto& option : options) if (option.per_game && option.category == category) {
        ++count; if (overrides_.count(option.key)) ++own;
      }
      const float y = 336 + n * 88.0f;
      const bool focused = n == game_category_;
      c.Fill(x + 40, y, 780, 76, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 12);
      if (focused) c.Edge(x + 40, y, 780, 76, kAccent, 0.9f, 12, 2.0f);
      c.Text(Tr(category == 0 ? "Gráficos" : kOptionCategories[category]), x + 64, y + 12, 26, focused ? kText : kBody);
      std::string detail = std::to_string(count) + " " + Tr("Opções");
      if (own) detail += " · " + std::to_string(own) + " " + Tr("Personalizadas");
      c.Text(detail, x + 64, y + 44, 19, own ? kAccent : kMuted);
      c.Text(">", x + 792, y + 22, 28, focused ? kAccent : kFaint, 1.0f, 2);
    }
    hint = c.Hint('x', Tr("Abrir"), hint, 1004);
    c.Hint('o', Tr("Voltar"), hint, 1004);
    return;
  }
  const int category = game_categories_[size_t(game_category_)];
  c.Text(Tr(category == 0 ? "Gráficos" : kOptionCategories[category]), x + 56, 334, 28, kText);
  const int rows = int(game_rows_.size()), shown = 8;
  const int first = std::clamp(game_option_row_ - shown / 2, 0, std::max(0, rows - shown));
  if (rows > shown) {
    char position[32];
    std::snprintf(position, sizeof(position), Tr("%d de %d"), game_option_row_ + 1, rows);
    c.Text(position, x + 820, 336, 20, kFaint, 1.0f, 2);
  }
  for (int n = first; n < rows && n < first + shown; ++n) {
    const Option& option = options[size_t(game_rows_[size_t(n)])];
    const float y = 384 + (n - first) * 48.0f;
    const bool focused = n == game_option_row_;
    c.Fill(x + 40, y, 780, 44, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 10);
    if (focused) c.Edge(x + 40, y, 780, 44, kAccent, 0.9f, 10, 2.0f);
    c.Text(Tr(option.label), x + 64, y + 10, 22, focused ? kText : kBody, 1.0f, 0, 420);
    // The game's own value stands out; one it does not change shows the general value.
    const auto found = overrides_.find(option.key);
    const auto recommended = preset.find(option.key);
    const bool own = found != overrides_.end(), preset_here = !own && recommended != preset.end();
    const int value = own ? found->second : preset_here ? recommended->second : settings_.*option.field;
    std::string text = Tr(option.choices[size_t(value)]);
    if (!own) text = std::string(preset_here ? Tr("Recomendado: ") : Tr("Geral: ")) + text;
    c.Text(text, x + 796, y + 10, 22, own ? kAccent : preset_here ? kBody : kFaint, 1.0f, 2, 360);
  }
  const Option& option = options[size_t(game_rows_[size_t(game_option_row_)])];
  c.Fill(x + 40, 774, 780, 1, kWhite, 0.10f);
  c.list->PushClipRect(c.At(x + 40, 780), c.At(x + 820, 930), true);
  c.Wrapped(Tr(option.about), x + 56, 786, 20, kBody, 750);
  c.list->PopClipRect();
  const char* when = option.when == OptionWhen::start ? Tr("O emulador reinicia sozinho ao abrir este jogo.")
                   : option.when == OptionWhen::launch ? Tr("Vale a partir da próxima vez que o jogo abrir.")
                                                       : Tr("Vale na hora, também com o jogo aberto (pelo guia).");
  c.Text(when, x + 56, 936, 20, kMuted, 1.0f, 0, 750);
  char own_count[64];
  std::snprintf(own_count, sizeof(own_count), Tr("%d ajustes próprios deste jogo"), int(overrides_.size()));
  c.Text(own_count, x + 56, 966, 20, overrides_.empty() ? kFaint : kAccent);
  hint = c.Hint('x', Tr("Alterar"), hint, 1004);
  if (game_categories_.size() > 1) hint = c.Hint('t', Tr("Outro assunto"), hint, 1004);
  hint = c.Hint('s', preset.empty() ? Tr("Limpar") : Tr("Recomendado"), hint, 1004);
  c.Hint('o', Tr("Voltar"), hint, 1004);
}

// The first page of a game's sheet: the button that starts it, and what it starts with.
void Launcher::DrawGamePlay(Canvas& c, float x) {
  const GameEntry& game = *Selected();
  const float pulse = 0.5f + 0.5f * std::sin(time_ * 3.0f);
  c.Fill(x + 40, 338, 780, 88, kAccent, 1.0f, 16);
  c.Edge(x + 36, 334, 788, 96, kAccent, 0.25f + 0.35f * pulse, 20, 3.0f);
  c.Text(Tr("Jogar"), x + 430, 360, 36, kInk, 1.0f, 1);
  // What is in force for this game: its own choices, then what is recommended for it.
  c.Text(Tr("Este jogo abre com"), x + 56, 462, 24, kText);
  const GameOverrides preset = game.title_id.empty() ? GameOverrides() : GamePreset(game.title_id);
  int line = 0, more = 0;
  for (const auto& option : Options()) {
    if (!option.per_game) continue;
    const auto own = overrides_.find(option.key);
    const auto recommended = preset.find(option.key);
    const bool is_own = own != overrides_.end();
    if (!is_own && recommended == preset.end()) continue;
    if (line >= 8) { ++more; continue; }
    const float y = 506 + line * 40.0f;
    const int value = is_own ? own->second : recommended->second;
    c.Text(Tr(option.label), x + 56, y, 22, kBody, 1.0f, 0, 430);
    c.Text(std::string(Tr(option.choices[size_t(value)])) + (is_own ? "" : std::string("  ·  ") + Tr("recomendado")),
           x + 820, y, 22, is_own ? kAccent : kMuted, 1.0f, 2, 330);
    ++line;
  }
  if (!line) {
    c.Wrapped(game.title_id.empty()
                  ? Tr("As configurações gerais. Depois da primeira vez que abrir, este jogo pode ter as suas.")
                  : Tr("As configurações gerais, sem nenhum ajuste próprio. A página Configurações guarda ajustes só deste jogo."),
              x + 56, 506, 22, kMuted, 750);
  } else if (more) {
    char rest[48];
    std::snprintf(rest, sizeof(rest), Tr("e mais %d"), more);
    c.Text(rest, x + 56, 506 + line * 40.0f, 20, kFaint);
  }
  c.Fill(x + 40, 864, 780, 1, kWhite, 0.10f);
  c.Text(patch_summary_.empty() ? std::string(Tr("Nenhum patch ligado")) : patch_summary_, x + 56, 884, 22,
         patch_summary_.empty() ? kFaint : kAccent, 1.0f, 0, 750);
  c.Text(Tr("L1 / R1 mostram os patches e as configurações deste jogo."), x + 56, 922, 20, kMuted, 1.0f, 0, 750);
  float hint = c.Hint('x', Tr("Jogar"), x + 56, 1004);
  c.Hint('o', game_return_ == Mode::settings ? Tr("Voltar") : Tr("Fechar"), hint, 1004);
}

void Launcher::DrawOptionsSheet(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  c.Text(Tr("Configurações"), x + 56, 52, 24, kMuted);
  c.Text(Tr(kOptionCategories[category_]), x + 56, 88, 36, kText);
  const auto& options = Options();
  const int rows = int(category_rows_.size());
  if (!rows) return;
  const int shown = 9;
  const int first = std::clamp(option_row_ - shown / 2, 0, std::max(0, rows - shown));
  if (rows > shown) {
    char position[32];
    std::snprintf(position, sizeof(position), Tr("%d de %d"), option_row_ + 1, rows);
    c.Text(position, x + 820, 104, 20, kFaint, 1.0f, 2);
  }
  for (int n = first; n < rows && n < first + shown; ++n) {
    const Option& option = options[size_t(category_rows_[size_t(n)])];
    const float y = 156 + (n - first) * 48.0f;
    const bool focused = n == option_row_;
    c.Fill(x + 40, y, 780, 44, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 10);
    if (focused) c.Edge(x + 40, y, 780, 44, kAccent, 0.9f, 10, 2.0f);
    c.Text(Tr(option.label), x + 64, y + 10, 22, focused ? kText : kBody, 1.0f, 0, 440);
    const int value = settings_.*option.field;
    c.Text(Tr(option.choices[size_t(value)]), x + 796, y + 10, 22,
           focused ? kAccent : value == option.recommended ? kMuted : kWarning, 1.0f, 2, 320);
  }
  const Option& option = options[size_t(category_rows_[size_t(option_row_)])];
  c.Fill(x + 40, 612, 780, 1, kWhite, 0.10f);
  c.Wrapped(Tr(option.about), x + 56, 632, 20, kBody, 750);
  // What is recommended, and when a change takes effect.
  c.Text(std::string(Tr("Recomendado: ")) + Tr(option.choices[size_t(option.recommended)]), x + 56, 860, 20, kAccent, 1.0f, 0, 750);
  const char* when = option.when == OptionWhen::start ? Tr("O emulador reinicia para aplicar, ao fechar as configurações.")
                   : option.when == OptionWhen::launch ? Tr("Vale a partir da próxima vez que um jogo abrir.")
                                                       : Tr("Vale na hora, também com o jogo aberto (pelo guia).");
  c.Text(when, x + 56, 892, 20, kMuted, 1.0f, 0, 750);
  if (option.per_game)
    c.Text(Tr("Cada jogo pode ter o seu valor: abra o jogo e vá à página Configurações."), x + 56, 924, 20, kMuted, 1.0f, 0, 750);
  float hint = c.Hint('x', Tr("Alterar"), x + 56, 1004);
  hint = c.Hint('o', Tr("Voltar"), hint, 1004);
  c.Hint('v', Tr("Mover"), hint, 1004);
}

void Launcher::DrawSettingsSheet(Canvas& c) {
  const float x = 1920 - 860 * sheet_;
  c.Fill(x, 0, 860, 1080, kSheet, 0.98f);
  c.Fill(x, 0, 3, 1080, kAccent, 0.9f);
  c.Text(Tr("Configurações"), x + 56, 64, 36, kText);
  {
    const char* const tabs[] = {"Geral", "Por jogo"};
    float tab_x = x + 400;
    for (int n = 0; n < 2; ++n) {
      const float width = c.Width(Tr(tabs[n]), 26);
      c.Text(Tr(tabs[n]), tab_x, 72, 26, n == settings_tab_ ? kText : kFaint);
      if (n == settings_tab_) c.Fill(tab_x, 106, width, 3, kAccent, 0.9f, 2);
      tab_x += width + 36;
    }
    c.Text("L1 / R1", tab_x, 78, 20, kFaint);
  }
  if (settings_tab_ == 1) { DrawPerGame(c, x); return; }
  const char* language = "English";
  for (const auto& entry : kLanguages) if (entry.id == settings_.interface_language) language = Tr(entry.name);
  std::string profile = Tr("Nenhum");
  for (const auto& entry : profiles_) if (entry.active) profile = entry.name;
  // How many options of a subject are not at the recommended value.
  const auto changed = [this](int category) {
    int count = 0;
    for (const auto& option : Options())
      if (option.category == category && settings_.*option.field != option.recommended) ++count;
    if (!count) return std::string(">");
    char text[48];
    std::snprintf(text, sizeof(text), Tr("%d alteradas  >"), count);
    return std::string(text);
  };
  struct Row { const char* label; std::string value; const char* about; };
  const Row rows[] = {
      {Tr("Perfil do jogador"), profile, Tr("O perfil (gamertag) conectado no console emulado. Os jogos gravam os saves e as conquistas no perfil; cada perfil tem os seus.")},
      {Tr("Idioma"), language, Tr("Automático usa o idioma do PS5. Sem tradução da interface, usa inglês. A seleção também vale para o próximo jogo; os idiomas disponíveis dependem de cada jogo.")},
      {Tr("Vídeo"), changed(0), Tr("Filtro de imagem (Simples, CAS, FSR), nitidez, filtro anisotrópico, resolução do console emulado e VSync.")},
      {Tr("Desempenho"), changed(1), Tr("Opções que trocam precisão por velocidade: memória de vídeo, consultas de visibilidade, leituras de imagem, shaders em segundo plano e testes.")},
      {Tr("Áudio"), changed(2), Tr("Som, volume dos jogos e sons da interface.")},
      {Tr("Controles"), changed(3), Tr("Clique do touchpad, vibração e zona morta dos analógicos.")},
      {Tr("Sistema"), changed(4), Tr("Contador de FPS, avisos de conquista, jogos Arcade e registros.")},
      {Tr("Baixar capas"), covers_.Busy() ? Tr("Baixando...") : "", Tr("Baixa do XboxUnity (o mesmo serviço do Aurora) as capas dos jogos que ainda não têm uma. Precisa de internet no PS5. Uma imagem cover.jpg na pasta do jogo sempre tem prioridade.")},
      {Tr("Pastas de jogos"), std::to_string(settings_.game_paths.size()), Tr("Adicione várias pastas, inclusive em dispositivos externos. Pastas desconectadas continuam salvas. Apenas locais acessíveis ao aplicativo podem ser lidos.")},
      {Tr("Atualizar lista de jogos"), scan_result_, Tr("Procura de novo os jogos, as capas e os patches nas pastas. Um jogo copiado para o console também aparece sozinho, alguns segundos depois do fim da cópia.")},
      {Tr("Saves"), "", Tr("Veja os dados por jogo e o local de armazenamento. Perfis e conquistas são preservados junto com os saves.")},
      {Tr("Restaurar o recomendado"), restored_ ? Tr("Feito") : "", Tr("Volta todas as opções de Vídeo, Desempenho, Áudio, Controles e Sistema para o valor recomendado. Os ajustes próprios de cada jogo não mudam.")},
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
  if (settings_row_ == 7 && !covers.empty()) c.Text(covers, x + 56, 770, 20, kAccent, 1.0f, 0, 750);
  c.Fill(x + 40, 808, 780, 1, kWhite, 0.10f);
  if (web_qr_.size) {
    // The settings page: its QR code on a white card, the address and the key beside it.
    const float card = 164, module = float(int((card - 16) / float(web_qr_.size)));
    const float qx = x + 56, qy = 824, inset = (card - module * float(web_qr_.size)) / 2;
    c.Fill(qx, qy, card, card, kWhite, 1.0f, 8);
    for (int my = 0; my < web_qr_.size; ++my) {
      for (int mx = 0; mx < web_qr_.size;) {
        if (!web_qr_.At(mx, my)) { ++mx; continue; }
        int run = 1;
        while (mx + run < web_qr_.size && web_qr_.At(mx + run, my)) ++run;
        c.Fill(qx + inset + float(mx) * module, qy + inset + float(my) * module, float(run) * module, module, 0x000000);
        mx += run;
      }
    }
    c.Text(Tr("Configurações pelo celular"), x + 244, 822, 24, kText);
    c.Text(web_plain_, x + 244, 858, 22, kAccent, 1.0f, 0, 570);
    c.Text(std::string(Tr("Código: ")) + web_key_, x + 244, 890, 20, kBody);
    c.Wrapped(Tr("Aponte a câmera do celular para o código, na mesma rede do PS5. Muda as opções com o jogo aberto e baixa os registros."),
              x + 244, 922, 18, kMuted, 570);
  } else {
    c.Text(Tr("Sobre"), x + 56, 820, 28, kText);
    c.Wrapped(std::string(Tr("PS5X360: emulador experimental de Xbox 360 para PlayStation 5.")) +
              " Xenia. " + Tr("Vídeo RADV (PS5_Vulkan), áudio XMA (FFmpeg) e patches Xenia Canary. Nenhum jogo, BIOS ou chave acompanha o emulador."),
              x + 56, 860, 20, kMuted, 750);
  }
  float hint = c.Hint('x', settings_row_ >= 2 && settings_row_ <= 6 ? Tr("Abrir") : Tr("Alterar"), x + 56, 1004);
  hint = c.Hint('o', Tr("Fechar"), hint, 1004);
  c.Hint('v', Tr("Mover"), hint, 1004);
}

// The settings sheet's second page: every identified game, and how it differs from the general options.
void Launcher::DrawPerGame(Canvas& c, float x) {
  const int rows = int(per_game_.size()), shown = 13;
  if (!rows) {
    c.Wrapped(Tr("Nenhum jogo identificado ainda. Abra um jogo uma vez: o emulador reconhece o título e ele passa a aparecer aqui, com as configurações só dele."),
              x + 56, 140, 24, kMuted, 750);
    c.Hint('o', Tr("Fechar"), x + 56, 1004);
    return;
  }
  const int first = std::clamp(per_game_row_ - shown / 2, 0, std::max(0, rows - shown));
  for (int n = first; n < rows && n < first + shown; ++n) {
    const GameEntry& game = games_[size_t(per_game_[size_t(n)])];
    const float y = 122 + (n - first) * 42.0f;
    const bool focused = n == per_game_row_;
    c.Fill(x + 40, y, 780, 40, focused ? kSurfaceHigh : kSurface, focused ? 1.0f : 0.6f, 10);
    if (focused) c.Edge(x + 40, y, 780, 40, kAccent, 0.9f, 10, 2.0f);
    c.Text(game.name, x + 64, y + 8, 24, focused ? kText : kBody, 1.0f, 0, 470);
    const size_t own = LoadGameOverrides(game.title_id).size();
    const bool preset = !GamePreset(game.title_id).empty();
    char text[64];
    if (own) std::snprintf(text, sizeof(text), own == 1 ? Tr("%d ajuste próprio") : Tr("%d ajustes próprios"), int(own));
    c.Text(own ? std::string(text) : std::string(preset ? Tr("Recomendado") : Tr("Geral")), x + 796, y + 8, 22,
           own ? kAccent : preset ? kBody : kFaint, 1.0f, 2, 250);
  }
  if (rows > shown) {
    char position[32];
    std::snprintf(position, sizeof(position), Tr("%d de %d"), per_game_row_ + 1, rows);
    c.Text(position, x + 820, 678, 20, kFaint, 1.0f, 2);
  }
  c.Wrapped(Tr("Cada jogo pode ter as suas configurações de vídeo, desempenho, áudio e sistema. O que ele não muda segue as configurações gerais. Também dá para chegar aqui pela prateleira: abra o jogo e vá à página Configurações."),
            x + 56, 704, 20, kBody, 750);
  c.Fill(x + 40, 808, 780, 1, kWhite, 0.10f);
  const GameEntry& game = games_[size_t(per_game_[size_t(per_game_row_)])];
  c.Text(game.name, x + 56, 826, 24, kText, 1.0f, 0, 750);
  c.Text(std::string("ID ") + game.title_id + "   ·   " + game.kind, x + 56, 862, 20, kMuted, 1.0f, 0, 750);
  float hint = c.Hint('x', Tr("Configurações deste jogo"), x + 56, 1004);
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
  c.Text(Tr("Perfis") + std::string(" · ") + Tr("Jogador") + " " + std::to_string(profile_player_ + 1), x + 56, 64, 36, kText);
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
      if (profile.player >= 0) c.Text(std::string(Tr("Jogador")) + " " + std::to_string(profile.player + 1), x + 796, y + 20, 24, kAccent, 1.0f, 2);
      else if (profile.active) c.Text(Tr("Em uso"), x + 796, y + 20, 24, kAccent, 1.0f, 2);
    } else {
      c.Text(Tr("Criar novo perfil"), x + 64, y + 20, 24, focused ? kText : kBody);
      c.Text("+", x + 796, y + 16, 28, kAccent, 1.0f, 2);
    }
  }
  float hint = c.Hint('x', profile_row_ < int(profiles_.size()) ? Tr("Usar este perfil") : Tr("Criar"), x + 56, 1004);
  hint = c.Hint('o', Tr("Voltar"), hint, 1004);
  if (profile_row_ < int(profiles_.size())) hint = c.Hint('s', Tr("Excluir perfil"), hint, 1004);
  c.Hint('v', Tr("Mover"), hint, 1004);
  c.Text("L1 / R1 · " + std::string(Tr("Jogador")), x + 820, 956, 20, kMuted, 1, 2);
  if (!message_.empty()) c.Wrapped(message_, x + 56, 904, 20, kWarning, 750);
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
