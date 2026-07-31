# Frontier — Folder Structure Plan

Six-pillar engine under `Internal/`. Coordinator suffix = `…Extension`. Editors are separate
executables. A WorkspaceDocument = one tab = one editable authoring session (Simulation is
document-less). Naming follows CLAUDE.md + SKILL-Naming.md (banned: Application, Division,
System-coordinator, Core, mesh, shell, binding, Stage, Trial, harness, Entity/Node/Parent/Child…).

```
Frontier/                                          ← C:\Users\OS\Documents\Projects\Frontier
│
├── Build.bat                                       // Root build entry → Automation/BuildPlan.ps1
├── CLAUDE.md   README.md   .gitignore   .claudeignore
│
├── Automation/                                     // Build orchestration (run via PowerShell only)
│   ├── BuildPlan.ps1                               // Compile/link driver invoked by every Build.bat
│   ├── ShaderPlan.ps1                              // glslc → .spv for every Internal/**/Shaders source. Stale-gated; owns
│   │                                               //   the per-shader --target-env and the two-variant route defines
│   └── ValidationRun.ps1                           // NEW: launch every …Validation target, collect pass/fail
│
├── ExternalPackages/                               // Vendored deps (git-ignored, .claudeignore'd)
│   └── (Vulkan, volk, VMA, glm, imgui, slang, SPIRV-Reflect, stb, MikkTSpace,
│        thorvg, tomlpp, tracy, xxHash, imgui-node-editor)
│
├── EngineContent/                                  // Shared runtime assets (fonts, default themes, icons)
│
├── Build/                                          // Intermediate build output (git-ignored)
│   ├── obj/                                        // Per-translation-unit .obj
│   └── lib/                                        // Static libs per pillar
│
├── Internal/                                       // ★ THE ENGINE — pillars
│   │
│   ├── EngineContext/                              // ══ pillar 1 — foundation + shared runtime services ══
│   │   │
│   │   ├── EngineHost/                             // engine lifecycle (was Root/; "Core" banned)
│   │   │   ├── EngineHost.{h,cpp}                  // 🟢 initialize → loop → finalize (was RootExtension)
│   │   │   ├── ExtensionRegistry.{h,cpp}           // 🟢 registry of active extensions
│   │   │   ├── TimingExtension.{h,cpp}             // 🟢 delta accumulation
│   │   │   └── MemorySegment.{h,cpp}               // 🧩 contiguous block provisioning
│   │   │
│   │   ├── Configuration/
│   │   │   ├── EngineConfiguration.{h,cpp}
│   │   │   └── RenderConfiguration.{h,cpp}
│   │   │
│   │   ├── Math/
│   │   │   ├── LinearAlgebra.h                     // (live)
│   │   │   ├── VectorAlgebra.{h,cpp}              // ← retired EngineInfrastructure/Math
│   │   │   ├── IntersectionSolver.{h,cpp}          // ← retired
│   │   │   ├── MatrixOperations.{h,cpp}
│   │   │   └── QuaternionRotation.{h,cpp}
│   │   │
│   │   ├── MetricSpace/                            // (live) spatial coordinate primitives
│   │   │   └── CoordinateSpace.h  CoordinateProjection.h  DistanceMetric.h  SpatialExtent.h  README.md
│   │   │
│   │   ├── SpatialAcceleration/                    // (live) octree / BVH partitioning
│   │   │   ├── ToroidalAddressing.h                // 🟢 HEADER-ONLY scroll arithmetic — positive modulo, floor-to-cell, exposed-strip
│   │   │   │                                       //    solve. Shared by the WORLD-space GI field and the LIGHT-space sun window; a
│   │   │   │                                       //    sibling .cpp would link-fail the two Validation targets that gate this math
│   │   │   ├── ToroidalClipmapField.{h,cpp}        // camera-tracked 3D WORLD-space voxel clipmap (GI probes only — not the sun's)
│   │   │   └── SpatialPartition.{h,cpp}  README.md
│   │   │
│   │   ├── MicroUtils/                             // cross-pillar helpers — CANONICAL shared identity/store utils
│   │   │   ├── IdentifierAllocation.{h,cpp}  ColorConversion.{h,cpp}  StringFormatting.{h,cpp}
│   │   │   ├── HashComputation.{h,cpp}  RangeArithmetic.{h,cpp}
│   │   │   ├── VacancyTable.{h,cpp}                // ★ canonical (Scene + Revision include this)
│   │   │   ├── RecordToken.h                        // ★ canonical generational identity value (shared by every store)
│   │   │   ├── TokenIssuer.{h,cpp}                 // ★ canonical (templated on the store; mint/revoke over parallel arrays)
│   │   │   └── TokenAuthentication.{h,cpp}         // ★ canonical (templated on the store; stale-token guard + ResolveEntry)
│   │   │
│   │   ├── Motion/                                 // shared animation math (was Interface/Motion)
│   │   │   ├── MotionExtension.{h,cpp}  MotionEvaluator.{h,cpp}
│   │   │   ├── Interpolation/  ScalarInterpolation, VectorInterpolation, ColorInterpolation
│   │   │   ├── Easing/         EasingProfile, BezierEasing, EaseTable   // EaseTable replaces "Tween"
│   │   │   ├── Dynamics/       SpringDynamics, InertiaDynamics, DampingDynamics
│   │   │   └── Sequencing/     TransitionDescriptor, SequenceAnimator, TimelineAnimator
│   │   │
│   │   ├── Input/                                  // ALL input — mechanic + routing (nothing in Interface)
│   │   │   ├── InputExtension.{h,cpp}              // 🟢 raw events → input maps → routed actions
│   │   │   ├── InputSnapshot.h                     // (live) per-frame input state
│   │   │   ├── InputMap.{h,cpp}  InputMapRegistry.{h,cpp}  InputPreset.{h,cpp}  InputContext.{h,cpp}
│   │   │   ├── KeyCombination.{h,cpp}  GestureSpecification.{h,cpp}
│   │   │   ├── Routing/  OperationRouter, ActionInvoker, ModeActivator, ToolEngager
│   │   │   └── Build.bat
│   │   │
│   │   ├── Audio/                                  // shared audio service — 📝 PLANNED, see PLAN-Audio.md (no source ported yet)
│   │   │   ├── PLAN-Audio.md                       // 🧩 authoritative engine audio design (build blueprint)
│   │   │   ├── AudioExtension.{h,cpp}              // 🧩 device open, mix advance, per-cycle drive
│   │   │   ├── AudioConfiguration.{h,cpp}          // 🧩 sample rate, channel layout, buffer size
│   │   │   ├── AudioDevice.{h,cpp}                 // 🧩 backend output stream ownership
│   │   │   ├── SoundSource.{h,cpp}                 // 🧩 one playable source (gain, pitch, loop)
│   │   │   ├── SpatialEmitter.{h,cpp}              // 🧩 3D-positioned source (attenuation, panning)
│   │   │   ├── AudioListener.{h,cpp}               // 🧩 listener pose (from active camera)
│   │   │   ├── MixOrder.{h,cpp}                    // 🧩 bus/submix ordering (not "graph"/"chain")
│   │   │   ├── AudioSampleStore.{h,cpp}            // 🧩 loaded PCM/streamed clip store
│   │   │   └── Build.bat
│   │   │
│   │   ├── Scene/                                  // shared world tree (every editor uses it)
│   │   │   ├── SceneExtension.{h,cpp}              // 🟢 scene assembly + update
│   │   │   ├── SceneDirectory.{h,cpp}             // owning tree container
│   │   │   ├── RecordEntry.{h,cpp}               // one tree item (never Node/Entity)
│   │   │   ├── RecordArchive.{h,cpp}             // dense store — was RecordStore (VacancyTable/TokenIssuer/RecordToken ← MicroUtils)
│   │   │   └── AdjacencyTable.{h,cpp}            // connectivity store (folds EnclosureAtlas + LateralTraversal)
│   │   │
│   │   └── Navigation/                             // camera + ViewCube (shared view control)
│   │       ├── Camera/
│   │       │   ├── CameraConfiguration.h
│   │       │   ├── CameraNavigation/  CameraNavigation
│   │       │   ├── CameraConstraint/  DistanceBoundary, PitchBoundary
│   │       │   ├── CameraProjection/  ProjectionEvaluation, FrustumBoundary, ViewFrameEvaluation
│   │       │   └── CameraTransition/  CameraEaseTable, CameraPoseInterpolation   // calls EngineContext/Motion
│   │       ├── ViewportOrientationManipulator/     // (live ViewCube)
│   │       │   ├── ViewportOrientationManipulator.{h,cpp}  OrientationManipulatorContext.h
│   │       │   ├── Configuration/ ManipulatorConfiguration.h, AlignmentPreset.h, SphericalCoordinateTable.h
│   │       │   ├── Intersection/  RayIntersectionSolver.h, IntersectionResult.h, DepthEvaluation.h
│   │       │   ├── Projection/    LabelCoordinateProjection.h, LabelAlignment.h, TransformAlignment.h
│   │       │   ├── Topology/      ManipulatorPartition.h, SurfacePatchDefinition.h
│   │       │   └── AuxiliaryTriggers/ CameraResetTrigger.h, ProjectionModeToggle.h
│   │       └── Build.bat
│   │
│   ├── Platform/                                   // ══ pillar 1b — OS backend (feeds EngineContext) ══
│   │   ├── Windowing/
│   │   │   ├── PlatformWindow.h                    // (live) abstract contract
│   │   │   ├── PlatformWindowWin32.cpp             // (live) native Win32
│   │   │   ├── PlatformWindowX11.cpp  PlatformWindowWayland.cpp  PlatformWindowStub.cpp   // (live)
│   │   │   └── WindowContext.{h,cpp}              // ← retired
│   │   ├── Concurrency/   WorkerPool.{h,cpp}       // ← retired (job pool)
│   │   ├── Storage/       OpenFileRequest.{h,cpp}, SessionLogPaths.{h,cpp}   // ← retired
│   │   ├── InputProvider/  InputEncoder.{h,cpp}     // raw OS capture (in PlatformWindow) → normalized edges (was "InputBackend"; Backend banned)
│   │   └── Build.bat  README.md
│   │
│   ├── Simulation/                                 // ══ pillar 2 — the DYNAMIC runtime only ══
│   │   ├── Physics/                               // ← retired Authoring/Physics (real rigid-body kernel)
│   │   │   ├── PhysicsExtension.{h,cpp}          // 🟢 loop + broad-phase
│   │   │   ├── Collision/  ColliderShape, NarrowPhaseContact, AnalyticContact, ConvexDistance, ContactManifold
│   │   │   ├── RealTime/   RigidState, ImpulseResponse, OrientationAlgebra
│   │   │   └── PhysicsValidation.cpp             // ← was PhysicsTrial.cpp ("Trial" banned)
│   │   ├── Cloth/                                 // NEW: soft-body / fabric
│   │   │   └── ClothExtension, FabricEnvelope, SpringConstraint, IntegrationSolver, WindEffector
│   │   └── Gameplay/                              // NEW: game-logic execution (document-less)
│   │       └── GameplayExtension, ActorRecord ("Entity" banned), BehaviorDescriptor,
│   │          BehaviorEvaluator, TriggerVolume
│   │
│   ├── Graphics/                                  // ══ pillar 3 — visual output ══
│   │   ├── Render/                                // 🟢 (live) RenderExtension coordinates
│   │   │   ├── RenderExtension.{h,cpp}           // 🟢 (live)
│   │   │   ├── RenderingConfiguration.{h,cpp}    // ← retired
│   │   │   ├── Device/       VulkanHost, VulkanImguiBridge, WindowSubstrate (live)
│   │   │   │                 + DeviceAssembly, CommandStructure, SwapchainDescriptor (← retired)
│   │   │   ├── Resources/    BufferAllocation, LayerTextureAllocation   // ← retired
│   │   │   ├── Submission/   SubmissionOrder, SubmissionDependencyTable  // ← retired (Stage→Submission)
│   │   │   ├── Timing/       FrameTimingLedger, GpuMemoryLedger, CelestialProfileLedger  // ← retired
│   │   │   ├── Viewport/     ViewportTarget   // ← retired
│   │   │   ├── Surface/      PolygonSurface, SilhouetteOutline, SkyBackdrop, CelestialSurface,
│   │   │   │                 HorizonOcclusion, MaterialProperties.h, MatcapProperties.h,
│   │   │   │                 BrightStarCatalogue.h + Shaders/ (SurfaceForward, ChannelGBuffer,
│   │   │   │                 BrdfIntegration, Horizon…, Sky…, PaintUvRaster)   // ← retired
│   │   │   ├── Radiance/     RadianceTarget, RadianceResolveInscription + Shaders/   // 🟢 (live, P5.9b)
│   │   │   │                 linear HDR scene target (R16G16B16A16_SFLOAT) + the ONE tone map
│   │   │   │                 (Khronos PBR Neutral) into the _SRGB swapchain. Scene (sky+shade)
│   │   │   │                 renders here; display-referred overlays draw after the resolve.
│   │   │   ├── Illumination/ DeferredShadePass, DeferredShadeCompositePass, IlluminationSettings,
│   │   │   │                 IlluminationTierConfiguration, SubmissionSchedule + Shaders/
│   │   │   │                 (Reservoir*, ScreenProbe*, Reflection*, DeferredShade*)   // ← retired ReSTIR GI
│   │   │   ├── Shadow/       (← retired + Shaders/)
│   │   │   ├── Outline/      ComponentOverlaySurface   // ← retired
│   │   │   ├── Preview/      PreviewStudioTarget + Shaders/   // ← retired
│   │   │   ├── Diagnostics/  DiagnosticArchive   // (live)
│   │   │   ├── Extension/    RenderExtension   // (live — extensible pass registry)
│   │   │   └── Build.bat  README.md
│   │   ├── Atmosphere/                            // (live) Hillaire 2020 sky
│   │   │   ├── SkyAtmosphere.{h,cpp}  AtmosphereProfile.h
│   │   │   └── Shaders/ (Transmittance.comp, MultiScatter.comp, SkyView.frag, SkyDome.{vert,frag},
│   │   │                 FullscreenTriangle.vert, AtmosphereCommon.glsl, TransmittanceLookup.glsl)
│   │   ├── Grid/                                  // 🟢 (live) analytic ground grid — display-referred overlay, drawn after the
│   │   │   │                                      //    radiance resolve; samples scene depth for occlusion (no depth attachment)
│   │   │   ├── GroundGridPass.{h,cpp}  Shaders/ (AnalyticGroundPlane.{vert,frag})
│   │   ├── Shadow/                                // 🟢 (live) virtual sun shadows — replaces the retired SDF bake below
│   │   │   ├── SunShadowClipmap.{h,cpp}           // 2D LIGHT-space tile window, one level per LOD. Scrolls by whole tiles (≤2
│   │   │   │                                      //    exposed strips); a sun that ROTATES invalidates the whole window instead
│   │   │   ├── ShadowPageAtlas.{h,cpp}            // 4096² R32_UINT page pool (256 pages of 256²) + free/cached/used bookkeeping.
│   │   │   │                                      //    24:1 oversubscribed, so eviction is the steady state — coarsest LOD first
│   │   │   ├── ShadowTileStore.{h,cpp}            // the VIRTUAL table above the atlas: one 32-bit word per tile (6144 = 24 KiB).
│   │   │   │                                      //    Bits 0..7 marking-only (atomicOr), 8..31 the allocator's page index. Demand
│   │   │   │                                      //    resets per image; `Update` (staleness) deliberately survives the boundary.
│   │   │   │                                      //    Upload buffer + a 3-slot readback RING (≥ FramesInFlight) — sharing one
│   │   │   │                                      //    staging buffer raced the upload memcpy against the in-flight download copy
│   │   │   ├── ShadowTileMarkingSubmission.{h,cpp} // 🟢 the GPU side: 3 pipelines (S1/S2/S3), the ONE shared 5-binding set they all
│   │   │   │                                      //    agree on, and the binding-3 per-level origin UBO. 🔴 origins refresh AFTER
│   │   │   │                                      //    the clipmap scroll, before any dispatch; S3 dispatches once per level with
│   │   │   │                                      //    DISPATCH_BASE + gl_GlobalInvocationID.z. S2 is optional (needs frag atomics)
│   │   │   └── Shaders/
│   │   │       ├── ShadowTileStore.glsl           // #include body (NOT a compile target — ShaderPlan globs only vert/frag/comp).
│   │   │       │                                  //    Mirrors the bit layout; the .cpp validator asserts it as text, incl. the
│   │   │       │                                  //    SIGN of the toroidal-origin add, which no compiler can check
│   │   │       ├── MarkVisibleShadowPages.comp    \ S1 receiver marking, 8x8 per screen pixel. Reads the ID BUFFER (not depth) for
│   │   │       │                                  //    coverage, unprojects, picks a LOD by distance, raises Used|Direct
│   │   │       ├── ShadowTileTagInscription.{vert,frag}  \ S2 caster staleness, raster into a 32x32 tilemap. Raises Update ONLY.
│   │   │       │                                  //    Sphere proxy (the only bounds that exist), z-pancaked, 1-texel conservative
│   │   │       │                                  //    expansion emulated in the vert stage
│   │   │       └── ShadowTileLevelPropagate.comp  \ S3 fine->coarse demand, local 32x32 = 1024 (Pascal's exact invocation floor).
│   │   │                                          //    Raises Used|Coarse, never Direct. 🔴 ONE DISPATCH PER LEVEL, ascending, with a
│   │   │                                          //    barrier between — no in-shader ordering is possible across workgroups
│   │   ├── DistanceField/                         // ← retired (global SDF sun-shadow bake)
│   │   │   └── GlobalDistanceField, SignedDistanceVolume, DistanceVolumeBaker, BrickResidencyPool,
│   │   │      AsyncTransferRing, BakeOutputTexture, DistanceSlicePreview
│   │   └── Terrain/                               // ← retired (SDF terrain field)
│   │       └── TerrainFieldProgram, TerrainFieldVolume, TerrainFieldPreview
│   │
│   ├── Authoring/                                 // ══ pillar 4 — creation toolkit ══
│   │   ├── Modeling/                              // ← retired (half-edge editable polygons)
│   │   │   ├── ModelingExtension.{h,cpp}         // 🟢 modelling session loop
│   │   │   ├── Geometry/
│   │   │   │   ├── PolygonComplex, PolygonDescriptor, PolygonCluster, VertexField, FaceTriangulation
│   │   │   │   ├── Topology/ PolygonTopologyEdit    Extrude/ PolygonExtrude    Inset/ PolygonInset
│   │   │   │   ├── Bevel/ PolygonBevel    LoopCut/ PolygonLoopCut    Deform/ PolygonDeform
│   │   │   │   ├── Modifier/ ModifierStack, SubdivideCatmullClark, SubdivideSimple
│   │   │   │   ├── Primitives/ PrimitiveShape    Display/ DisplayPolygonAssembly, TriangleOrigin
│   │   │   │   ├── Fixtures/ CalibrationSpecimen
│   │   │   │   └── UV/  ComponentUVSurface        // per-mesh UV DATA (edited by UVEditing)
│   │   │   ├── Adjacency/ AdjacencyIndex    Selection/ ComponentSelection, SelectionMode
│   │   │   └── Picking/ RayPickIntersection
│   │   │
│   │   ├── UVEditing/                             // first-class UV editor (peer to Modeling)
│   │   │   ├── UVEditingExtension.{h,cpp}        // 🟢 UV session lifecycle
│   │   │   ├── SeamSpecification.{h,cpp}         // 🧩 seam / island descriptor
│   │   │   ├── LscmSolver.{h,cpp}                // 🧩 LSCM parameterization
│   │   │   ├── SlimSolver.{h,cpp}                // 🧩 SLIM relaxation
│   │   │   ├── HarmonicSolver.{h,cpp}            // 🧩 harmonic parameterization
│   │   │   ├── IslandPacking.{h,cpp}            // 🧩 atlas packing
│   │   │   └── UVPinConstraint.{h,cpp}          // 🧩 pinned-vertex constraint during unwrap
│   │   │
│   │   ├── ParametricSketch/                      // was Cad/ — exact B-rep NURBS kernel (PLAN-CadWorkspace.md)
│   │   │   ├── ParametricSketchExtension.{h,cpp}     // 🟢 (was CadExtension)
│   │   │   ├── ParametricSketchConfiguration.{h,cpp} // 🧩 tolerances, continuity, tessellation error
│   │   │   ├── Geometry/     ParametricCurve, NurbsCurve, NurbsSurface, BsplineBasis, KnotSequence,
│   │   │   │                 ConicSection, AnalyticSurface, CurveInterpolation, SurfaceInterpolation
│   │   │   ├── Boundary/     SolidBody, SolidEnvelope, SolidFace, FaceLoop, CoEdge, SolidEdge,
│   │   │   │                 SolidVertex, BoundaryConstructor, BoundaryValidate
│   │   │   ├── Sketch/       SketchPlane, SketchPrimitive, SketchConstraint, ConstraintSolver,
│   │   │   │                 ConstraintDecomposition, SketchProfile
│   │   │   ├── Operations/   Extrude, Revolve, Sweep, Loft, Fillet, Chamfer, BlendSurface,
│   │   │   │                 HollowOperation, OffsetOperation, SetbackCorner
│   │   │   ├── Boolean/      BooleanOperation, SurfaceIntersection, IntersectionLoop, FacePartition,
│   │   │   │                 RegionClassification, BoundaryStitch, ToleranceModel
│   │   │   ├── Tessellation/ SurfaceTessellation, TrimTessellation, EdgeDiscretization, DisplayPolygonAssembly
│   │   │   └── Draughting/   DraughtShapeStore, DraughtConstraintSolver, DraughtBoolean,
│   │   │                     DraughtFillet, DraughtLoft, DraughtTransform   // ← retired
│   │   │
│   │   ├── TexturePainting/                       // was Painting/ — 3D texture painting
│   │   │   ├── TexturePaintingExtension.{h,cpp}      // 🟢 (was PaintingExtension)
│   │   │   ├── TexturePaintingConfiguration.{h,cpp}  // 🧩 (was PaintingConfiguration)
│   │   │   ├── GpuPaintDispatch, PaintStoreAddress.h   // ← retired
│   │   │   ├── BrushSpecification, BrushProfile, PaintStroke, TexturePaintSurface,
│   │   │   │   ProjectionPaint, PaintChannel
│   │   │   └── PaintTools/ FillTool, SmearTool, CloneTool, MaskTool
│   │   │
│   │   ├── TextureBaking/                         // was Baking/ — normal/AO/curvature bake
│   │   │   ├── TextureBakingExtension.{h,cpp}    // 🟢 NEW coordinator
│   │   │   ├── GpuBakeDispatch, NormalMapBaker, SurfaceSampleField, TriangleRayVolume
│   │   │   └── Shaders/ Surface{Normal,Position,Occlusion,Curvature,Bevel,Dust,AmbientOcclusion}Encode.comp,
│   │   │              DistanceVolume{Seed,Flood,Narrow,Resolve,BandExact,BakeExact}.comp
│   │   │
│   │   ├── Interchange/                           // ← retired (import readers)
│   │   │   └── ModelInterchange, InterchangeReaders.h, InterchangeWavefrontReader (obj),
│   │   │      InterchangeGltfReader (glTF), InterchangeFilmboxReader (fbx)
│   │   │
│   │   └── Gizmos/                                // manipulation handles (was Interaction/Gizmos)
│   │       ├── GizmoExtension.{h,cpp}            // 🟢 gizmo lifecycle + hit-routing
│   │       ├── GizmoActiveAxis.{h,cpp}           // 🧩 active axis/plane, drag delta, snap ("state" banned)
│   │       ├── Gizmo3D/  TranslateGizmo3D, RotateGizmo3D, ScaleGizmo3D, TransformGizmoFrame
│   │       └── Gizmo2D/  TranslateGizmo2D, RotateGizmo2D, ScaleGizmo2D, PlanarGizmoFrame
│   │
│   ├── Workspaces/                                // ══ pillar 5 — editable workspace documents ══
│   │   │                                          //   A WorkspaceDocument = one tab = one editable authoring
│   │   │                                          //   session (Modeling/UV/Sketch/Paint/Bake). Each owns a
│   │   │                                          //   Revision track. Simulation is NOT a document.
│   │   ├── WorkspaceDocumentExtension.{h,cpp}     // 🟢 open / activate / close documents; owns the tab set
│   │   ├── WorkspaceDocument.{h,cpp}              // 🧩 one document: data + its Revision track + dirty mark
│   │   ├── WorkspaceRegistry.{h,cpp}              // 🧩 all open documents (dense store)
│   │   ├── WorkspaceToken.h                       // 🧩 stale-detecting identity (MicroUtils TokenIssuer + TokenAuthentication)
│   │   ├── WorkspaceClassification.h              // 🧩 which authoring category (Modeling/UV/Sketch/Paint/Bake)
│   │   ├── AuthoringWorkspace.{h,cpp}             // 🧩 links a document to its authoring content (PolygonComplex, sketch, paint target…)
│   │   │
│   │   ├── Revision/                              // ← graveyard RevisionDivision — PER-DOCUMENT history
│   │   │   │                                      //   Undo/redo/non-linear versioning + forking, one track
│   │   │   │                                      //   per WorkspaceDocument. P0 — headers + vertical slice
│   │   │   │                                      //   (ContentArena + MutationIntake + CursorResolution).
│   │   │   │                                      //   Replaces the legacy linear EditLog + LogCursor.
│   │   │   ├── RevisionExtension.{h,cpp}          // 🟢 (was RevisionDivision)
│   │   │   ├── Build.bat  README.md
│   │   │   ├── Registry/       RevisionEntry.h, RevisionRegistry.{h,cpp}, ContentArena.{h,cpp},
│   │   │   │                   TrackFootprint.h    // VacancyTable/TokenIssuer ← MicroUtils
│   │   │   ├── Connectivity/   SequenceAdjacency.{h,cpp}, ForkTranslation.{h,cpp}
│   │   │   ├── Evaluation/     CursorResolution.{h,cpp}, InvertibleContent.{h,cpp}, SequenceLinearizer.{h,cpp}
│   │   │   ├── Lifecycle/      MutationIntake.{h,cpp}, GarbageReclamation.{h,cpp}
│   │   │   └── SignalRouting/  SignalBroadcaster.{h,cpp}, RevisionDeltaDecoder.{h,cpp}  // "state" banned
│   │   │
│   │   └── Codex/                                 // ← retired (UOF unified binary project format)
│   │       ├── CodexDocument.{h,cpp}              // 🧩 serialize a WorkspaceDocument + its Revision track → UOF
│   │       └── CodexArchive.{h,cpp}              // 🧩 read/write the on-disk archive
│   │
│   └── Interface/                                 // ══ pillar 6 — purely the UI ══
│       ├── InterfaceExtension.{h,cpp}             // 🟢 UI lifecycle
│       ├── README.md   Build.bat
│       ├── Theme/          ThemeConfiguration, ThemeResolver, ColorPaletteDescriptor
│       ├── Instrumentation/                       // (live) on-screen metrics/telemetry HUD
│       │   ├── InstrumentationExtension.{h,cpp}  InstrumentationValidation.{h,cpp}
│       │   ├── Classification/ InstrumentRecordClassification.h, PresentationSpecification.h
│       │   ├── Registry/       InstrumentRecordEntry.h, InstrumentRecordStore, InstrumentVacancyTable, InstrumentToken.h
│       │   ├── SignalAccumulation/ MetricSignalAccumulator, SignalEvaluationRange.h, SignalIngestionProfile.h
│       │   ├── SpatialAlignment/   DormantInstrumentLinearizer, EnclosureAlignmentProfile
│       │   └── InstrumentProjection/ AllocationBarPass, BudgetPercentPass, FrameGraphPass,
│       │                             InstrumentEnclosurePass, LiveReadoutPass, RenderReportPass, InstrumentBodyContext.h
│       ├── Components/
│       │   ├── PropertyPanelBase
│       │   ├── Cards/    ActionToolbar, SectionHeader, ContentSection, ContentCarousel
│       │   ├── Controls/ ValueSlider, VectorEntry, ScalarEntry, BooleanEntry, ColorEntry,
│       │   │             SelectionEntry, PathEntry, Dropdown, ControlLayout, MenuPill
│       │   └── Menus/    GlyphInscription (unit-square vector glyphs, ImDrawList — backend-agnostic),
│       │       │         TopologyActionMenu (selection-driven context menu card, one flat list),
│       │       │         ModellingToolMenu  (two-pane band rail + tool grid, same catalogue split by family)
│       │       └── PolygonMutation/   // polygon-specific payload: what the menu offers and when
│       │           ├── SelectionPredicate      // gate: stratum + count + connectivity -> AvailabilityCondition
│       │           ├── OperationCatalogue      // the 94-operation table across 11 bands
│       │           ├── PolygonGlyphIdentity    // glyph names only (no ImGui — keeps the catalogue pure)
│       │           ├── PolygonGlyphTable       // the stroke paths that draw them
│       │           └── StratumBadgeTable       // the 7 two-tone header badges, one per TopologyStratum
│       ├── Icons/        SvgRasterizer (thorvg), SvgIconRegistry (Vulkan textures),   // keyed SVG glyph store, content-hash deduped
│       │                 IconPackGlobal (g-), IconPackCad (cad-), IconPackScene (scene-)
│       ├── Dialogues/      ValidationDialogue, ConfirmDialogue   // ← retired
│       ├── Settings/       RootPanel, SettingsPanel, GeneralPanel, AppearancePanel, AppearancePersistence,
│       │                   LibraryPanel, NotificationPanel, PerformancePanel   // ← retired
│       ├── RevisionPanel/  RevisionPanel, RevisionPanelConfiguration   // consumes Workspaces/Revision delta bus
│       ├── WorkspaceHost/                         // (live) app chrome + docking (replaces "Shell")
│       │   ├── WorkspaceApplicationRunner, WorkspaceDockHost, WorkspacePanelDock, WorkspaceTabStrip
│       │   ├── PanelRegistry, DeploymentBracket, ImguiPlatformBridge
│       │   ├── Viewport/  ViewportPanel, ViewportCamera, ViewportGrid
│       │   └── Outliner/  OutlinerPanel, OutlinerConfiguration, Model/OutlinerModel, Model/OutlinerRow
│       └── WorkspaceLayouts/                      // one UI layout per editor mode (was Workspaces/)
│           ├── Modeling/          ModelingWorkspace, ModelingPropertyPanel
│           ├── UV/                UVWorkspace, UVPropertyPanel
│           ├── ParametricSketch/  ParametricSketchWorkspace, SketchPropertyPanel
│           ├── TexturePaint/      TexturePaintWorkspace, PaintPropertyPanel
│           ├── TextureBake/       TextureBakeWorkspace, BakePropertyPanel
│           └── Simulation/        SimulationWorkspace, SimulationPropertyPanel
│
├── Executables/                                   // was "Applications/" ("Application" banned) — one folder per build target
│   │                                              //   Every tool is its OWN Editor executable that boots its workspace
│   ├── ModelingEditor/         { ModelingEditorEntry.cpp,          Config/, Build.bat, README.md }
│   ├── UVEditor/               { UVEditorEntry.cpp,                Config/, Build.bat }
│   ├── ParametricSketchEditor/ { ParametricSketchEditorEntry.cpp,  Config/, Build.bat }
│   ├── TexturePaintEditor/     { TexturePaintEditorEntry.cpp,      Config/, Build.bat }
│   ├── TextureBakeEditor/      { TextureBakeEditorEntry.cpp,       Config/, Build.bat }
│   ├── SimulationEditor/       { SimulationEditorEntry.cpp,        Config/, Build.bat }
│   └── Validation/                                // validation targets (not editors)
│       ├── ControlsGallery/           { ControlsGalleryEntry.cpp,           …Panel.{h,cpp}, Build.bat }
│       ├── InstrumentationValidation/ { InstrumentationValidationEntry.cpp, Build.bat }
│       ├── ModellingToolValidation/   { ModellingToolValidationHost.cpp,    …Panel.{h,cpp}, Build.bat }
│       ├── PolygonActionValidation/   { PolygonActionValidationHost.cpp,    …Panel.{h,cpp}, Build.bat }
│       ├── OrientationCube/           { OrientationCubeEntry.cpp,           Build.bat }
│       ├── RenderExtensionValidation/ { RenderExtensionValidationEntry.cpp, Build.bat }
│       ├── SceneDirectory/            { SceneDirectoryEntry.cpp,            …Panel.{h,cpp}, Build.bat }
│       └── WorkspaceDock/             { WorkspaceDockEntry.cpp,             Build.bat }
│
├── Binaries/                                       // outputs mirror Executables/ 1:1 (git-ignored)
│   ├── ModelingEditor/         { ModelingEditor.exe,         Config/, EngineContent/ }
│   ├── UVEditor/               { UVEditor.exe,               Config/, EngineContent/ }
│   ├── ParametricSketchEditor/ { ParametricSketchEditor.exe, Config/, EngineContent/ }
│   ├── TexturePaintEditor/     { TexturePaintEditor.exe,     Config/, EngineContent/ }
│   ├── TextureBakeEditor/      { TextureBakeEditor.exe,      Config/, EngineContent/ }
│   ├── SimulationEditor/       { SimulationEditor.exe,       Config/, EngineContent/ }
│   └── Validation/             { ControlsGallery.exe, OrientationCube.exe, SceneDirectory.exe,
│                                 WorkspaceDock.exe, InstrumentationValidation.exe, RenderExtensionValidation.exe,
│                                 PolygonActionValidation.exe, ModellingToolValidation.exe }
│
├── Documentation/                                 // per "Where to Save Documents"
│   ├── Explainers/  Mockups/  Research/  Skills/  Assets/
│   └── Prototypes/                                // single-file .html prototypes, plus:
│       └── RockFormation/                         // node-based 3D rock/erosion editor (WebGPU, voxel)
│           │                                      //   RockFormationConstructionTree.html  (chrome + boot)
│           │                                      //   RockFormationEntry.js               (device, loop)
│           │                                      //   RockFormationNotes.md  (working notes:
│           │                                      //     confirmed species, deferred work, hazards)
│           ├── Construction/                      // the tree: species, state, WGSL transcription
│           │                                      //   GeologyReference.js  PortCategories.js
│           │                                      //   ConstructionSpecifications.js  TreeState.js
│           │                                      //   ConstructionTranscriber.js  SeedTree.js
│           ├── Simulation/                        // the erosion sim: voxel grid + compute WGSL
│           │                                      //   VoxelField.js    (ping-pong storage, bindings)
│           │                                      //   FieldSeed.js     (analytic SDF -> density)
│           │                                      //   ErosionCompute.js(thermal/hydraulic/aeolian/
│           │                                      //                     spheroidal/saltfreeze, gather-only)
│           │                                      //   GridMarch.js     (fixed-step density march)
│           │                                      //   ErosionTimeline.js (play/pause/scrub; GPU snapshot
│           │                                      //                     ring — erosion has no inverse,
│           │                                      //                     so back-scrub restores + re-steps)
│           ├── Resolve/                           // WGSL library + device host + card-preview march
│           │                                      //   DistanceExpression.js  DeviceHost.js
│           │                                      //   HullExpression.js (Blender rock-generator hull:
│           │                                      //                     plane intersection w/ per-plane
│           │                                      //                     crease + 4-stage displacement)
│           │                                      //   SphereTrace.js  (analytic; CARD PREVIEWS only —
│           │                                      //                    the viewport marches the grid)
│           └── Surface/                           // authoring interface (DOM cards, SVG links)
│                                                  //   TreeSurface.js  Catalogue.js  PanelDials.js
│
├── EngineDocs/                                     // plans + running docs
│   ├── FolderStructure.md   (+ PARSE:FOLDER-INDEX block)   Backlog.md   ImportantNotes.md   Phase.md
│   ├── PLAN-CadWorkspace.md
│   └── AgenticInstuctions/  SKILL-Naming.md, SKILL-formatting.md, SKILL-CompactOutputProtocol.md
│
└── Archive/                                        // single graveyard (dead code / retired trees)
    └── (RetiredProject + Attic-EngineReset fold here after adoption)
```

---

## Pillars at a glance

| # | Pillar | Owns |
|---|---|---|
| 1 | **EngineContext/** | EngineHost · Configuration · Math · MetricSpace · SpatialAcceleration · MicroUtils · Motion · Input · Audio · Scene · Navigation |
| 1b | **Platform/** | Windowing · Concurrency · Storage · InputProvider |
| 2 | **Simulation/** | Physics · Cloth · Gameplay |
| 3 | **Graphics/** | Render · Visibility · Atmosphere · Grid · Shadow · DistanceField · Terrain |
| 4 | **Authoring/** | Modeling · UVEditing · ParametricSketch · TexturePainting · TextureBaking · Interchange · Gizmos |
| 5 | **Workspaces/** | WorkspaceDocument · Revision · Codex |
| 6 | **Interface/** | Theme · Instrumentation · Components · Dialogues · Settings · RevisionPanel · WorkspaceHost · WorkspaceLayouts |

## Editor targets (6) + Validation (7)

`ModelingEditor · UVEditor · ParametricSketchEditor · TexturePaintEditor · TextureBakeEditor · SimulationEditor` — each `…Entry.cpp`, mirrored 1:1 in Binaries. SimulationEditor is the only document-less one.
