"""Recover pinned exhibits and retain source-branch checks; never changes the working branch."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

Root = Path(__file__).resolve().parents[3]
Evidence = Root / "Exhibits/IlluminationConsolidation"
Scratch = Root / "_ClaudeScratch/tmp"
Sources = json.loads((Evidence / "Code/Sources.json").read_text())
Parser = argparse.ArgumentParser(description=__doc__)
Parser.add_argument("--fetch", action="store_true", help="Recover exact revisions from GitHub")
Arguments = Parser.parse_args()


def Execute(Command, Directory, Destination, Environment=None):
    Result = subprocess.run(Command, cwd=Directory, env=Environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (Evidence / "Results" / Destination).write_text(Result.stdout)
    if Result.returncode:
        raise RuntimeError(f"{Command} failed ({Result.returncode}); see {Destination}")
    return Result.stdout


Summary = {"Node": subprocess.check_output(["node", "--version"], text=True).strip(),
           "Platform": sys.platform, "Backend": "Dawn null", "Viewport": "320x180", "Sources": []}
for Source in Sources:
    Name = Source["Name"]
    Revision = Source["Revision"]
    if Arguments.fetch:
        subprocess.run(["git", "fetch", "--no-tags", "https://github.com/" + Source["Repository"] + ".git",
                        Revision], cwd=Root, check=True)
    Destination = Scratch / Name
    Destination.mkdir(parents=True, exist_ok=True)
    Files = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", Revision, Source["Folder"]],
                                    cwd=Root, text=True).splitlines()
    for File in Files:
        Output = Destination / Path(File).relative_to(Source["Folder"])
        Output.parent.mkdir(parents=True, exist_ok=True)
        Output.write_bytes(subprocess.check_output(["git", "show", Revision + ":" + File], cwd=Root))
    Entry = dict(Source)
    Entry["Files"] = len(Files)
    if (Destination / "package.json").exists():
        Execute(["npm", "ci", "--ignore-scripts"], Destination, Name + "-install.txt")
        Tests = Execute(["npm", "test"], Destination, Name + "-tests.txt")
        Entry["UnitTests"] = int(re.search(r"# pass (\d+)", Tests)[1])
        assert re.search(r"# fail 0\b", Tests)
        Execute(["npm", "ci", "--ignore-scripts"], Destination / "Tools", Name + "-tools-install.txt")
        Execute(["node", "VehicleStability.mjs", ".."], Destination / "Tools", Name + "-stability.txt")
        Entry["Stability"] = "passed"
    else:
        Entry["UnitTests"] = None
        Entry["Stability"] = "not bundled"
    # The early c6 exhibit predates the harness. Borrow only the DOM/WebGPU test fixture, not application code.
    Fixture = Destination if (Destination / "Tools/HeadlessHarness.mjs").exists() else Scratch / "b55d93c0-slate"
    Environment = dict(os.environ, SIZE="320x180", BACKEND="null", HARNESS_TIMEOUT="30000")
    Output = Execute(["node", str(Fixture / "Tools/HeadlessHarness.mjs"), str(Destination), "12",
                      "?capture=1&quality=low"], Root, Name + "-gpu.txt", Environment)
    assert re.search(r"12 frames, 0 errors, 0 warnings", Output), Output
    Entry["NullFrames"] = 12
    Selections = ["screen"] if Name in ("d984ba1b-slate", "b55d93c0-slate") else []
    if Name == "21530081-frontier":
        Selections = ["rtx", "gtx"]
    for Selection in Selections:
        Environment.update(HOOKS=str(Evidence / "Code/TraceSelection.mjs"), TRACE_SELECTION=Selection)
        Output = Execute(["node", str(Fixture / "Tools/HeadlessHarness.mjs"), str(Destination), "12",
                          "?capture=1&quality=low"], Root, Name + "-" + Selection + ".txt", Environment)
        assert re.search(r"12 frames, 0 errors, 0 warnings", Output), Output
        assert "Verified trace selection: " + Selection in Output
    Entry["TraceSelections"] = Selections
    Summary["Sources"].append(Entry)
    print(Name, Entry["UnitTests"], "unit tests; null frames:", Entry["NullFrames"], flush=True)
(Evidence / "Results/Summary.json").write_text(json.dumps(Summary, indent=4) + "\n")
