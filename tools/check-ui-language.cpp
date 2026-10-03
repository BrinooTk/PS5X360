// SPDX-License-Identifier: MIT
// Standalone host regression for console language mapping and UI fallbacks.
#include "xbox360ps5/i18n.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main() {
  using namespace xbox360ps5;
  for (int id : {7, 17}) assert(ConsoleGameLanguage(id) == 9);
  for (int id : {3, 20}) assert(ConsoleGameLanguage(id) == 5);
  for (int id : {-1, 1, 18, 999}) assert(ConsoleGameLanguage(id) == 1);
  // French console games can use French while the untranslated UI uses English.
  assert(ConsoleGameLanguage(22) == 4);
  assert(SupportedUiLanguage(ConsoleGameLanguage(22)) == 1);
  ui_language = 1;
  assert(!std::strcmp(Tr("Configurações"), "Settings"));
  assert(!std::strcmp(Tr("Automático (console)"), "Automatic (console)"));
  ui_language = 5;
  assert(!std::strcmp(Tr("Configurações"), "Configuración"));
  ui_language = 9;
  assert(!std::strcmp(Tr("Configurações"), "Configurações"));
  assert(!std::strcmp(Tr("Sonic the Hedgehog"), "Sonic the Hedgehog"));
  std::puts("PASS: PS5 regional variants, unsupported-language fallback and UI translations");
}
