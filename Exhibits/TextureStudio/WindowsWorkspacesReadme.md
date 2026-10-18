# Windows standalone application tests

Extract the complete archive before launching. Windows 10/11 x64 and an installed Vulkan 1.2-capable GPU driver
are required. Shader binaries are included; no compiler or Vulkan SDK installation is required on the test machine.

| Executable | Scope |
|---|---|
| `SolidArc.exe` | Project Browser only. Create/open/select validates projects but deliberately stays in the browser. |
| `TextureWorkspace.exe` | Material Library, Preview and Inspector only. No painting/pattern/stamp/baking tabs. |

Use the adjacent `RunSolidArc.cmd` / `RunTextureWorkspace.cmd`, or launch either executable directly.
Both resolve bundled engine content relative to their executable. Materials are copied to
`%LOCALAPPDATA%/Frontier/TextureStudio/Materials` before editing so the packaged presets remain unchanged.
Existing personal materials are preserved, not overwritten by an updated test archive.

## Acceptance checklist

- Launch both applications. SolidArc must never enter the general editor/CAD workspace.
- In Texture Workspace select Racing Green and another material; allow the first shader-ball render to complete.
- Use the minus / percentage reset / plus pill and mouse wheel over empty preview space.
- Switch Shader Ball, Sphere, Plane and Cube. Switch Studio Softbox, Warm Courtyard, Cool Rim and Overcast.
- Change Exposure. Only the large preview should change; catalogue thumbnails keep their fixed lighting/geometry.
- Edit a supported coefficient, Apply, Revert and Save. Invalid source must preserve the last valid preview.
- Restart, resize, collapse panels and test a second monitor only if that preference is explicitly enabled.

## Diagnostic collection

Logs: `%LOCALAPPDATA%/Frontier/Logs/SolidArc-*.log` and `TextureWorkspace-*.log`.
If LOCALAPPDATA is unavailable, look in `%TEMP%/Frontier/Logs`. Files are flushed after events, capped at 8 MiB
per run, and retain the latest eight runs per application. Startup paths, font/icon recovery, Vulkan device/driver,
shader byte counts, material operations, preview settings, GPU submissions and severe failures are recorded.

Run `CollectDiagnostics.cmd` to copy these files into a `Diagnostics` folder beside the executables.
Send that folder, `Package.json`, GPU/Windows version, reproduction steps and a screenshot of any error.
Logs contain local file paths but not document source or credentials; review them before sharing.
A native severe-failure dialog remains available when normal rendering fails. Hard termination/power loss cannot
be guaranteed to generate a final entry; compile/package success is not user GPU-driver/startup certification.

## Provenance and limits

`Package.json` records the source revision, payload SHA-256 and shader/source fingerprints. All ten authored Slang
entry points are compiled by CI; runtime binaries reside in `EngineContent/ShaderArchives/Surface`.
The CI CPU/native UI tests do not create an interactive Windows Vulkan window. Runtime GPU/driver acceptance
still requires the checklist above. Seven automotive procedural recipes are implemented; remaining materials
retain the previously documented supported physical subset. Exact reference appearance is not yet certified.
