# VirtualizedGeometry — C++ port evidence

## Scope

C++ port of the HTML virtualized-geometry implementation, per
`Documentation/Plans/Ongoing/VirtualizedGeometryCppPort.md`. This exhibit holds the headless
validation for Division 1 (geometry kernel, `Engine/Geometry/`). Rendered evidence arrives with
Division 2 (`Engine/Render/`).

## K1a — FacetTierValidation

- Code: `Code/FacetTierValidation.cpp` against `Engine/Geometry/FacetTierBuild.{h,cpp}`
  (weld by exact position, edge incidence, UV-seam/sharp classification, ≤128-triangle connected
  class-pure cluster partition with bounds sphere + normal cone, invariant validator).
- Corpora: subdivided icosphere (5120 facets) and heightfield cliff (8192 facets), same
  constructions as the HTML reference corpus.
- Result (2026-10-22, sandbox g++ 12.2, `-std=c++20 -O2 -Wall -Wextra`): **PASS**
  - sphere: 2562 welded vertices, 63 clusters, 0 sharp / 0 boundary (closed smooth solid)
  - heightfield: 4225 welded vertices, 87 clusters, 214 sharp crease edges, 256 boundary edges
  - invariants: coverage exactly once, cap, connectivity, class purity, sphere containment
- Reproduce: `g++ -std=c++20 -O2 -I. Exhibits/VirtualizedGeometry/Code/FacetTierValidation.cpp
  Engine/Geometry/FacetTierBuild.cpp -o FacetTierValidation && ./FacetTierValidation`
  or via CMake: `-DFRONTIER_HOST_VERIFICATION=ON`, test name `FacetTierValidation`.
- Not run here: Windows/MSVC build (no Windows host in this environment); Vulkan rendering
  (Division 2, no GPU/Vulkan in this environment).
