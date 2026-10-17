# Folder Structure

## Target ownership

```text
Frontier/
├── CMakeLists.txt             # UI-only build and local verification
├── Automation/                # Existing tooling plus pinned UI setup and patches
├── ExternalPackages/          # Ignored downloaded dependency trees
├── README.md
├── AGENTS.md
├── CLAUDE.md
├── Documentation/
│   ├── Tracker.md
│   ├── FolderStructure.md
│   ├── Rules/
│   └── Plans/
│       ├── Ongoing/
│       └── Completed/
├── Engine/                    # Shared production code
│   ├── Interface/             # Selectively ported UI foundations
│   ├── Lifecycle/             # Shared activation sequence
│   └── Render/                # Blank; Git retention marker only
├── UI/
│   ├── Editor/                # Production editor UI foundations
│   └── Assets/Icons/          # Original and compatibility SVGs
├── Exhibits/
│   ├── <ValidationName>/
│   │   ├── README.md
│   │   ├── Code/
│   │   ├── Images/
│   │   └── Results/
│   └── EditorMirror/           # Comparison mirror of the C++ editor
├── Experimental/              # Prototypes and experimental HTML
├── EngineDocs/AgenticInstuctions/  # Four retained authoritative skills
└── _ClaudeScratch/            # Ignored logs/, build/, tmp/
```

This is a target map, not a claim that source migration is complete.
Create meaningful folders as their contents are needed; do not add empty placeholder trees.
Use technical validation names under Exhibits, not the banned naming placeholder from earlier discussion.
No new top-level ownership categories without approval. Update this map after approved structural changes.

## Existing layout awaiting migration review

`Internal/`, `Executables/`, `Applications/`, `Automation/`, `memory/`,
`Documentation/Prototypes/`, and `Documentation/Research/` remain inherited locations.
Do not delete or move them blindly. Existing diagnostics remain in `Exhibits/Diagnostics/`.
`EngineDocs/` retains existing governance references until separately reconciled.
The linked source also has `Docs/`, `Editor/`, `Projects/`, `Tools/`, and dependency/content folders;
map their ownership and build dependencies before any import or relocation.

## Selective port boundary

`Exhibits/UIFoundation/` retains verification code, results, and actual SVG raster evidence.
`Automation/Patches/` owns the six inherited ImGui patches. Dependency pins live in
`Automation/UIDependencies.json`. No scene renderer is ported or built by the new CMake target.
Inherited `Internal/Graphics/` remains untouched and is not linked into this target.
