// SPDX-License-Identifier: MIT
// The interface's sound effects (assets/sounds/*.raw: 48 kHz, 16-bit stereo).
#pragma once
namespace xbox360ps5 {
enum class UiSound { move, select, back };
// Loads the clips and opens the port. Without the files nothing plays.
void StartUiSounds();
void PlayUiSound(UiSound sound);
void MuteUiSounds(bool mute);
}
