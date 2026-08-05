# Frontier (Projects) — Agent Instructions

This is the **migration target** repo. Code is ported here 1:1 from the legacy trees under
`C:\Users\OS\Documents\Frontier\` (`Internal\`, `RetiredProject\Engine`, `RetiredProject\GEngine`).

---

## 🔴 Read these first — every session, before naming/formatting/output

MANDATORY. Read all four at session start; they are authoritative and override defaults:

- `EngineDocs/AgenticInstuctions/SKILL-Naming.md` — ALL naming (banned words + how to build a name)
- `EngineDocs/AgenticInstuctions/SKILL-formatting.md` — file/source formatting
- `EngineDocs/AgenticInstuctions/SKILL-Emoji.md` — the ONLY approved emojis + where each is used
- `EngineDocs/AgenticInstuctions/SKILL-CompactOutputProtocol.md` — output style (tables/diagrams over prose)

`EngineDocs/FolderStructure.md` is the map of the repo — **stay close to it, and update it whenever
you add/move/rename a folder or subsystem.** `EngineDocs/Backlog.md` is the one running deferred-work
list (append/prune; no per-topic backlog files).

---

## 🔴 Autonomy limits — ASK, do not act

Default is **make the edit, then stop and report.** Verification is the user's call, never yours.

| Action                                                 | Rule                                                |
|--------------------------------------------------------|-----------------------------------------------------|
| Build (`Build.bat`, `cl.exe`, `BuildPlan.ps1`)         | 🔴 ASK first. Never self-initiate.                  |
| Run a test / validation exe / probe / harness          | 🔴 ASK first. Never self-initiate.                  |
| Screenshot, image capture, CDP / browser capture       | 🔴 NEVER. Not to scratch, not anywhere.             |
| Write ANY file that is not the source edit asked for   | 🔴 ASK first.                                       |
| Plans, notes, reports, summaries, `.md` write-ups      | 🟢 Chat only. A file ONLY on explicit instruction.  |
| Prototype `.html` the user asked for (+ launcher)      | 🟢 `Documentation/Prototypes/` — only when asked.   |
| Logs / probes / temp — **once validation is approved** | 🟢 `_ClaudeScratch/` (`logs/` · `build/` · `tmp/`). |

- "It compiles" is **not** a deliverable unless the user asked for it. Say *"not built — say the word
  and I'll build it"* instead of building.
- Wanting to be sure is not a reason. If the urge to validate arrives, **ask in one line** and wait.
- Never invent a probe, driver, or validator to check your own work unless the user approves it.

---

## 🔴 Do not clutter `EngineDocs/`

Never drop plans, `.md` notes, reports, or any document into `EngineDocs/` **unless explicitly
instructed.** `EngineDocs/` holds only `FolderStructure.md`, `Backlog.md`, and `AgenticInstuctions/`.
Plans go in chat first — write a file only when the user says to save it.

---

## 🔴 Scratch folder — keep the workspace clean

Once a build or validation **has been approved**, all of its disposable output goes in
`_ClaudeScratch/` — never next to source, never at repo root.

`_ClaudeScratch/` → `logs/` (build logs, command output) · `build/` (throwaway `.obj`, compile probes)
· `tmp/` (copies, staging, scratch `.cpp`, experiments, self-invented probes/drivers/validators).

- Fully git-ignored; nothing in it is ever committed.
- A requested prototype is **not** scratch: write it straight to `Documentation/Prototypes/` — never
  build it in scratch and copy it over, never leave the only copy in scratch.
- Assembly fragments (partial `<head>`/`<body>` chunks stitched by a script) are **not** viewable pages:
  give them a non-`.html` extension such as `.part` so nothing in scratch resembles a deliverable.

---

## Build & tooling

- **Shell = PowerShell.** Use the PowerShell tool for commands; run every `.bat` / `.ps1` through it,
  never Bash (Bash mangles Windows paths + `cmd` parsing). Use PowerShell syntax (`$env:VAR`, `$null`).
- MSVC: `cl.exe` via
  `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"`.
- Build planner: `Automation/BuildPlan.ps1` (drives every target's `Build.bat`).
- **Python 3.12 is available** for scripts/tooling — invoke as `python` or the launcher `py`
  (`python script.py`, `py -3 script.py`). Interpreter:
  `C:\Users\OS\AppData\Local\Programs\Python\Python312\python.exe`. Put throwaway scripts in
  `_ClaudeScratch/tmp/`.
