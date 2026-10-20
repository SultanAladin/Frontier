"""Run the combined prototype's reproducible CPU and Dawn-null validation matrix."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

Prototype = Path(__file__).resolve().parents[1]
Evidence = Prototype.parents[1] / "Exhibits/IlluminationConsolidation/Results/Combined"
Evidence.mkdir(parents=True, exist_ok=True)
Parser = argparse.ArgumentParser(description=__doc__)
Parser.add_argument("--install", action="store_true")
Arguments = Parser.parse_args()
Summary = {"Backend": "Dawn null", "Viewport": "320x180", "Checks": []}


def Execute(Name, Command, Directory, Environment=None):
    Result = subprocess.run(Command, cwd=Directory, env=Environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (Evidence / (Name + ".txt")).write_text(Result.stdout)
    Summary["Checks"].append({"Name": Name, "ExitCode": Result.returncode})
    if Result.returncode:
        raise RuntimeError(f"{Name} failed; see {Evidence / (Name + '.txt')}")
    print(Name, "passed", flush=True)
    return Result.stdout


if Arguments.install:
    Npm = "npm.cmd" if os.name == "nt" else "npm"
    Execute("Dependencies", [Npm, "ci", "--ignore-scripts"], Prototype)
    Execute("ToolsDependencies", [Npm, "ci", "--ignore-scripts"], Prototype / "Tools")
Tests = Execute("UnitTests", ["node", "--test", *[str(File) for File in sorted((Prototype / "Tools").glob("*.test.mjs"))]], Prototype)
Summary["UnitTests"] = int(re.search(r"# pass (\d+)", Tests)[1])
Execute("VehicleStability", ["node", "VehicleStability.mjs", ".."], Prototype / "Tools")
for Category in ["spring", "shell", "solid"]:
    Execute("VehicleStability-" + Category, ["node", "VehicleStability.mjs", ".."], Prototype / "Tools",
            dict(os.environ, TYRE_MODEL=Category))
Environment = dict(os.environ, SIZE="320x180", BACKEND="null", HARNESS_TIMEOUT="30000")
for Name, Hook, Frames, Query in [
    ("CombinedToggles", "CombinedFeaturesHooks.mjs", 24, "?capture=1&quality=low"),
    ("TyreModels", "TyreModelsHooks.mjs", 16, "?capture=1&quality=low"),
    ("Effects", "VehicleEffectsHooks.mjs", 36, "?capture=1&quality=low"),
    ("PlumeHmi", "SlateIntegrationHooks.mjs", 48, "?capture=1&quality=low"),
    ("Orbit", "VehicleOrbitHooks.mjs", 30, "?capture=1&quality=low"),
    ("SoftwareBvh", "", 12, "?capture=1&quality=low&renderer=bvh"),
    ("Stress", "", 12, "?capture=1&quality=low&stress=256&stressmotion=1")
]:
    Environment.pop("HOOKS", None)
    if Hook:
        Environment["HOOKS"] = Hook
    Output = Execute(Name, ["node", "HeadlessHarness.mjs", "..", str(Frames), Query], Prototype / "Tools", Environment)
    assert f"{Frames} frames, 0 errors, 0 warnings" in Output, Output
(Evidence / "Summary.json").write_text(json.dumps(Summary, indent=4) + "\n")
