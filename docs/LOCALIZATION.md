# Interface localization

The console launcher, in-game guide, notifications and authenticated web settings use
`Tr()` in `include/xbox360ps5/i18n.hpp`. Source literals (mostly Portuguese) are lookup keys; entries
supply English, Spanish and French translations. Keep existing keys stable and add
all four columns when adding a message. Unknown keys are returned unchanged, which
preserves game names, gamertags and other external content.

Xbox language ID `4` selects French. PS5 system language IDs `2` (France) and `22`
(Canada) both map to it. Automatic selection and manual selection use the same
catalogue. Other unsupported interface languages fall back to English. Changing the
interface language does not add translations to games; they must contain the chosen
language themselves.

## French wording

Use clear, direct French, with infinitives for actions and polite instructions in
help text. Keep experimental qualifications and performance trade-offs. Describe
what a setting does without translating technical explanations word for word.

| Concept | French |
| --- | --- |
| Settings | Paramètres |
| Library | Bibliothèque |
| Cover | Jaquette |
| Xbox achievement | Succès |
| Game-specific setting | Réglage spécifique |
| Log | Journal |
| Community patch | Patch; plural patchs |
| Touchpad | Pavé tactile |
| Xbox BACK button | BACK |

Keep game names, authors, paths, configuration keys and format names unchanged.
External patch descriptions and achievement text belong to their source content.
Use short labels in the fixed-width console panels; use full sentences in the help
area. The existing Noto Sans atlas covers French accents, œ and typographic
apostrophes. Preserve leading and trailing spaces on concatenated messages, and
preserve the order and types of printf arguments such as `%d` and `%08X`. Add a
singular message where a count of one would otherwise read incorrectly.

## Web surfaces

The settings page uses the emulator's language, including its pairing prompt.
`/api/state` includes the language tag so the document language follows changes
while the page is open.

The separate camera tool (`tools/kinect-phone`) uses `i18n.mjs`. It follows the
browser's primary language, with a visible English/French selector and an optional
`?lang=fr` or `?lang=en` override. This choice does not change console or game
settings. Use whole messages with named placeholders for dynamic status text.
Unknown errors retain their original detail.

## Checks

```sh
clang++ -std=c++20 -Iinclude tools/check-ui-language.cpp -o /tmp/check-ui-language
/tmp/check-ui-language
python3 tools/check-ui-translations.py
python3 tools/check-web-settings.py
node tools/kinect-phone/check.mjs
```

`check-web-settings.py` defaults to `clang++-18`; `CXX` can select another compatible
compiler, including any required SDK flags. Its host test covers the French pairing
page and the existing API authorization, settings and log-download behavior.

Before release, inspect the console's library, settings, profile dialogs and guide
in French, including long game names and singular/plural counts. Host checks do
not replace a PS5 build or a console layout check. See [BUILD.md](BUILD.md) for the
native toolchain and packaging requirements.
