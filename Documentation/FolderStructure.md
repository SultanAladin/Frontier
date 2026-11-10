# Frontier — Folder Structure

Authoritative directory ownership for the Remastered Frontier repository. Every directory under `Engine/` and `UI/`
follows the strict two-word PascalCase `<Subject><Role>` taxonomy in `AgenticInstructions/SKILL-Naming.md` (19 closed
Role suffixes, zero banned words). Update this map whenever an approved structural change occurs.

```text
Frontier/
├── README.md
├── CLAUDE.md
├── AGENTS.md
├── .gitignore
├── .claudeignore
├── AgenticInstructions/                        # Authoritative agent skill specifications (skills only)
│   ├── SKILL-Naming.md                         # 19-Role <Subject><Role> taxonomy, banned words, identifier rules
│   ├── SKILL-Formatting.md                     # 142/122 banners, [unit] alignment, Unicode math, Allman 4-space
│   ├── SKILL-Emoji.md                          # Closed emoji whitelist & decision-table rating glyphs
│   ├── SKILL-CompactOutputProtocol.md
│   └── SKILL-SymbolIndex.md                    # .symbolindex digest lookup protocol
├── Automation/                                 # Build, shader, font, and dependency scripts (PowerShell / Python)
├── ExternalPackages/                           # Git-ignored third-party libraries + tracked Dependencies.lock.json
├── EngineContent/                              # Executable-relative runtime content (never "Assets")
│   ├── Icons/                                  # Shared SVG icon vectors
│   ├── FontArchives/                           # OFL font families + licence files + TOML descriptors
│   ├── MaterialArchives/                       # Authored .material descriptors and surface maps
│   ├── EnvironmentArchives/                    # Studio & sky equirectangular environments
│   └── Defaults/                               # Default Appearance.toml and blank project templates
│
├── Engine/                                     # Reusable native C++20 engine (<Subject><Role> modules)
│   │
│   ├── SlotIndex/                              # 🧩 Generational identity, occupancy, delivery & micro-codecs
│   │   └── (planned: Identity.h [Identity<Subject>], OccupancyIndex.{h,cpp}, DeliveryGuarantee.h [Deliver<T>],
│   │                 PrecisionGuarantee.h, HashCodec.{h,cpp}, StringCodec.{h,cpp}, ColourProjection.{h,cpp},
│   │                 IntervalClassifier.{h,cpp})
│   │
│   ├── PartitionSpace/                         # 🧩 Spatial subdivision — octree, BVH & toroidal clipmaps
│   │   └── (planned: ToroidalSpace.h [header-only scroll math], ClipmapSpace.{h,cpp},
│   │                 BoundingStructure.{h,cpp}, SpatialPartition.{h,cpp})
│   │
│   ├── MetricSpace/                            # 🧩 Spatial coordinates, algebra & ray/primitive intersection
│   │   └── (planned: CoordinateSpace.h, CoordinateProjection.h, DistanceMetrics.h, SpatialExtent.h,
│   │                 LinearAlgebra.h, VectorAlgebra.{h,cpp}, MatrixAlgebra.{h,cpp},
│   │                 QuaternionRotation.{h,cpp}, IntersectionSolver.{h,cpp})
│   │
│   ├── EngineSpecification/                    # 🧩 Engine & renderer parametric TOML schemas
│   │   └── (planned: EngineSpecification.{h,cpp}, RenderSpecification.{h,cpp})
│   │
│   ├── MotionSequence/                         # 🧩 Shared animation quadrature, easing, springs & timelines
│   │   ├── InterpolantProjection/              #   ScalarProjection, VectorProjection, ColourProjection
│   │   ├── EasingSpecification/                #   EasingSpecification, BezierProjection
│   │   ├── DynamicsIntegrator/                 #   SpringIntegrator, InertiaIntegrator, DampingIntegrator
│   │   └── TimelineSequence/                   #   TransitionSpecification, MotionSequence, TimelineSequence
│   │
│   ├── InputInterchange/                       # 🧩 Raw OS/GLFW events -> chord specifications -> action queue
│   │   └── (planned: InputInterchange.{h,cpp}, ChordSpecification.{h,cpp}, ChordClassifier.{h,cpp},
│   │                 ActionQueue.{h,cpp})
│   │
│   ├── WindowInterchange/                      # 🧩 Platform window guarantee (GLFW + Win32/X11/Wayland/Stub)
│   │   └── (planned: PlatformWindow.h, WindowInterchangeGlfw.cpp, WindowInterchangeWin32.cpp,
│   │                 WindowInterchangeX11.cpp, WindowInterchangeWayland.cpp, WindowInterchangeStub.cpp)
│   │
│   ├── PhysicsSolver/                          # 🧩 Rigid-body broad-phase, contacts, impulses & fixed-step loop
│   │   └── (planned: PhysicsScheduler.{h,cpp}, BroadphaseSpace.{h,cpp}, ContactClassifier.{h,cpp},
│   │                 ImpulseSolver.{h,cpp}, RigidBodyIntegrator.{h,cpp})
│   │
│   ├── RenderScheduler/                        # 🧩 Vulkan device exchange, Slang pass scheduling & HDR resolve
│   │   └── (planned: RenderScheduler.{h,cpp}, VulkanDeviceExchange.{h,cpp}, CycleScheduler.{h,cpp},
│   │                 RadianceProjection.{h,cpp}, DiagnosticExtension.{h,cpp})
│   │
│   ├── ContentInterchange/                     # 🧩 ContentDirectory, TOML codecs & OBJ/glTF/FBX import codecs
│   │   └── (planned: ContentDirectory.{h,cpp}, GeometryInterchange.{h,cpp}, WavefrontCodec.{h,cpp},
│   │                 GltfCodec.{h,cpp}, FilmboxCodec.{h,cpp})
│   │
│   └── WorkspaceSequence/                      # 🧩 Editable per-tab authoring workspaces (Simulation is stateless)
│       │   (planned: WorkspaceSequence.{h,cpp}, WorkspaceSpecification.{h,cpp}, WorkspaceIndex.{h,cpp},
│       │             WorkspaceClassifier.h [Polygon/Uv/Sketch/Texturing/Transfer], Identity<Workspace>)
│       └── RevisionSequence/                   # 🧩 Per-document non-linear undo/redo/forking revision track
│           └── (planned: RevisionSequence.{h,cpp}, RevisionIndex.{h,cpp}, ByteSpace.{h,cpp},
│                         MutationQueue.{h,cpp}, CursorSolver.{h,cpp}, ForkProjection.{h,cpp})
│
├── UI/                                         # Reusable ImGui/ImDrawList presentation & editor chrome
│   ├── CardPanel/                              # 🧩 ActionToolbar, SectionHeader, ContentSection, ContentCarousel
│   ├── ControlPanel/                           # 🧩 PropertyPanel, ValueSlider, Vector/Scalar/Boolean/Colour/
│   │                                           #    Selection/PathEntry, DropdownPanel, ControlSpace, MenuPill
│   ├── MenuPanel/                              # 🧩 GlyphSpace (unit-square ImDrawList vector glyphs), menus
│   └── EditorPanel/                            # 🧩 Multi-band workspace dock, outliner & inspector hosts
│
├── Projects/                                   # Standalone executable hosts (Project-Zero/, etc.)
├── Exhibits/                                   # Retained validation executables, exhibits & captured proof
│   ├── FrontierEngine/                         # Baseline WebGPU engine & Project-Zero editor exhibit
│   └── FrontierEngineV2/                       # Same scene running ReSTIR PT Enhanced (Lin et al. 2026, 10.1145/3804494)
├── Experimental/                               # Isolated prototypes (never linked into Engine/)
│   ├── ProjectZeroEditor/                      # Standalone Project-Zero Editor UI prototype (port 8083)
│   └── FrontierEditor/                         # Standalone FrontierEditor UI prototype (Vite + React, port 8084)
├── Documentation/
│   ├── FolderStructure.md                      # This ownership map
│   ├── Rules/                                  # Additional subsystem engineering specifications
│   └── Plans/
│       ├── Ongoing/
│       └── Completed/
└── _AgentScratch/                              # Git-ignored disposable scratchpad (logs/, build/, tmp/)
```
