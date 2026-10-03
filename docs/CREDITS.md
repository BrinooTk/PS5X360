# Credits and source origins

PS5X360 brings together an emulator core and native PS5 platform work. Upstream authors retain credit for their components.

| Component | Source / role |
| --- | --- |
| Xenia Canary | [xenia-canary/xenia-canary](https://github.com/xenia-canary/xenia-canary), active emulator core at `b083312b8b18e07e6e410b82104191f126722794` |
| Original Xenia | [xenia-project/xenia](https://github.com/xenia-project/xenia), earlier port core at `95a5c3ee250f80c3b9d139658649d9ffb6db3eec` |
| PS5_Vulkan | [mihawk-99/PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), RADV integration, native application tooling and linker support; development checkout at `3f3ee69607013b345d2baa6d6a37c86745649a08` |
| Native runtime / Castation | [BrinooTk/castation](https://github.com/BrinooTk/castation), prepared runtime/toolchain used by this workspace; development checkout at `94dfef79479266c66d51b2d57314557c606dc1ce` |
| PS5 native app boilerplate | [blackbearreloaded/ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), native application infrastructure used during bring-up |
| Community game patches | [xenia-canary/game-patches](https://github.com/xenia-canary/game-patches), credit to each patch's named authors; the release includes a revision notice |
| Covers | XboxUnity, the cover service also used by Aurora |
| Noto Sans | [Noto fonts](https://github.com/notofonts/noto-fonts), regular and semibold interface fonts; SIL Open Font License |
| FFmpeg | XMA audio decoding; see the bundled FFmpeg license notices |
| Dear ImGui, Mesa/RADV, Vulkan dependencies and other libraries | See individual notices in `licenses/` |

Reference projects consulted during the port include [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden), [PS5CEMU](https://github.com/premohq/PS5CEMU), [PS5SX2](https://github.com/Swordpdf/PS5SX2) and [XPSemu](https://github.com/ZiZc3/XPSemu). The launcher uses its own presentation inspired by the Xbox 360 dashboard and Aurora.

## Licenses

The project's original code is MIT. The emulator core, graphics driver, native runtime, tools and other dependencies keep their respective licenses. The root MIT license does not replace third-party BSD, LGPL or GPL terms. Copies of notices are included in `licenses/` and in release archives.

## Artwork

The GitHub banner and installation illustration are original decorative assets generated using the built-in image generation tool. They are not screenshots or evidence of emulation performance. Prompts are recorded in [images/README.md](images/README.md).
