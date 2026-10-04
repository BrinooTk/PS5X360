// SPDX-License-Identifier: MIT
#pragma once
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/patch_runtime.hpp"
#include <cstdio>
namespace xbox360ps5 {
inline int CheckPatchSelection() {
  GamePatch old{"Unlock FPS", "Disable V-Sync in the config", "", true, {{0x100, {1,2}}}};
  GamePatch current = old; current.writes[0].address = 0x200;
  GamePatch duplicate = old; duplicate.name = " unlock fps ";
  PatchFile a{"a", "game", 1, {1}, {old}}, b{"b", "game", 1, {1}, {duplicate}}, c{"c", "game", 1, {2}, {current}};
  auto selected = SelectPatchFiles({a,b,c}, 2);
  if (selected.size()!=1 || selected[0].patches[0].writes[0].address!=0x200) return 1;
  if (SelectPatchFiles({a,b},1).size()!=1 || SelectPatchFiles({a,b,c},0).size()!=1 || !SelectPatchFiles({a,b,c},3).empty()) return 2;
  GamePatch collision = old; collision.name="Another patch"; collision.writes={{0x101,{9}}};
  if (!PatchWritesConflict(old,collision)) return 3;
  collision.writes[0].bytes={2};
  if (PatchWritesConflict(old,collision) || !PatchRequiresVsyncOff(old)) return 4;
  patch_vsync_off=false;
  if (!EffectiveVsync(true) || EffectiveVsync(false)) return 5;
  patch_vsync_off=true;
  if (EffectiveVsync(true)) return 6;
  patch_vsync_off=false;
  if (!EffectiveVsync(true)) return 7;
  std::puts("PASS: version filtering before deduplication, single unknown-version row, conflicting byte detection, per-title VSync override/reset");
  return 0;
}
}
