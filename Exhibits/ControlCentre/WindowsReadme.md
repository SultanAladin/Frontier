# Frontier — Windows Control Centre Exhibit

## Run

- Requires Windows 10/11 x64 with a Vulkan 1.1-capable driver and the Windows Universal CRT.
- Extract the entire archive into a writable directory, not just the `.exe`.
- Run `FrontierUI.exe`. It uses GLFW window/input and the ImGui Vulkan display adapter.
- This is an unsigned development exhibit, not a signed production release.
- The executable was cross-compiled and inspected as Windows x64, but not executed on Windows here.
- No scene renderer, GI, render-quality, resolution, AA, or shadow controls are included.

## Controls

- Starts with a top-edge trapezoidal expansion tab. Tap it or pull down to open.
- Slide the bottom grip, card header, or quick-card blank space upward to close. Escape also closes.
- Drag the closed tab sideways to reposition it.
- Fonts live under `EngineContent/FontArchives/<font name>/`; SVGs under `EngineContent/Icons/`.
- GLFW owns all OS windows. Vulkan displays the editor UI; no scene renderer has been imported.
- `FrontierUIDX11.exe` is the separate Direct3D 11 comparison exhibit; it is not the default editor.
- Open the Settings button, then Appearance for Typography, Theme and interface scale.
- `Ctrl+,` toggles the main Control Centre.
- Font cards, available weight chips, and size sliders edit a draft.
- Theme tiles, accent swatches, semantic colours, and corner radius edit a draft.
- Apply saves preferences; Discard restores the applied preferences.
- Scroll the main content to see the complete typography and theme sections.

Personal preferences are stored in `%LOCALAPPDATA%/Frontier/Personal.projectspace` when available.
If it is unavailable, preferences apply for the current session only.
Fonts are restored by family name, not a fragile numerical position in the folder inventory.

## Content Browser

- Tap the bottom Content Browser tab or pull it upward; drag its grip/header downward to dismiss.
- Single click selects files/folders; double click or Enter opens folders. Arrow keys change selection.
- Back/Forward/Up/Root/Reload, filename search, file-type categories and Grid/List operate on actual EngineContent.
- Search is folder-local: Enter finishes editing; Escape restores the pre-edit query. Ctrl+F focuses search.
- Maximize/Restore changes open height; narrow layouts use Size. Type filters keep folders navigable.
- Details show filenames, relative paths and byte counts. Type glyphs are not content thumbnails.
- Browsing is read-only: no import/delete, scene/material runtime, project-root picker or format conversion.
- The top Control Centre takes input priority while open. Both panels share fonts, themes and control styling.

## Content and licences

The bundle contains Inter, Archivo, Space Grotesk, and JetBrains Mono: 30 static faces.
Each font family includes its original SIL Open Font License notice.
`EngineContent/FontArchives/FontCatalogue.toml` lists every bundled face with weight and SHA-256.
Original and inherited compatibility SVGs are in `EngineContent/Icons/`.
Compatibility differences are retained in the SVG manifest; these are not exact browser-parity claims.
Dependency notices are in `Notices/`; retained artwork notices are beside the SVGs.
No new licence grant is asserted for inherited Frontier or Slate artwork.

`Package.json` records the toolchain, source status, and hashes of bundled files.
Source, automated tests, and CPU-generated UI images are retained in the repository exhibits.

## Project configuration

```text
FrontierUI.exe --create-project "C:\Projects\Example\Example.projectspace"
FrontierUI.exe --project "C:\Projects\Example\Example.projectspace"
```

The installed `EngineContent/Defaults/BlankProject/Blank.projectspace` seeds a fresh-identity project without replacing
existing files. Appearance, input, notification and engine settings are embedded in that one editable container.
The content roots are created; scene/dependency loading is not implemented. Creation needs filesystem hard-link support.
Personal/Project chips explicitly choose where settings are saved. Personal overrides can be switched On/Off once saved.
No new Appearance/Input/Notifications/Engine TOML sidecars are written. The editor container uses TOML internally,
not binary FSPC. The old standalone TOML APIs remain compatible but are not automatically migrated by this host.
Notification preferences persist. Cards appear top-right, with Notification Stream in Quick Controls (split wide,
stacked narrow). Save/Apply opens a confirmation popup: Cancel/Escape keep the draft, Apply performs the actual save.
The shared top/bottom tabs are half-size; Invert Y-Axis is a plain switch. Independent severe-error UI remains pending.

## Workspace and panel hosting

Settings → Workspace selects Content Browser: Draggable (default) or Dockable. Both presentations share the same
browser state; Apply/Cancel confirms changes, saved only in Personal.projectspace. In Dockable mode `+` reopens
closed tabs; Notification Stream is also available. The `+` follows the final tab directly and is centred.
Trapezoidal tabs use the darkest named theme surface with opaque fill.
Optional detached OS windows remain disabled by default. Windows runtime/multi-monitor verification, saved layouts,
monitor recovery and Vulkan platform-window rendering are still pending; do not infer runtime success from a build.

## Detached windows and monitor recovery

Settings → Workspace → Allow detached OS windows is optional and disabled by default.
Enable/Apply, then use the native tab context menu to detach or dock back. GLFW owns secondary OS windows;
the pinned ImGui Vulkan adapter owns their swapchains and render/present callbacks.
Personal `.projectspace` retains layout, monitor work-area and density metadata; no ImGui INI is written.
Saved floating rectangles reconcile density and fit current work areas. Work-area changes trigger live recovery;
minimized secondary windows defer recovery until restored. Physical mixed-DPI and monitor-hotplug operation has not
been executed in the cross-compilation environment. Driver/GPU failures use a native error-dialog fallback.
