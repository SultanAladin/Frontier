# Geometry Division evidence

Headless validation for the C++ virtualized-geometry build. Source in `Code/`; the retained transcripts
beside this file are the raw stdout of the runs below.

## Reproduction

```sh
g++ -std=c++20 -O2 -Wall -Wextra -I. \
    Engine/Geometry/FacetTierBuild.cpp Engine/Geometry/QuadricTierBuild.cpp \
    Exhibits/Geometry/Code/FacetTierValidation.cpp -o FacetTierValidation
./FacetTierValidation

g++ -std=c++20 -O2 -Wall -Wextra -I. \
    Engine/Geometry/FacetTierBuild.cpp Engine/Geometry/QuadricTierBuild.cpp \
    Exhibits/Geometry/Code/TierSequenceValidation.cpp -o TierSequenceValidation
./TierSequenceValidation
```

Sanitizer configuration used for the retained run:

```sh
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -I. \
    Engine/Geometry/FacetTierBuild.cpp Engine/Geometry/QuadricTierBuild.cpp \
    Exhibits/Geometry/Code/TierSequenceValidation.cpp -o TierSequenceValidationSan
./TierSequenceValidationSan
```

## Environment

| Item | Value |
|---|---|
| Platform | Linux x86-64 sandbox (Debian 12) |
| Compiler | g++ 12.2.0, `-std=c++20 -Wall -Wextra` (clean) |
| Revision | branch `arena/08de38f7-frontier`, geometry division A0-A2 |
| GPU | none used; the whole division is CPU and standard library only |
| CMake / ctest | not available in this sandbox; the same targets are registered in `CMakeLists.txt` (`FrontierGeometry`, `FacetTierValidation`, `TierSequenceValidation`) and remain unverified on Windows |

## Results

| Corpus | Source facets | Root facets | Tiers | Checks |
|---|---|---|---|---|
| Icosphere, 4 subdivisions | 5120 | 94 (1 cluster) | 7 | PASS |
| Heightfield cliff 64▲2 | 8192 | 244 (2 clusters) | 13 | PASS |
| UV-creased vessel 96x32 | 6144 | 197 (7 clusters) | 11 | PASS |

Per-tier tables, deviations and the invariant list are in `TierSequenceValidation.txt`.

### What the checks assert

- Every tier covers each of its facets exactly once, within the 128-facet cluster cap, edge-connected,
  classification-pure, with every corner inside the cluster bounds sphere.
- Facet count strictly decreases between tiers and the sequence ends at the fixed point of coarsening.
- Cluster deviation never decreases with depth, and a coarser bounds sphere encloses every finer sphere it links.
- Every finer cluster has at least one coarser home.
- The UV meridian seam is never crossed by a facet at any tier of the vessel.
- The vessel rim ring stays planar and no vertex rises above the authored rim.

## Honest limits

- An open mesh with a pinned border cannot coarsen to a single 128-facet cluster; the heightfield settles at
  244 facets in two clusters and the vessel at 197 in seven. Every individual cluster is still inside the cap.
  The tier cut selector (A3) handles several root clusters; the earlier plan wording "one root cluster" was
  too strong and has been corrected.
- `Deviation` is a conservative travelled-distance bound, not measured Hausdorff error. The sphere's actual
  maximum radial error at the root tier is 4.6 % (reported in the log as `max-error`), while its deviation
  bound is 0.40 m on a unit sphere.
- The HTML per-level counts (sphere 5120 -> 2558 -> 1280 -> 640 -> 320 -> 160 -> 80) are **not** reproduced
  exactly: this build lands on 5120 -> 2926 -> 1510 -> 754 -> 376 -> 188 -> 94. Grouping and lock policy differ.
  The guarantees that matter for the renderer — monotone error, capped clusters, exact coverage, unbroken
  seams — hold and are asserted.
- No rendered image is claimed here. Division 2 owns pixels; Division 1 owns numbers.
