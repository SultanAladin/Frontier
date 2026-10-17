# Selective UI Port — Source Notices

Imported Frontier source and SVGs originate from `SultanAladin/Frontier-` at
`338293863d7df4e585dec0c4acae45fe9c524ed5`. Exact imported paths and hashes are in `ImportManifest.json`.
No project-wide licence was present in that imported Frontier snapshot. This port does not invent a licence grant.
The owner must establish applicable permissions before external redistribution or relicensing.

Original icon attribution and compatibility notes remain in `UI/Assets/Icons/Slate-NOTICE.md` and
`UI/Assets/Icons/ThorVG/manifest.json`. These are inherited provenance records, not local regeneration instructions:
referenced upstream gallery scripts and sources have not all been ported. Manifest reference paths identify
upstream originals; they are not promises of files in this checkout.

Lucide-related paths retain the upstream ISC notice in `Engine/Interface/Lucide.NOTICE`.
ImGui is pinned at `3bae66c735670619baf51391eba7f3d90a25d125` with its MIT licence.
ThorVG is pinned at `6715f99ac106b6d4587f384a78c59fe957cfd2a3` with its MIT licence.
Downloaded archives preserve their licence files. The adapted official Win32/DX11 exhibit retains ImGui's
licence at `Exhibits/UIFoundation/Code/ImGui.LICENSE`; dependency notices are included by Windows installation.
Third-party code is not represented as original Frontier work.

The inherited six ImGui patches were originally cut against `83f6686`; they apply cleanly to the locked `3bae66c`
pin used here. ThorVG's staged initializer adjustment is inherited and leaves its downloaded source untouched.

## Control Centre and fonts

The subsequent appearance port adapts `AppearanceInspector`, `FontCodec`, and `TypefaceRegistry` from the same baseline.
The exhibit-only CPU command rasterization fixture originates from `Exhibits/Workbench/IconArt/CpuDraw.h` there.
The UI-only Control Centre deliberately excludes the old host's renderer/material/fidelity dependencies.

Font downloads are unmodified Inter, Archivo, Space Grotesk, and JetBrains Mono static faces from that baseline.
`Automation/FontSources.json` pins each font and licence file by Git blob and SHA-256.
Each family's SIL Open Font License 1.1 notice is retained beside the downloaded fonts and in the Windows bundle.
General Sans and Clash Display are not fetched as part of this open-font selection.
Toml++ is pinned in `Automation/UIDependencies.json`; its MIT licence is retained with the download and installation.
Zig 0.13.0 is used only as the cross-toolchain; its downloaded distribution stays outside Git in ignored scratch.
No source licence is replaced or extended by the generated binary package.
