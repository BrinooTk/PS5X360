// SPDX-License-Identifier: MIT
#include "xbox360ps5/web_settings.hpp"
#include "xbox360ps5/motion_input.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <netinet/in.h>
#include <pthread.h>
#include <random>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
std::mutex mutex;
std::string state = "{}";
std::vector<WebChange> changes;
fs::path log_folder;
std::string key, address;
int listener = -1;
unsigned short port = 0;

// The page. It draws everything from /api/state, so its words come from the
// title in the interface's language.
const char kPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PS5X360</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#0a0c11;color:#d5dae2;font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;background:#12161d;padding:14px 16px;border-bottom:2px solid #35d07f;z-index:2}
h1{margin:0;font-size:20px;color:#f3f5f8}h1 small{font-size:13px;color:#6f7888;font-weight:400;margin-left:8px}
#running{font-size:13px;color:#9aa3b2;margin-top:2px}
main{max-width:760px;margin:0 auto;padding:12px 12px 60px}
select,button,input{font:inherit;color:#f3f5f8;background:#252c38;border:1px solid #3a4352;border-radius:10px;padding:9px 10px}
#scope{width:100%;margin:6px 0 12px}
nav{display:flex;gap:6px;overflow-x:auto;margin-bottom:10px}
nav button{white-space:nowrap;background:#1a1f28;color:#9aa3b2}
nav button.on{background:#35d07f;color:#06130c;border-color:#35d07f;font-weight:600}
.option{background:#1a1f28;border-radius:12px;padding:12px;margin-bottom:8px}
.option .top{display:flex;gap:10px;align-items:center;justify-content:space-between}
.option b{color:#f3f5f8;font-weight:600}
.option select{max-width:55%}
.option p{margin:8px 0 0;font-size:13px;color:#9aa3b2}
.option .note{color:#6f7888}.changed select{border-color:#f0a67a}.own select{border-color:#35d07f}
.logs a{display:block;color:#35d07f;padding:8px 0;border-bottom:1px solid #1a1f28;text-decoration:none;word-break:break-all}
.logs span{color:#6f7888;font-size:13px;margin-left:6px}
.zip a{display:flex;justify-content:space-between;gap:10px;align-items:center;background:#1a1f28;border-radius:12px;padding:12px;margin-bottom:8px;color:#f3f5f8;text-decoration:none;font-weight:600}
.zip a.all{background:#35d07f;color:#06130c}.zip span{font-weight:400;font-size:13px;color:#9aa3b2;white-space:nowrap}.zip a.all span{color:#0b3a22}
h3{font-size:14px;color:#9aa3b2;margin:16px 0 8px;font-weight:600}
#gate{padding:30px 16px;text-align:center}#gate input{width:9em;text-align:center;letter-spacing:.2em;margin:10px}
#saved{position:fixed;bottom:14px;left:50%;transform:translateX(-50%);background:#35d07f;color:#06130c;padding:8px 16px;border-radius:20px;font-weight:600;opacity:0;transition:opacity .2s}
#saved.on{opacity:1}
</style></head><body>
<header><h1>PS5X360<small id="version"></small></h1><div id="running"></div></header>
<main><div id="gate" hidden><div id="gatetext">Code shown on the TV</div><input id="code" maxlength="6" autocapitalize="off" autocomplete="off"><br><button id="enter">OK</button></div>
<div id="app" hidden><select id="scope"></select><nav id="tabs"></nav><div id="list"></div></div></main>
<div id="saved"></div>
<script>
let key=new URLSearchParams(location.search).get('k')||sessionStorage.getItem('k')||'',state=null,tab=0,scope='';
const $=id=>document.getElementById(id);
async function api(path,options){const r=await fetch(path,Object.assign({headers:{'X-Key':key}},options||{}));if(r.status==403)throw new Error('key');return r.json();}
async function load(){
  try{state=await api('/api/state');}catch(e){$('app').hidden=true;$('gate').hidden=false;return;}
  sessionStorage.setItem('k',key);$('gate').hidden=true;$('app').hidden=false;render();
}
function render(){
  const t=state.text;$('version').textContent=state.version;
  $('running').textContent=state.running?t.running+' '+state.running.name:'';
  const games=state.games||[];if(scope&&!games.some(g=>g.id==scope))scope='';
  $('scope').innerHTML='<option value="">'+t.all+'</option>'+games.map(g=>'<option value="'+g.id+'">'+esc(g.name)+' ['+g.id+']</option>').join('');
  $('scope').value=scope;
  const names=state.categories.concat([t.logs]);
  $('tabs').innerHTML=names.map((n,i)=>'<button class="'+(i==tab?'on':'')+'" data-tab="'+i+'">'+esc(n)+'</button>').join('');
  if(tab==state.categories.length){logs();return;}
  const game=scope?games.find(g=>g.id==scope):null,own=game?(game.own||{}):null,pre=game?(game.preset||{}):{};
  $('list').innerHTML=state.options.filter(o=>o.category==tab&&(!scope||o.perGame)).map(o=>{
    const mine=own&&(o.key in own),value=own?(mine?own[o.key]:-1):o.value;
    let choices=o.choices.map((c,i)=>'<option value="'+i+'"'+(i==value?' selected':'')+'>'+esc(c)+(i==o.recommended?' ★':'')+'</option>').join('');
    if(own)choices='<option value="-1"'+(mine?'':' selected')+'>'+((o.key in pre)?t.recommended+esc(o.choices[pre[o.key]]):t.general+': '+esc(o.choices[o.value]))+'</option>'+choices;
    const cls=own?(mine?'own':''):(o.value!=o.recommended?'changed':'');
    return '<div class="option '+cls+'"><div class="top"><b>'+esc(o.label)+'</b><select data-key="'+o.key+'">'+choices+'</select></div><p>'+esc(o.about)+'</p><p class="note">'+t.recommended+' '+esc(o.choices[o.recommended])+' · '+t[o.when]+'</p></div>';
  }).join('')||'<p>'+t.none+'</p>';
}
async function logs(){
  const t=state.text,r=await api('/api/logs'),size=b=>b>=1048576?(b/1048576).toFixed(1)+' MB':Math.ceil(b/1024)+' KB';
  const zip=(game,cls,name,count,bytes)=>'<a class="'+cls+'" href="/logs.zip?k='+key+'&game='+game+'" download>'+esc(name)+'<span>'+count+' · '+size(bytes)+'</span></a>';
  $('list').innerHTML='<div class="zip">'+zip('all','all',t.zipAll,r.count,r.size)+'<h3>'+esc(t.zipGame)+'</h3>'+
    r.games.map(g=>zip(g.id,'',g.name,g.count,g.size)).join('')+'</div><h3>'+esc(t.files)+'</h3><div class="logs">'+
    r.files.map(l=>'<a href="/logs/'+encodeURIComponent(l.name)+'?k='+key+'" download>'+esc(l.name)+'<span>'+size(l.size)+'</span></a>').join('')+'</div>';
}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
$('tabs').onclick=e=>{if(e.target.dataset.tab!==undefined){tab=+e.target.dataset.tab;render();}};
$('scope').onchange=e=>{scope=e.target.value;render();};
$('list').onchange=async e=>{
  const k=e.target.dataset.key;if(!k)return;
  await api('/api/set',{method:'POST',headers:{'X-Key':key,'Content-Type':'application/x-www-form-urlencoded'},body:'scope='+scope+'&key='+k+'&value='+e.target.value});
  $('saved').textContent=state.text.saved;$('saved').classList.add('on');setTimeout(()=>$('saved').classList.remove('on'),1200);
  setTimeout(load,500);
};
$('enter').onclick=()=>{key=$('code').value.trim().toLowerCase();load();};
load();setInterval(()=>{if(!$('app').hidden&&document.activeElement.tagName!='SELECT'&&tab<state.categories.length)load();},5000);
</script></body></html>
)HTML";

void Send(int client, const char* status, const char* type, const std::string& body, const char* extra = "") {
  char head[320];
  const int length = std::snprintf(head, sizeof(head),
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
      "X-Content-Type-Options: nosniff\r\n%sConnection: close\r\n\r\n", status, type, body.size(), extra);
  if (send(client, head, size_t(length), 0) < 0) return;
  size_t sent = 0;
  while (sent < body.size()) {
    const ssize_t part = send(client, body.data() + sent, body.size() - sent, 0);
    if (part <= 0) return;
    sent += size_t(part);
  }
}
// A header's value, or a parameter of a query or a form.
std::string Header(const std::string& request, const char* name) {
  const std::string wanted = std::string("\n") + name + ":";
  std::string lower = request;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  std::string lower_wanted = wanted;
  std::transform(lower_wanted.begin(), lower_wanted.end(), lower_wanted.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  const size_t at = lower.find(lower_wanted);
  if (at == std::string::npos) return {};
  size_t from = at + wanted.size();
  while (from < request.size() && request[from] == ' ') ++from;
  const size_t to = request.find('\r', from);
  return request.substr(from, to == std::string::npos ? std::string::npos : to - from);
}
std::string Parameter(const std::string& text, const char* name) {
  const std::string wanted = std::string(name) + "=";
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('&', at);
    if (end == std::string::npos) end = text.size();
    if (!text.compare(at, wanted.size(), wanted)) return text.substr(at + wanted.size(), end - at - wanted.size());
    at = end + 1;
  }
  return {};
}
std::string Unescaped(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
      out += char(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else out += text[i] == '+' ? ' ' : text[i];
  }
  return out;
}
// Names the page may ask for: a session log or a text file, in the folder
// itself. A game's name is part of its logs' names, so anything goes but what
// could lead out of the folder.
bool LogName(const std::string& name) {
  if (name.empty() || name.size() > 200 || name.front() == '.') return false;
  for (const unsigned char c : name) if (c < 0x20 || c == 0x7F || c == '/' || c == '\\') return false;
  return name.find("..") == std::string::npos &&
         (name.size() > 4 && (!name.compare(name.size() - 4, 4, ".log") || !name.compare(name.size() - 4, 4, ".txt")));
}
// A file that goes into an archive of logs: a log or a text file, or a rotated
// part of a log.
bool ArchiveName(const std::string& name) {
  if (LogName(name)) return true;
  const size_t part = name.rfind(".log.part");
  return part != std::string::npos && name.size() == part + 10 && std::isdigit(static_cast<unsigned char>(name.back())) &&
         LogName(name.substr(0, part + 4));
}
// "Game-<name>-<8 hex digits>-<date>-<time>-UTC-<n>.log": the game a session
// log belongs to. The digits are the same for every session of one game,
// whatever name it was listed under at the time.
bool SessionOf(const std::string& file, std::string* id, std::string* name) {
  const size_t utc = file.rfind("-UTC-");
  if (file.compare(0, 5, "Game-") || utc == std::string::npos || utc < 5 + 1 + 9 + 16) return false;
  const size_t stamp = utc - 16, id_at = stamp - 9;
  if (file[stamp] != '-' || file[id_at] != '-') return false;
  for (size_t n = id_at + 1; n < stamp; ++n) if (!std::isxdigit(static_cast<unsigned char>(file[n]))) return false;
  *id = file.substr(id_at + 1, 8);
  *name = file.substr(5, id_at - 5);
  return true;
}
uint32_t Crc32(const std::string& data) {
  static uint32_t table[256];
  static bool made = false;
  if (!made) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int bit = 0; bit < 8; ++bit) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    made = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (const unsigned char c : data) crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
// Deflate with the fixed codes and the last place each three bytes were seen:
// small, and enough for logs, whose lines repeat one another.
std::string Deflated(const std::string& in) {
  static const uint16_t length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t distance_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  std::string out;
  out.reserve(in.size() / 4 + 64);
  uint64_t bits = 0;
  int held = 0;
  const auto put = [&](uint32_t value, int length) {  // Least significant bit first.
    bits |= uint64_t(value) << held;
    for (held += length; held >= 8; held -= 8, bits >>= 8) out += char(bits & 0xFF);
  };
  const auto code = [&](uint32_t value, int length) {  // A Huffman code goes most significant bit first.
    uint32_t reversed = 0;
    for (int bit = 0; bit < length; ++bit) reversed |= ((value >> bit) & 1u) << (length - 1 - bit);
    put(reversed, length);
  };
  const auto symbol = [&](unsigned s) {
    if (s < 144) code(0x30 + s, 8);
    else if (s < 256) code(0x190 + s - 144, 9);
    else if (s < 280) code(s - 256, 7);
    else code(0xC0 + s - 280, 8);
  };
  put(1, 1);  // The last block,
  put(1, 2);  // with the fixed codes.
  const auto* bytes = reinterpret_cast<const unsigned char*>(in.data());
  const size_t size = in.size();
  std::vector<int32_t> seen(size_t(1) << 15, -1);
  for (size_t at = 0; at < size;) {
    size_t match = 0, distance = 0;
    if (at + 3 <= size) {
      const uint32_t hash = (uint32_t(bytes[at]) << 16 | uint32_t(bytes[at + 1]) << 8 | bytes[at + 2]) * 2654435761u >> 17;
      const int32_t before = seen[hash];
      seen[hash] = int32_t(at);
      if (before >= 0 && at - size_t(before) <= 32768) {
        const size_t most = std::min<size_t>(258, size - at);
        size_t same = 0;
        while (same < most && bytes[size_t(before) + same] == bytes[at + same]) ++same;
        if (same >= 3) { match = same; distance = at - size_t(before); }
      }
    }
    if (!match) { symbol(bytes[at++]); continue; }
    int l = 28;
    while (length_base[l] > match) --l;
    symbol(257 + unsigned(l));
    put(uint32_t(match - length_base[l]), length_extra[l]);
    int d = 29;
    while (distance_base[d] > distance) --d;
    code(uint32_t(d), 5);
    put(uint32_t(distance - distance_base[d]), distance_extra[d]);
    at += match;
  }
  symbol(256);
  if (held) put(0, 8 - held);
  return out;
}
bool SendAll(int client, const std::string& data) {
  for (size_t sent = 0; sent < data.size();) {
    const ssize_t part = send(client, data.data() + sent, data.size() - sent, 0);
    if (part <= 0) return false;
    sent += size_t(part);
  }
  return true;
}
// Streams a ZIP of the logs of one game (by the eight digits of its session
// logs), or of every log when `game` is "all". The boot log goes in every
// archive: crash records are written there. One file is in memory at a time.
void SendLogsZip(int client, const std::string& game) {
  struct Entry { std::string name; uint32_t crc, packed, size, offset; uint16_t method, time, date; };
  std::vector<std::string> names;
  std::error_code error;
  for (fs::directory_iterator it(log_folder, error), end; !error && it != end; it.increment(error)) {
    std::error_code entry_error;
    const std::string name = it->path().filename().string();
    if (!it->is_regular_file(entry_error) || !ArchiveName(name)) continue;
    std::string id, title;
    const bool session = SessionOf(name, &id, &title);
    if (game == "all" || name == "boot.log" || (session && id == game)) names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  timeval limit{30, 0};
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
  char stamp[32] = "";
  const time_t now = time(nullptr);
  tm utc{};
  gmtime_r(&now, &utc);
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
  const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Disposition: attachment; filename=\"PS5X360-logs-" +
      game + "-" + stamp + ".zip\"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
  if (!SendAll(client, head)) return;
  const auto le = [](std::string& out, uint32_t value, int bytes) { for (int n = 0; n < bytes; ++n) out += char(value >> (8 * n)); };
  std::vector<Entry> entries;
  uint64_t offset = 0;
  for (const std::string& name : names) {
    std::ifstream file(log_folder / name, std::ios::binary);
    if (!file) continue;
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // Zip without the 64-bit extension: four gigabytes in all, far above any set of logs kept here.
    if (data.size() > (64u << 20) || offset + data.size() > 0xE0000000ull || entries.size() >= 60000) continue;
    Entry entry{name, Crc32(data), 0, uint32_t(data.size()), uint32_t(offset), 8, 0, 0x21};
    struct stat info{};
    tm changed{};
    if (!stat((log_folder / name).c_str(), &info) && gmtime_r(&info.st_mtime, &changed) && changed.tm_year >= 80) {
      entry.time = uint16_t(changed.tm_hour << 11 | changed.tm_min << 5 | changed.tm_sec / 2);
      entry.date = uint16_t((changed.tm_year - 80) << 9 | (changed.tm_mon + 1) << 5 | changed.tm_mday);
    }
    std::string packed = Deflated(data);
    if (packed.size() >= data.size()) { packed.swap(data); entry.method = 0; }
    entry.packed = uint32_t(packed.size());
    std::string local;
    le(local, 0x04034B50, 4); le(local, 20, 2); le(local, 0x0800, 2); le(local, entry.method, 2);
    le(local, entry.time, 2); le(local, entry.date, 2); le(local, entry.crc, 4); le(local, entry.packed, 4);
    le(local, entry.size, 4); le(local, uint32_t(name.size()), 2); le(local, 0, 2);
    local += name;
    if (!SendAll(client, local) || !SendAll(client, packed)) return;
    offset += local.size() + packed.size();
    entries.push_back(std::move(entry));
  }
  std::string directory;
  for (const Entry& entry : entries) {
    le(directory, 0x02014B50, 4); le(directory, 20, 2); le(directory, 20, 2); le(directory, 0x0800, 2);
    le(directory, entry.method, 2); le(directory, entry.time, 2); le(directory, entry.date, 2); le(directory, entry.crc, 4);
    le(directory, entry.packed, 4); le(directory, entry.size, 4); le(directory, uint32_t(entry.name.size()), 2);
    le(directory, 0, 2); le(directory, 0, 2); le(directory, 0, 2); le(directory, 0, 2); le(directory, 0, 4);
    le(directory, entry.offset, 4);
    directory += entry.name;
  }
  const uint32_t directory_size = uint32_t(directory.size());
  le(directory, 0x06054B50, 4); le(directory, 0, 2); le(directory, 0, 2); le(directory, uint32_t(entries.size()), 2);
  le(directory, uint32_t(entries.size()), 2); le(directory, directory_size, 4); le(directory, uint32_t(offset), 4); le(directory, 0, 2);
  SendAll(client, directory);
}
void Answer(int client) {
  timeval limit{5, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
  std::string request;
  char buffer[2048];
  size_t head_end = std::string::npos;
  while (request.size() < 16384 && (head_end = request.find("\r\n\r\n")) == std::string::npos) {
    const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
    if (got <= 0) return;
    request.append(buffer, size_t(got));
  }
  if (head_end == std::string::npos) return;
  const size_t wanted_body = std::min<size_t>(size_t(std::atoi(Header(request, "Content-Length").c_str())), 4096);
  while (request.size() < head_end + 4 + wanted_body) {
    const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
    if (got <= 0) return;
    request.append(buffer, size_t(got));
  }
  const std::string body = request.substr(head_end + 4, wanted_body);
  const size_t space = request.find(' '), space2 = request.find(' ', space + 1);
  if (space == std::string::npos || space2 == std::string::npos) return;
  const std::string method = request.substr(0, space);
  std::string target = request.substr(space + 1, space2 - space - 1), query;
  if (const size_t mark = target.find('?'); mark != std::string::npos) { query = target.substr(mark + 1); target.resize(mark); }
  if (target == "/" && method == "GET") { Send(client, "200 OK", "text/html; charset=utf-8", kPage); return; }
  if (target == "/favicon.ico") { Send(client, "404 Not Found", "text/plain", ""); return; }
  // Everything else needs the key shown on the television.
  std::string given = Header(request, "X-Key");
  if (given.empty()) given = Parameter(query, "k");
  if (given != key) { Send(client, "403 Forbidden", "application/json", "{\"error\":\"key\"}"); return; }
  if (target == "/api/state" && method == "GET") {
    std::string copy;
    { std::lock_guard<std::mutex> lock(mutex); copy = state; }
    Send(client, "200 OK", "application/json; charset=utf-8", copy);
  } else if (target == "/api/motion/status" && method == "GET") {
    Send(client, "200 OK", "application/json", motion::Status());
  } else if (target == "/api/motion/frame" && method == "POST") {
    const auto result = motion::Receive(body);
    const char* status = result == motion::Result::accepted ? "200 OK" : result == motion::Result::malformed ? "400 Bad Request" : "409 Conflict";
    Send(client, status, "application/json", result == motion::Result::accepted ? "{\"ok\":true}" : "{\"error\":\"disabled or invalid motion frame\"}");
  } else if (target == "/api/set" && method == "POST") {
    WebChange change;
    change.scope = Unescaped(Parameter(body, "scope"));
    change.key = Unescaped(Parameter(body, "key"));
    change.value = std::atoi(Parameter(body, "value").c_str());
    const auto plain = [](const std::string& text, size_t most) {
      if (text.size() > most) return false;
      for (const unsigned char c : text) if (!(std::isalnum(c) || c == '_')) return false;
      return true;
    };
    if (change.key.empty() || !plain(change.key, 40) || !plain(change.scope, 8) || change.value < -1 || change.value > 64) {
      Send(client, "400 Bad Request", "application/json", "{\"error\":\"request\"}");
      return;
    }
    { std::lock_guard<std::mutex> lock(mutex); if (changes.size() < 64) changes.push_back(std::move(change)); }
    Send(client, "200 OK", "application/json", "{\"ok\":true}");
  } else if (target == "/api/logs" && method == "GET") {
    struct Entry { std::string name; uintmax_t size; fs::file_time_type time; };
    struct Game { std::string id, name; uintmax_t size; unsigned count; fs::file_time_type time; };
    std::vector<Entry> entries;
    std::vector<Game> games;
    uintmax_t all_size = 0;
    unsigned all_count = 0;
    std::error_code error;
    for (fs::directory_iterator it(log_folder, error), end; !error && it != end; it.increment(error)) {
      std::error_code entry_error;
      const std::string name = it->path().filename().string();
      if (!it->is_regular_file(entry_error) || !ArchiveName(name)) continue;
      const Entry entry{name, it->file_size(entry_error), it->last_write_time(entry_error)};
      all_size += entry.size;
      ++all_count;
      // The games the logs are of, each under the name of its latest session.
      std::string id, title;
      if (SessionOf(name, &id, &title)) {
        auto game = std::find_if(games.begin(), games.end(), [&](const Game& g) { return g.id == id; });
        if (game == games.end()) game = games.insert(games.end(), Game{id, title, 0, 0, entry.time});
        if (entry.time >= game->time) { game->time = entry.time; game->name = title; }
        game->size += entry.size;
        ++game->count;
      }
      if (LogName(name)) entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time > b.time; });
    std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return a.time > b.time; });
    std::string json = "{\"count\":" + std::to_string(all_count) + ",\"size\":" + std::to_string(all_size) + ",\"games\":[";
    for (size_t n = 0; n < games.size() && n < 200; ++n)
      json += std::string(n ? "," : "") + "{\"id\":\"" + games[n].id + "\",\"name\":" + JsonText(games[n].name) +
              ",\"count\":" + std::to_string(games[n].count) + ",\"size\":" + std::to_string(games[n].size) + "}";
    json += "],\"files\":[";
    for (size_t n = 0; n < entries.size() && n < 40; ++n)
      json += std::string(n ? "," : "") + "{\"name\":" + JsonText(entries[n].name) + ",\"size\":" + std::to_string(entries[n].size) + "}";
    Send(client, "200 OK", "application/json; charset=utf-8", json + "]}");
  } else if (target == "/logs.zip" && method == "GET") {
    const std::string game = Parameter(query, "game");
    bool known = game == "all";
    if (!known && game.size() == 8) {
      known = true;
      for (const unsigned char c : game) known = known && std::isxdigit(c);
    }
    if (!known) { Send(client, "400 Bad Request", "text/plain", "which game"); return; }
    SendLogsZip(client, game);
  } else if (!target.compare(0, 6, "/logs/") && method == "GET") {
    const std::string name = Unescaped(target.substr(6));
    std::ifstream file(log_folder / name, std::ios::binary);
    if (!LogName(name) || !file) { Send(client, "404 Not Found", "text/plain", "not found"); return; }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.size() > (32u << 20)) text.erase(0, text.size() - (32u << 20));
    Send(client, "200 OK", "text/plain; charset=utf-8", text, "Content-Disposition: attachment\r\n");
  } else {
    Send(client, "404 Not Found", "text/plain", "not found");
  }
}
void* Serve(void*) {
  for (;;) {
    const int client = accept(listener, nullptr, nullptr);
    if (client < 0) { usleep(100000); continue; }
    Answer(client);
    close(client);
  }
  return nullptr;
}
// The address this console has on the network: the one a socket towards the
// outside would use. Nothing is sent.
std::string LocalAddress() {
  const int probe = socket(AF_INET, SOCK_DGRAM, 0);
  if (probe < 0) return {};
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(53);
  inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);
  std::string text;
  sockaddr_in local{};
  socklen_t length = sizeof(local);
  if (!connect(probe, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) &&
      !getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length)) {
    char buffer[32] = "";
    if (inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer)) && std::strcmp(buffer, "0.0.0.0")) text = buffer;
  }
  close(probe);
  return text;
}
}
std::string JsonText(const std::string& text) {
  std::string out = "\"";
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c == '\n') out += "\\n";
    else if (c < 0x20) out += ' ';
    else out += char(c);
  }
  return out + "\"";
}
bool StartWebSettings(const fs::path& logs) {
  if (listener >= 0) return true;
  log_folder = logs;
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return false;
  const int yes = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  bool bound = false;
  for (unsigned short candidate = 8360; candidate < 8368 && !bound; ++candidate) {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(candidate);
    if (!bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local))) { bound = true; port = candidate; }
  }
  pthread_t thread;
  if (!bound || listen(listener, 4) || pthread_create(&thread, nullptr, Serve, nullptr)) {
    close(listener);
    listener = -1;
    return false;
  }
  pthread_detach(thread);
  std::random_device random;
  char text[8];
  std::snprintf(text, sizeof(text), "%06x", unsigned(random()) & 0xFFFFFFu);
  key = text;
  address = LocalAddress();
  return true;
}
std::string WebSettingsPlainAddress() {
  if (listener < 0) return {};
  if (address.empty()) address = LocalAddress();  // The network may have come up since.
  return address.empty() ? std::string() : "http://" + address + ":" + std::to_string(port) + "/";
}
std::string WebSettingsAddress() {
  const std::string plain = WebSettingsPlainAddress();
  return plain.empty() ? plain : plain + "?k=" + key;
}
std::string WebSettingsKey() { return listener < 0 ? std::string() : key; }
void PublishWebState(std::string json) {
  std::lock_guard<std::mutex> lock(mutex);
  state = std::move(json);
}
std::vector<WebChange> TakeWebChanges() {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<WebChange> taken;
  taken.swap(changes);
  return taken;
}
}
