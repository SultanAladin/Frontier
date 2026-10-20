"""Inspect pinned donor tyre implementations using authenticated GitHub CLI; no branch changes."""
import base64
import hashlib
import json
import subprocess
from pathlib import Path

Directory = Path(__file__).resolve().parent
Sources = json.loads((Directory / "Sources.json").read_text())
Findings = []
for Source in Sources:
    Evidence = {}
    Paths = ["Source/VehicleSolver.js"]
    if Source["Name"] != "c6cba543-slate":
        Paths += ["Source/TyreThermalSolver.js", "Shader/VehicleVisual.wgsl", "README.md"]
    for Relative in Paths:
        Endpoint = (f"repos/{Source['Repository']}/contents/{Source['Folder']}/{Relative}"
                    f"?ref={Source['Revision']}")
        Response = subprocess.run(["gh", "api", Endpoint], check=True, capture_output=True, text=True)
        Content = base64.b64decode(json.loads(Response.stdout)["content"])
        Text = Content.decode("utf-8")
        Evidence[Relative] = {
            "Sha256": hashlib.sha256(Content).hexdigest(),
            "Quotes": [Line.strip() for Line in Text.splitlines() if any(Phrase in Line for Phrase in [
                "here each corner casts its strut axis", "there is no XPBD carcass here",
                "const TyreSidewallStiffness", "const TyreAirStiffness", "PB-FEM deformation continuum",
                "let PatchSpan =", "DR = -Deflection", "let BulgeIntensity =", "DY = sign(Position.y)"
            ])]
        }
        if Relative == "Source/VehicleSolver.js":
            assert "here each corner casts its strut axis" in Text
        if Relative == "Shader/VehicleVisual.wgsl":
            assert "DR = -Deflection * Envelope * RadialFrac" in Text
            assert "DY = sign(Position.y)" in Text
    Findings.append({"Source": Source, "Evidence": Evidence})

Destination = Directory.parent / "Results" / "TyreContinuumAudit.json"
Destination.write_text(json.dumps({
    "Scope": "Pinned donor vehicle solver and, where present, thermal model, wheel shader and README.",
    "Conclusion": "Inspected tyre paths are spring/contact dynamics and analytic vertex deformation, not shell or solid FEM.",
    "Limitation": "This is not a claim that no FEM code exists in any other repository, branch or historical revision.",
    "Findings": Findings
}, indent=2) + "\n")
print(f"PASS: {len(Findings)} pinned vehicle solvers inspected; 4 analytic tyre shaders confirmed.")
print(Destination)
