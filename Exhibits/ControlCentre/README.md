# Control Centre, Typography, and Themes

## Delivered scope

- Native quick panel: collapse/expand, Fonts, Theme, session tooltips, interface scale, Apply, and Discard.
- Native Control Centre: discovered typeface cards, available weights, type-scale previews, eight themes,
  corner radius, ten accents, and semantic colours.
- Draft edits remain separate from applied preferences; Apply saves them and Discard restores applied settings.
- Applied body face, weight, size, interface scale, and palette affect actual native UI output.
- Preferences restore font selection by family name and live in `%LOCALAPPDATA%/Frontier/Appearance.toml` on Windows.
- No GI, renderer quality, scene resolution, AA, shadows, materials inspector, or scene-renderer dependencies.

The inherited `AppearanceInspector`, `FontCodec`, and `TypefaceRegistry` were selectively adapted from
`SultanAladin/Frontier-` commit `338293863d7df4e585dec0c4acae45fe9c524ed5`.
`ControlCentrePanel` replaces the renderer-coupled host for this UI-only scope; it is not a claim of complete
upstream notch dragging, spring, or visual parity. The engine's Render directory remains blank.

## Fonts and inventory

Downloaded unmodified static faces: Inter, Archivo, Space Grotesk, and JetBrains Mono.
There are 30 faces; each family retains its original SIL Open Font License 1.1 notice.
General Sans and Clash Display are not downloaded: no open-source licence was established for those families.

`Automation/FontSources.json` pins file sources and SHA-256 digests to the approved baseline.
`Automation/PrepareFonts.py` downloads missing pinned files, refuses modified files, and atomically generates
`UI/Assets/Fonts/FontCatalogue.json` from every local TTF/OTF using actual font metadata.
The catalogue includes family, weight, italic classification, relative path, and SHA-256; invalid files are reported.
Native `FontCodec` also scans current files and regenerates per-family TOML descriptors.
Rescanning removes deleted faces instead of reviving stale descriptor entries.
`TypefaceRegistry` includes discovered families beyond its preference list and permits single-weight families.

Downloaded font binaries and generated descriptors remain Git-ignored; source pins and recovery code are tracked.
Add static fonts with `Family-Weight.ttf` or `.otf` naming, regenerate the catalogue, and restart the application.
Native filename classification and metadata inventory are distinct: unusual filenames may need normalisation.
WOFF/WOFF2, arbitrary variable-axis instances, shaping, and ligature controls are not implemented in this batch.
Non-body type-scale settings currently drive their previews; a full editor-wide typography mapping is future work.

## Native Windows build — MSVC

Use a Visual Studio x64 developer terminal, Python 3.9+, Git, and CMake 3.21+:

```powershell
python -m pip install -r Automation/FontRequirements.txt
python Automation/PrepareUI.py
python Automation/PrepareFonts.py
cmake -S . -B Build/UI -A x64
cmake --build Build/UI --config Release
ctest --test-dir Build/UI -C Release --output-on-failure
python Automation/PackageWindows.py --build Build/UI --config Release --toolchain "MSVC x64"
```

The resulting archive is `Binaries/ControlCentre-Windows-x64.zip`. Extract the complete archive, then run
`FrontierUI.exe`. Fonts, SVGs, inventory, per-family font licences, and third-party notices are bundled.
The native MSVC commands are provided for reproduction; MSVC itself was not available for verification here.

## Cross-toolchain used here

The earlier LLVM-MinGW release-asset download was network-blocked. The PyPI Zig distribution succeeded.
This is a toolchain for generating Windows binaries, not a Linux application deliverable.

```text
python -m pip install ziglang==0.13.0
cmake -S . -B _ClaudeScratch/build/windows-ui -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=Automation/WindowsToolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build _ClaudeScratch/build/windows-ui
python Automation/PackageWindows.py --build _ClaudeScratch/build/windows-ui \
  --toolchain "Zig 0.13.0 / Clang 18.1.6"
```

The Windows toolchain file is for a Linux editing host; Windows users can keep their normal MSVC build.
Zig's bundled Windows headers/import libraries avoid downloading a separate Windows SDK here.
The Windows link uses `d3dcompiler_47`; native MSVC uses its SDK's `d3dcompiler` import library.
ThorVG uses the Windows C++ thread implementation, not a guessed pthread import library.
No GitHub Actions are required or authorised.

## Verification and evidence

Local editing host: Linux, GCC 12.2, Python 3.11, ImGui/ThorVG/toml++ at the locked revisions.
The following host-side tools exercise native C++ UI code; they are not distributed Linux product binaries:

```text
cmake -S . -B _ClaudeScratch/build/ui -G Ninja \
  -DFRONTIER_HOST_VERIFICATION=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build _ClaudeScratch/build/ui
ctest --test-dir _ClaudeScratch/build/ui --output-on-failure
```

Sanitizer configuration additionally uses `-fsanitize=address,undefined -fno-omit-frame-pointer`
and links executables with `-fsanitize=address,undefined`.

| Check                        | Observed result                                      |
|------------------------------|------------------------------------------------------|
| Debug tests                  | 5/5 passed.                                          |
| Address/undefined checks     | 5/5 passed.                                          |
| Font catalogue unit tests    | Stable offline scan, removal, corrupt/modified rejection. |
| Native font discovery        | Four families, 30 faces; no duplication on rescan.     |
| Native pointer interaction   | Font cards, theme tiles, quick-panel collapse/expand. |
| Keyboard shortcut            | Ctrl+, toggles the Control Centre.                   |
| Applied settings             | Actual palette, font face, weight, size, and scale.   |
| Scaled interaction           | Theme pointer coordinates remain correct at 125%.    |
| Persistence                  | Apply/discard, reload by family name, invalid-save preservation. |
| Windows cross-compilation    | Succeeded; Release PE32+ x64 executable linked.       |
| Windows runtime / GPU        | Not run: no Windows runtime/display available.       |

`Images/Fonts.png`, `TypeScale.png`, `Theme.png`, and `LightTheme.png` are actual native ImGui draw commands
rasterized by the inherited exhibit-only CPU fixture. They are not mockups or Windows GPU screenshots.
`Code/ControlCentreValidation.cpp` generates PPM pixels; `Exhibits/UIFoundation/Code/EncodeEvidence.py`
losslessly encodes those pixels as PNG without changing the design.
The CPU rasterization code lives only in this exhibit, never under `Engine/Render/`.

To reproduce package/import inspection, install `pefile==2024.8.26`, then run
`python Exhibits/ControlCentre/Code/InspectWindowsPackage.py`. It verifies ZIP integrity, bundled file hashes,
current source hashes, font catalogue hashes, PE architecture, and Windows-only import dependencies.

Results include Debug/sanitizer logs, the font inventory, Windows executable/import metadata, and source hashes.
The previous UI foundation report is historical; this exhibit describes the current Control Centre batch.
The development executable is unsigned. Cross-compilation and import inspection do not prove Windows startup,
DPI/device behaviour, GPU correctness, or native MSVC compatibility.
