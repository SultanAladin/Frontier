# 🧩 `Experimental/` — Reference Studios Pending Native C++20 Conversion

> **TL;DR**: `Experimental/` stages **eight interactive web/WebGPU/WebGL2 reference studios** extracted from prior Frontier prototype branches. None of these JavaScript prototypes ship in the production runtime; each serves as the verified UX, mathematical, and shader specification for native **ISO C++20 + `.slang` GPU** modules inside `Engine/` and `Editor/` (`FRONTIER_PROFILE_EDITOR`).

---

## 🏷️ 1. Staged Reference Studios & C++20 Conversion Targets

| Studio Directory | Prototype Scope | Provenance | Target Native C++20 / `.slang` Destination |
| :--- | :--- | :--- | :--- |
| **`FluidEditor/`** | Gaseous plume, combustion, detonation, wildfire, and vortex solver (`WebGPU` + `WebGL2`) with `.gasscene.toml` serialization and flipbook baking. | `Frontier-OLD` @ `438aeaf` | `Engine/Physics/` (fluid/gas solver) + `Shaders/` (`.slang` compute advection & raymarch) + `Engine/Editor/` |
| **`FractureEditor/`** | Six fracture & debris solvers: baked Voronoi fracture, fast runtime shattering, convex partitioning, piece SDFs, rigid-body dynamics, and atmospheric drag. | `Frontier-OLD` @ `438aeaf` | `Engine/Geometry/` (Voronoi/convex partitioning) + `Engine/Physics/` (`JoltPhysics` rigid bodies) + `Engine/Editor/` |
| **`ParticleEditor/`** | GPU compute particle emitters, fibres/ribbons, procedural lightning, and live spatial force-field sampling (**Particle Studio**). | `Frontier-OLD` @ `438aeaf` | `Engine/Render/` & `Shaders/` (`.slang` GPU compute simulation & indirect draw) + `Engine/Editor/` |
| **`PhotometricEditor/`** | LM-63 `.ies` luminaire profiles, automotive headlamp optics, celestial sky scattering, ACES tone mapping, and isolux metering (**Photometric Studio**). | `Frontier-OLD` @ `438aeaf` | `Engine/Render/` (photometric light evaluation & sky LUTs in `.slang`) + `Engine/Editor/` |
| **`ForceFieldEditor/`** | Global wind synthesis + 16 bounded spatial force fields with RK2 streamline tracing and GPU vector/bounds overlays (**Force Field Studio**). | `Frontier-OLD` @ `438aeaf` | `Engine/Physics/` & `Shaders/` (shared `.slang` spatial force-field evaluation & GPU debug overlays) |
| **`VehicleLab/`** | `ControlVehicle` `FCT1` mesh inspection, VR38DETT torque & turbo spool curves, Pacejka MF6.1 tyre thermal/slip solver, and 10 telemetry channels. | `Frontier-OLD` @ `438aeaf` | `Engine/Physics/` (`JoltPhysics` wheeled vehicle + Pacejka/thermal solver) + `Engine/Assets/` + `Engine/Editor/` |
| **`UVEditor/`** | Synchronized 2D UDIM layout & 3D mesh viewport, LSCM conformal unwrapping, seam authoring, GPU wireframe overlays, and importance-weighted packing. | `c7egoist/Frontier` @ `3350774` | `Engine/Geometry/` (LSCM unwrap & UDIM packer) + `Shaders/` (GPU UV/wireframe overlays) + `Engine/Editor/` |
| **`TextureStudio/`** | Five-workspace texturing suite: `#material` look-dev, `#pattern` noise-scaled layer stack, `#stamp` decals, `#paint` 3D brush projection, and `#bake` mesh + volume SDF baking. | `c7egoist/Frontier` @ `3350774` | `Engine/Materials/`, `Engine/Assets/`, & `Shaders/` (`.slang` procedural noise, brush projection, AO/curvature/SDF bakers) + `Engine/Editor/` |

---

## 🧩 2. Shared Prototype Support Directories

| Path | Role |
| :--- | :--- |
| **`ForceFieldVisualization/`** | Shared 3D force-field glyph and streamline builder consumed by `ParticleEditor/` and `ForceFieldEditor/`. |
| **`Shared/UvTextureBridge.js`** | Cross-studio UV layout & mesh transfer bridge connecting `UVEditor/` and `TextureStudio/`. |
| **`VehicleProvingGrounds/{Archive,Source}/`** | Binary `FCT1` `ControlVehicle` body/rim/tyre streams and authored powertrain/Pacejka/thermal sheets loaded by `VehicleLab/`. |
| **`UI_COMPONENT_GUIDELINES.md`** | Visual layout, typography, and custom inspector widget specification shared across the reference studios. |

---

## 🔍 3. Local Preview & Automated Verification

```bash
# Serve all 8 Experimental studios simultaneously on http://0.0.0.0:8080/Experimental/
python3 Experimental/Serve.py 8080

# Run standalone Node ESM validation suites across all 8 staged studios
node Experimental/FluidEditor/src/SceneMetrics.mjs
node Experimental/FractureEditor/FractureValidation.mjs
node Experimental/ParticleEditor/Engine/CheckForceFields.mjs
node Experimental/PhotometricEditor/PhotometricValidation.mjs
node Experimental/ForceFieldEditor/ForceFieldValidation.mjs
node Experimental/VehicleLab/AssetInspectorValidation.mjs
node Experimental/UVEditor/UVValidation.mjs
node Experimental/TextureStudio/TextureStudioValidation.mjs
```
