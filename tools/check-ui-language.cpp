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
  // Both France and Canada select French automatically; a manual choice does too.
  for (int id : {2, 22}) assert(SupportedUiLanguage(ConsoleGameLanguage(id)) == 4);
  assert(SupportedUiLanguage(4) == 4);
  for (int id : {-1, 0, 2, 3, 6, 17, 999}) assert(SupportedUiLanguage(id) == 1);
  ui_language = 4;
  assert(!std::strcmp(UiLanguageTag(), "fr"));
  assert(!std::strcmp(Tr("Configurações"), "Paramètres"));
  assert(!std::strcmp(Tr("Automático (console)"), "Automatique (console)"));
  assert(!std::strcmp(Tr("Sua prateleira está vazia"), "Votre bibliothèque est vide"));
  assert(!std::strcmp(Tr("Conquistas"), "Succès"));
  assert(!std::strcmp(Tr("Baixar capas"), "Télécharger les jaquettes"));
  assert(!std::strcmp(Tr("Apertar BACK no jogo"), "Appuyer sur BACK dans le jeu"));
  assert(!std::strcmp(Tr("FSR (upscale)"), "FSR (mise à l’échelle)"));
  assert(!std::strcmp(Tr("Código exibido na TV"), "Code affiché sur la TV"));
  assert(!std::strcmp(Tr("Sonic the Hedgehog"), "Sonic the Hedgehog"));
  char formatted[128];
  std::snprintf(formatted, sizeof(formatted), Tr("%d de %d patches ligados"), 2, 5);
  assert(!std::strcmp(formatted, "2 patchs activés sur 5"));
  std::snprintf(formatted, sizeof(formatted), Tr("Não foi possível iniciar este jogo (erro %08X)."), 0x1234u);
  assert(!std::strcmp(formatted, "Impossible de lancer ce jeu (erreur 00001234)."));
  std::snprintf(formatted, sizeof(formatted), Tr("%d de %d patch ligado"), 1, 5);
  assert(!std::strcmp(formatted, "1 patch activé sur 5"));
  std::snprintf(formatted, sizeof(formatted), Tr("%d ajuste próprio deste jogo"), 1);
  assert(!std::strcmp(formatted, "1 réglage spécifique à ce jeu"));
  std::snprintf(formatted, sizeof(formatted), Tr("%d alterada  >"), 1);
  assert(!std::strcmp(formatted, "1 modifié  >"));
  ui_language = 1;
  assert(!std::strcmp(UiLanguageTag(), "en"));
  assert(!std::strcmp(Tr("Configurações"), "Settings"));
  assert(!std::strcmp(Tr("Automático (console)"), "Automatic (console)"));
  ui_language = 5;
  assert(!std::strcmp(UiLanguageTag(), "es"));
  assert(!std::strcmp(Tr("Configurações"), "Configuración"));
  ui_language = 9;
  assert(!std::strcmp(UiLanguageTag(), "pt"));
  assert(!std::strcmp(Tr("Configurações"), "Configurações"));
  assert(!std::strcmp(Tr("Sonic the Hedgehog"), "Sonic the Hedgehog"));
  std::puts("PASS: PS5 regional variants, unsupported-language fallback and UI translations");
}
