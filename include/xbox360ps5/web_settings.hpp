// SPDX-License-Identifier: MIT
// The settings page for a phone or a PC on the same network: a small HTTP
// server inside the title. The page shows the options (for every game or for
// one), sends changes back and lists the session logs for download.
//
// The server owns nothing. The thread that owns the settings publishes what the
// page shows as one JSON text and takes the changes the page asked for; the
// server only hands the two across. Every request but the page itself must
// carry the key shown on the television (it is in the QR code's address), so
// another device on the network cannot change options or read logs unasked.
#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace xbox360ps5 {
struct WebChange {
  std::string scope;  // Empty: the general options. Else a title ID.
  std::string key;    // An option's key.
  int value = 0;      // -1 for one game: back to the general option.
};
// Starts the server on the first free port from 8360 on. `logs`: the folder
// whose .log and .txt files the page lists and serves.
bool StartWebSettings(const std::filesystem::path& logs);
// "http://<this console>:<port>/?k=<key>", or empty when it is not running.
std::string WebSettingsAddress();
// The same without the key, and the key by itself, for showing on screen.
std::string WebSettingsPlainAddress();
std::string WebSettingsKey();
void PublishWebState(std::string json);
std::vector<WebChange> TakeWebChanges();
// Text inside a JSON string, quotes included.
std::string JsonText(const std::string& text);
}
