# Project Tracker

Single progress and violation tracker. Detailed evidence belongs in Exhibits, not competing progress reports.

## EngineUpdate status

| ID  | Finding or task                        | Status      | Next action                            |
|-----|----------------------------------------|-------------|----------------------------------------|
| 001 | Checkout differs from source baseline  | Partial     | UI imported; rest deliberately absent. |
| 002 | Root agent rules used obsolete workflow| Implemented | Keep entry instructions consistent.    |
| 003 | Prototypes live in Documentation       | Open        | Inventory paths and launcher links.    |
| 004 | Inherited Internal tree remains        | Open        | Audit ownership before further ports.  |
| 005 | Old maps/backlog conflict              | Partial     | Historical pointers added; reconcile.  |
| 006 | Windows binary and GPU checks         | Partial     | Cross-built; Windows runtime pending.  |
| 007 | Four skills retained at old paths      | Intentional | Preserve until approved relocation.    |
| 008 | UI foundations and SVGs ported         | CPU checked | Windows integration still unverified.  |
| 009 | Six ImGui patches                     | Checked     | Applied, verified, reverted/reapplied. |
| 010 | Shared activation lifecycle           | CPU checked | Integrate into further scene classes.  |
| 011 | Scene renderer                        | Excluded    | Blank new Render directory by request.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |
| 014 | Control Centre and quick panel        | CPU checked | Windows interaction validation next.  |
| 015 | Open fonts and catalogue              | Checked     | Four families, 30 faces, OFL retained. |
| 016 | Appearance persistence                | Checked     | Reload and invalid-save checks passed.|

## Verification

`Exhibits/ControlCentre/README.md` contains current commands, results, evidence, and limitations.
All five local tests passed in Debug and with address/undefined-behaviour sanitizers.
All 151 SVG symbols remain ready. Native Control Centre images are CPU raster output, not Windows GPU captures.
Windows x64 cross-compilation succeeded using Zig 0.13.0 / Clang 18.1.6 obtained through PyPI.
The unsigned Windows executable is packaged with fonts, SVGs, licences, and integrity metadata under ignored Binaries.
Native MSVC and Windows runtime/GPU execution are still unavailable. No Linux product binary is delivered.
No GitHub Actions are authorised.

## Known rule findings

The port preserves existing APIs for this bounded batch; it is not a clean naming/formatting certification.
Examples requiring repair: `VectorGlyphRecord`, short local names, comment vocabulary,
namespace brace layout, header banners, and non-approved decorative glyphs in inherited source comments.
The imported official Windows example retains third-party conventions; its provenance and licence are explicit.
The four mandatory skill documents have not been weakened or edited to waive these findings.

## Scope history

The earlier documentation-only validation script was removed when that phase was clarified.
Subsequent user instructions authorise source porting, local validation code, proof images, and commit/push.
No full baseline replacement or inherited C++ relocation has occurred.

The selected theme/control configuration APIs now use `Configure...` rather than the banned `Assign...` verbs.
The new appearance files use Allman braces, flat namespace indentation, and descriptive file headers.
