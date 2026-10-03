// SPDX-License-Identifier: MIT
// xe::system for the console: there is no browser, file manager or message box
// to hand things to; messages go to the log.
#include "xenia/base/system.h"
#include "xenia/base/logging.h"
#include "xenia/ui/file_picker.h"
namespace xe {
void LaunchWebBrowser(const std::string_view url) { XELOGW("Not opened on the console: {}", url); }
void LaunchFileExplorer(const std::filesystem::path& path) { XELOGW("Not opened on the console: {}", path.string()); }
void ShowSimpleMessageBox(SimpleMessageBoxType type, std::string_view message) {
  if (type == SimpleMessageBoxType::Error) XELOGE("{}", message);
  else XELOGW("{}", message);
}
bool SetProcessPriorityClass(const uint32_t) { return true; }
bool IsUseNexusForGameBarEnabled() { return false; }
namespace ui {
// No file browser on the console: games are chosen in the launcher.
std::unique_ptr<FilePicker> FilePicker::Create() { return nullptr; }
}
}
