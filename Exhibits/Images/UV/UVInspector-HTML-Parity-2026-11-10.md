# UV Inspector HTML parity — 10 November 2026

## Completed in this pass

- Rendered and saved the HTML reference states for Unwrap, UDIM, Shading, and Check (`.reference-*.png`).
- Updated the native Unwrap renderer with transform controls, unwrap/stacking/projection actions, angle threshold, Live Unwrap, and seam operations.
- Rebuilt the UDIM tab around independent metric cards, tile controls, resolution selection, and backdrop presets (Color, UV Code, Checker, Slate).
- Rebuilt Check around separate overlap/flipped/stretch KPIs and UV integrity/repair actions.
- Reworked Shading to use the reference's material hero and 2×2 telemetry cards, followed by native MatCap and material-finish controls.
- Corrected the Inspector header/body vertical rhythm and replaced unsupported text glyphs in Shading KPIs with repository vector icons.
- Captured the native states in `UVInspector-{Unwrap,UDIM,Shading,Check}-native.png`.

## Verification

`cmake --build build/Development --target FrontierUV -j2` succeeds. Each of the four native tab states was separately selected, built, and captured; the session header was restored to its default Packing tab afterward. `git diff --check` is clean. No GitHub Actions were run.

## Remaining phases

1. Finish exact Shading feature parity where the native backend differs from HTML: shader-mode choices and material colour/sample/apply controls need native state/API support, rather than visual-only controls.
2. Align KPI metric semantics (HTML angle-stretch and texel-density values versus the native GPU summary) and do a final pixel-level spacing/type pass.
3. Continue the remaining UV workspace parity work, then run the project's broader validation suite before release.
