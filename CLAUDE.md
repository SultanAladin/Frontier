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

## 🔴 Do not clutter `EngineDocs/`

Never drop plans, `.md` notes, reports, or any document into `EngineDocs/` **unless explicitly
instructed.** `EngineDocs/` holds only `FolderStructure.md`, `Backlog.md`, and `AgenticInstuctions/`.
Plans go in chat first — write a file only when the user says to save it.

---

## 🔴 Scratch folder — keep the workspace clean

**All disposable work goes in `_ClaudeScratch/`. Never clutter the real source tree or repo root.**

`_ClaudeScratch/` → `logs/` (build logs, command output) · `build/` (throwaway `.obj`, compile probes)
· `tmp/` (copies, staging, scratch `.cpp`, experiments).

- **Every** temporary file — test `.cpp`, compile probe, log dump, copied-for-inspection file,
  intermediate build output — is created **inside `_ClaudeScratch/`**, never next to source or at root.
- Fully git-ignored; nothing in it is ever committed.
- When compile-checking a ported batch, emit the probe `.cpp` + `.obj` into `_ClaudeScratch/build/`,
  never into the ported subsystem's own folder.

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
