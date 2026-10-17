#============================================================================================================================================
#                                                          PREPAREFONTS.PY
#============================================================================================================================================
# 🧩 Download pinned OFL fonts and enumerate all local TTF/OTF files into a reproducible catalogue.

from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import tempfile
import urllib.request

RepositoryRoot = Path(__file__).resolve().parents[1]


def WriteAtomic(Destination, Payload):
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Descriptor, Temporary = tempfile.mkstemp(dir=Destination.parent, prefix='.font-')
    try:
        with os.fdopen(Descriptor, 'wb') as Output:
            Output.write(Payload)
        os.replace(Temporary, Destination)
    finally:
        Path(Temporary).unlink(missing_ok=True)


def GenerateCatalogue(FontRoot):
    from fontTools.ttLib import TTFont
    Entries = []
    Rejected = []
    for FontPath in sorted(FontRoot.rglob('*')):
        if not FontPath.is_file() or FontPath.suffix.lower() not in ('.ttf', '.otf'):
            continue
        try:
            with TTFont(FontPath) as Typeface:
                Names = Typeface['name']
                Family = Names.getDebugName(16) or Names.getDebugName(1) or FontPath.parent.name
                Entries.append({
                    'family': Family,
                    'file': FontPath.relative_to(FontRoot).as_posix(),
                    'weight': Typeface['OS/2'].usWeightClass,
                    'italic': bool(Typeface['OS/2'].fsSelection & 1),
                    'sha256': hashlib.sha256(FontPath.read_bytes()).hexdigest(),
                })
        except Exception as Error:
            Rejected.append({'file': FontPath.relative_to(FontRoot).as_posix(), 'error': str(Error)})
    Report = {'format': 1, 'faces': Entries, 'rejected': Rejected}
    WriteAtomic(FontRoot / 'FontCatalogue.json', (json.dumps(Report, indent=2) + '\n').encode())
    print(f'Catalogue: {len(Entries)} faces, {len(set(Entry["family"] for Entry in Entries))} families, '
          f'{len(Rejected)} rejected files')
    if Rejected:
        raise RuntimeError('Some font files are invalid; inspect FontCatalogue.json')


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--scan-only', action='store_true', help='No network; enumerate local fonts only')
    Parser.add_argument('--verify', action='store_true', help='Check pinned files without downloading')
    Parser.add_argument('--root', type=Path, default=RepositoryRoot / 'UI/Assets/Fonts')
    Arguments = Parser.parse_args()
    Manifest = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text())
    if not Arguments.scan_only:
        for Entry in Manifest['files']:
            Destination = Arguments.root / Entry['family'] / Entry['file']
            if Destination.exists():
                if hashlib.sha256(Destination.read_bytes()).hexdigest() == Entry['sha256']:
                    continue
                raise RuntimeError('Modified pinned font; preserve local edits before recovery: ' + str(Destination))
            if Arguments.verify:
                raise RuntimeError('Missing pinned file: ' + str(Destination))
            Request = urllib.request.Request(Entry['url'], headers={
                'Accept': 'application/vnd.github.raw+json', 'User-Agent': 'Frontier-font-setup'})
            with urllib.request.urlopen(Request, timeout=60) as Response:
                Payload = Response.read()
                if 'json' in Response.headers.get('Content-Type', ''):
                    Payload = base64.b64decode(json.loads(Payload)['content'])
            if hashlib.sha256(Payload).hexdigest() != Entry['sha256']:
                raise RuntimeError('Font checksum mismatch: ' + Entry['file'])
            WriteAtomic(Destination, Payload)
            print('Downloaded:', Entry['family'], Entry['file'], flush=True)
    GenerateCatalogue(Arguments.root)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(Main())
    except Exception as Error:
        print('Font preparation failed:', Error)
        raise SystemExit(1)
