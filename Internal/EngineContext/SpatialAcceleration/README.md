# SpatialAcceleration/

Spatial acceleration structures and broadphase queries for the engine — the partitioning that turns
"test against everything" into "test against the few that matter." Built on the world conventions
from `../MetricSpace/` (right-handed, Z-up, metres) and the float types from
`../Math/LinearAlgebra_Float32.h`; struct + free-function house style, no glm.

## Status

**Placeholder — no source yet.** This directory is reserved; nothing is ported here today.

## Planned contents

| Component | Owns |
|---|---|
| `SpatialPartition.{h,cpp}` | Octree / BVH broadphase partitioning over `SpatialExtent` regions. |
| Toroidal structures | Wrap-around (periodic) spatial layout for streaming / infinite domains. |

Both are future work; this note marks the home so the folder is not mistaken for missing.
