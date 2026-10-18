#============================================================================================================================================
#                                                   WINDOWSDISTRIBUTIONVALIDATION.PY
#============================================================================================================================================
# 🧩 Verify that incomplete safety results cannot produce a downloadable executable archive.

from pathlib import Path
import json
import subprocess
import sys
import tempfile

Repository = Path(__file__).resolve().parents[3]
Scratch = Repository / '_ClaudeScratch/tmp'
Scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=Scratch, prefix='distribution-refusal-') as Temporary:
    Directory = Path(Temporary)
    for Index, Report in enumerate([
        {'distribution_ready': False},
        {'distribution_ready': True, 'defender': {'clean': False}},
        {'distribution_ready': True, 'defender': {'clean': True}, 'signatures': []},
        {'distribution_ready': True, 'defender': {'clean': True}, 'signatures': [
            {'application': Name, 'status': 'NotSigned'} for Name in ('Frontier', 'TextureWorkspace')]},
    ]):
        Source = Directory / ('Refusal' + str(Index) + '.json')
        Source.write_text(json.dumps(Report), encoding='utf-8')
        Archive = Directory / ('Refusal' + str(Index) + '.zip')
        Execution = subprocess.run([sys.executable, str(Repository / 'Automation/PackageWorkspacesWindows.py'),
            '--build', str(Directory), '--security-report', str(Source), '--output', str(Archive)],
            capture_output=True, text=True)
        assert Execution.returncode != 0 and not Archive.exists(), 'Incomplete safety results produced an archive'
        assert 'Distribution withheld' in Execution.stderr, 'Refusal did not explain its security boundary'
print('PASS: four incomplete/untrusted distribution reports refused; no executable archives produced.')
