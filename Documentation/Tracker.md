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
| 006 | Windows binary and GPU checks         | Blocked     | Need Windows toolchain and runtime.    |
| 007 | Four skills retained at old paths      | Intentional | Preserve until approved relocation.    |
| 008 | UI foundations and SVGs ported         | CPU checked | Windows integration still unverified.  |
| 009 | Six ImGui patches                     | Checked     | Applied, verified, reverted/reapplied. |
| 010 | Shared activation lifecycle           | CPU checked | Integrate into further scene classes.  |
| 011 | Scene renderer                        | Excluded    | Blank new Render directory by request.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |

## Verification

`Exhibits/UIFoundation/README.md` contains commands, results, evidence, and limitations.
All three local CPU tests passed in Debug and with address/undefined-behaviour sanitizers.
All 151 SVG symbols decode to nontransparent rasters; the image is CPU-generated, not Windows GPU proof.
Windows SDK/MSVC and a Windows runtime are unavailable. An LLVM-MinGW download attempt was blocked by network access.
No Linux product binaries are delivered. No GitHub Actions are authorised.

## Known rule findings

The port preserves existing APIs for this bounded batch; it is not a clean naming/formatting certification.
Examples requiring repair: `ControlKit::AssignTheme`, `VectorGlyphRecord`, short local names, comment vocabulary,
namespace brace layout, header banners, and non-approved decorative glyphs in inherited source comments.
The imported official Windows example retains third-party conventions; its provenance and licence are explicit.
The four mandatory skill documents have not been weakened or edited to waive these findings.

## Scope history

The earlier documentation-only validation script was removed when that phase was clarified.
Subsequent user instructions authorise source porting, local validation code, proof images, and commit/push.
No full baseline replacement or inherited C++ relocation has occurred.
