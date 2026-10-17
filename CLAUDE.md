# Frontier — Agent Instructions

Frontier is a general-purpose, cross-platform editor with reusable engine code.
The legacy migration is complete. Do not treat this as an unfinished 1:1 migration target.

## Mandatory reading — every session

Read all four documents at session start, before naming, formatting, editing, or presenting work.
They are authoritative and override agent defaults within their scope:

- `EngineDocs/AgenticInstuctions/SKILL-Naming.md` — all naming, banned words, and name construction.
- `EngineDocs/AgenticInstuctions/SKILL-formatting.md` — source and document formatting.
- `EngineDocs/AgenticInstuctions/SKILL-Emoji.md` — the only approved emojis and permitted usage.
- `EngineDocs/AgenticInstuctions/SKILL-CompactOutputProtocol.md` — compact output; tables and diagrams over prose.

Report missing rules before affected work. Do not silently skip them or invent exceptions.
These paths remain authoritative until an approved relocation updates every reference.

## Work and verification

Complete the requested work, run relevant local verification, then deliver results with evidence.
Local builds, tests, focused validation programs, and actual application captures need no separate approval.
Never deliver an implementation as complete without testing it.

| Work                  | Required verification                                             |
|-----------------------|-------------------------------------------------------------------|
| C++ or shaders        | Relevant build and tests; runtime validation where applicable.     |
| UI                    | Relevant tests and actual rendered or interaction evidence.        |
| Behaviour repairs     | Reproduce the defect and verify the repaired behaviour.            |
| Performance claims    | Reproducible measurements, conditions, and comparison results.      |
| Documentation         | Paths, links, structure, consistency, and applicable rule checks.   |
| Plans and analysis    | Verify factual claims against inspected sources; show a report.    |

- Exercise the changed behaviour, not merely unrelated passing tests.
- Add focused validation when existing coverage is insufficient.
- Compilation alone does not establish runtime correctness.
- Progress messages must not imply unfinished work passed verification.
- When blocked, report the command, observed result, missing prerequisites, and remaining checks.
- Mark unavailable checks blocked or not run, never passing. A blocked report is not completed implementation.

## Visible evidence — required with delivery

Every delivery must include an actual image, graph, results table, or concise verification report.
UI appearance claims require actual rendered evidence when capture is supported.
Mockups, generated illustrations, and invented measurements are not validation evidence.
A browser prototype does not prove that the production C++ editor renders the same result.

Retain evidence and its generating code together under `Exhibits/`.
Include reproduction commands, prerequisites, platform, compiler, configuration, tested revision,
and any relevant uncommitted changes. Explain limitations and present the evidence, not just its location.

## Folder ownership and documentation

Follow `Documentation/FolderStructure.md`. Do not invent alternative top-level folders.

| Location                         | Responsibility                                        |
|----------------------------------|-------------------------------------------------------|
| `Engine/`                        | Reusable production code shared across applications.  |
| `UI/Editor/`                     | Production editor UI.                                 |
| `Exhibits/`                      | Validation code, retained results, and proof images.   |
| `Exhibits/EditorMirror/`          | Comparison mirror of the C++ editor UI.                |
| `Experimental/`                  | Prototypes, experimental HTML, and related assets.     |
| `Documentation/Tracker.md`       | Single project progress and violation tracker.        |
| `Documentation/Rules/`           | Project rules; four retained skills remain above.     |
| `Documentation/Plans/Ongoing/`   | Approved active plans.                                |
| `Documentation/Plans/Completed/` | Completed plans, outcomes, and evidence links.         |
| `_ClaudeScratch/`                | Disposable local work; ignored and never committed.    |

Do not put prototypes, captures, reports, or validation-only code in `Engine/`.
Exhibits must reuse production engine code, not independent copies of it.
Keep requested prototype source, assets, and launchers together in `Experimental/`.
Do not delete inherited build, dependency, content, or application folders without an approved migration.
Update the folder map and affected references whenever approved moves change paths.
Maintain one tracker, not per-agent progress reports or competing backlogs.
Keep detailed results in exhibits and link them from the tracker.
Do not silently weaken rules to make a scan pass.

## Scratchpad

Use `_ClaudeScratch/logs/`, `_ClaudeScratch/build/`, and `_ClaudeScratch/tmp/` for disposable material.
Never commit scratch contents or leave the only copy of a deliverable or retained evidence there.
Permanent prototypes go in `Experimental/`; retained validation code and results go in `Exhibits/`.
Respect existing ignored build-output conventions when tools require their own directories.
Do not leave generated output next to source or loose at the root.
Incomplete HTML fragments use a non-`.html` extension such as `.part`.

## Platforms, binaries, and tooling

The user is on Windows. Deliver Windows binaries by default when a binary is requested or part of the task.
Do not build, package, or deliver Linux binaries unless requested.
Never substitute a Linux executable for a Windows deliverable.
If Windows compilation is unavailable, report the blocker; do not claim a Windows binary was verified.

Preserve Windows, Linux, and macOS portability. Android is a future consideration; no Android app is in scope.
Do not download macOS binaries or set up macOS during current work.
MSVC is the user's current compiler, not the only permitted compiler.
GCC/g++, MinGW-w64, or Clang may be used where the repository supports them.
Distinguish locally tested configurations from untested portability expectations.

Use actual repository build instructions and available tooling; do not hard-code developer installation paths.
Isolate platform-specific code from reusable code. Avoid unnecessary Windows-only assumptions.
Use the appropriate host interpreter: Windows command tooling for `.bat`, PowerShell for scripts requiring it,
and Bash for appropriate Linux/macOS commands. Python need not live at a fixed path.

## GitHub Actions — explicit approval required

Always ask before causing a GitHub Actions run unless the user explicitly authorised that run.
This includes dispatches, reruns, and remote operations such as pushes or pull requests that trigger workflows.
Inspect triggers before an authorised remote operation. Never use Actions as an automatic local-test fallback.
Permission to edit or commit locally is not permission to run Actions.
Follow the user's current commit and push instructions; do not infer permission to commit or push.

## Delivery

Follow the compact output skill: short outcome, verification table, visible evidence, exhibit paths,
blockers, untested configurations, and remaining work. Do not substitute confidence or compilation for testing.
