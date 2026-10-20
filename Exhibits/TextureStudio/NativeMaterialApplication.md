# Native material application status

Pattern-to-PBR scope is approximately 95% complete. The native Pattern panel is reachable from Texture Workspace,
with GPU coverage/selection, TOML save/load and independent dockspace layout. Float32 map-to-material application is
still open.

## Complete in this increment

- Seven typed RGBA32F GPU surface maps with per-layer contribution masks.
- Authored channel isolation, including distinct golden-thread and fabric metalness.
- Shared little-endian Float32 PFM encoding and a verified 14-file fixture archive.
- Strict and sanitizer Vulkan readback validation against SwiftShader, including the 512×512 retired-slot cache path
  and repeated equivalent-program selection.
- Reconciled dropdown and dockspace APIs; the native host now toggles between Material and Pattern, saves/opens a
  separate native vector TOML file, and drives the Pattern canvas with Vulkan coverage and selection pipelines.
- The C++ Pattern validation now passes 619 checks with 342 GPU coverage dispatches and 63 selection dispatches,
  including per-shape Roughness toggling, independent contribution compilation, and Undo/Redo. Multi-selection toggles
  apply only to selected shapes. This runner did not execute a Windows desktop session (`windows_executed: false`).
- `VectorSurfaceValidation` separately confirms on SwiftShader that disabling dielectric-fabric roughness removes that
  shape's GPU contribution without changing the golden thread's roughness.

## Current phase: web Pattern geometry import

- Added `Automation/ConvertWebPattern.mjs`, a fail-closed converter from normalized web Pattern JSON into native vector-authoring TOML. It covers supported primitive/path geometry, transforms, literal solid paint, path fill rules, stitch/weave roles, and channel contributions; four Node tests currently exercise conversion and rejection cases.
- This is only the first importer increment. Masks, embedded assets/SVG/text, linked/gradient paints, folders/instances, repeat-edge wrapping, Pattern finish roughness/metalness, and complete document semantics are still unsupported. Stamp remains deferred. See `PatternJsonNativeImport.md` for the exact scope.

## Remaining gates

- The engine bridge passes its SwiftShader dispatch/readback/upload/sample regression. The native Texture Workspace now routes
  the 512x512 Pattern document through the bridge in its shared Vulkan submission and places retired Float32 maps into a
  non-persistent live material preview. The Windows x64 executable cross-links, but has not run on Windows.
- Capture and validate that full host submission and displayed sampling path before enabling persistent or scene-level
  Apply-to-material controls. The native C++ GIF records the renderer fixture, not the interactive Pattern window; native
  Windows GPU execution is still absent.
- Calibrate tangent orientation and physical height units; current authored height is normalized and must not be
  presented as metres or as silhouette displacement.
- Verify full-size 512x512 maps, per-shape edits, undo/redo, separate gold-thread response and persistent material
  application on an actual native capture.

The downloadable PFM archive contains a 32x32 synthetic validation fixture, not a user's document. Its GIF is 8-bit
visual evidence; only the numerical GPU tests establish Float32 behavior.
