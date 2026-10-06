// SPDX-License-Identifier: MIT
#pragma once
namespace xbox360ps5 {
#ifdef XBOX360PS5_VERSION
inline constexpr char kBuildVersionLabel[] = "PS5X360 v" XBOX360PS5_VERSION;
#else
inline constexpr char kBuildVersionLabel[] = "PS5X360 development build";
#endif
}
