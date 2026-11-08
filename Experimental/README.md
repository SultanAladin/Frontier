# Experimental

Requested prototypes, experimental HTML, scripts, styles, assets, and launchers belong together here.
Do not place reusable production code or permanent validation reports here.
The approved C++ editor comparison mirror belongs in `Exhibits/EditorMirror/`.
Earlier prototypes are retained under `Documentation/Plans/References/InheritedEngine/Prototypes/`;
new prototypes belong here. Review launcher and asset references before relocating anything.

## Landscape workspace

[`LandscapeWorkspace/`](LandscapeWorkspace/README.md): vector-mask authoring, scoped XYZ terrain stacks,
ProjectZero presentation, independent region resolutions, persistent Clay/Carve sculpt strokes, always-high
field-refined geometry and bounded native-pitch working areas. GPU-only rasterization has no software fallback;
water stays world-space and sparse exports preserve the 3D field. Actual captures and reproducible checks are in
Exhibits. Focused sculpt checks pass; the broad editor GPU fence failure remains unresolved. Smooth/Flatten,
simulation and complete volume paging are not implemented.
