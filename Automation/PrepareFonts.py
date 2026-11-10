#============================================================================================================================================
#                                                          PREPAREFONTS.PY
#============================================================================================================================================
# 🧩 Download pinned OFL typefaces into EngineContent/FontsArchives/ and generate FontCatalogue.toml.

from pathlib import Path
import argparse
import base64
import hashlib
import io
import json
import os
import tarfile
import tempfile
import urllib.request

RepositoryRoot = Path(__file__).resolve().parents[1]


def WriteAtomic(Destination, Payload):
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Descriptor, TemporaryPath = tempfile.mkstemp(dir=Destination.parent, prefix='.font-')
    try:
        with os.fdopen(Descriptor, 'wb') as Output:
            Output.write(Payload)
        os.replace(TemporaryPath, Destination)
    finally:
        Path(TemporaryPath).unlink(missing_ok=True)


def GenerateCatalogue(FontRoot):
    from fontTools.ttLib import TTFont
    Faces = []
    Rejected = []
    Manifest = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text(encoding='utf-8'))
    FamilyLookup = {
        Entry['family'] + '/' + Entry['file']: Entry['family_name']
        for Entry in Manifest['files'] if 'family_name' in Entry
    }

    for FontPath in sorted(FontRoot.rglob('*')):
        if not FontPath.is_file() or FontPath.suffix.lower() not in ('.ttf', '.otf'):
            continue
        try:
            with TTFont(FontPath) as Typeface:
                NameRecord = Typeface['name']
                RelativeKey = FontPath.relative_to(FontRoot).as_posix()
                FamilyLabel = FamilyLookup.get(RelativeKey) or NameRecord.getDebugName(16) or NameRecord.getDebugName(1) or FontPath.parent.name
                Faces.append({
                    'family': FamilyLabel,
                    'file': RelativeKey,
                    'weight': Typeface['OS/2'].usWeightClass,
                    'italic': bool(Typeface['OS/2'].fsSelection & 1),
                    'sha256': hashlib.sha256(FontPath.read_bytes()).hexdigest(),
                })
        except Exception as Error:
            Rejected.append({'file': FontPath.relative_to(FontRoot).as_posix(), 'error': str(Error)})

    Lines = ['format = 1', '']
    for Entry in Faces:
        Lines.append('[[faces]]')
        for Key, FieldContent in Entry.items():
            Lines.append(Key + ' = ' + json.dumps(FieldContent, ensure_ascii=True))
        Lines.append('')
    WriteAtomic(FontRoot / 'FontCatalogue.toml', ('\n'.join(Lines) + '\n').encode('utf-8'))
    print('[Fonts]    {0} faces across {1} families ({2} rejected)'.format(
        len(Faces), len({Entry['family'] for Entry in Faces}), len(Rejected)))
    if Rejected:
        raise RuntimeError('Font verification rejected one or more files.')


def Main():
    Parser = argparse.ArgumentParser(description='Prepare pinned Frontier font archives.')
    Parser.add_argument('--scan-only', action='store_true', help='Enumerate local fonts without network access')
    Parser.add_argument('--verify', action='store_true', help='Verify pinned fonts exist without downloading')
    Parser.add_argument('--root', type=Path, default=RepositoryRoot / 'EngineContent/FontsArchives')
    Arguments = Parser.parse_args()

    Manifest = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text(encoding='utf-8'))
    DownloadedArchives = {}

    if not Arguments.scan_only:
        for Entry in Manifest['files']:
            Destination = Arguments.root / Entry['family'] / Entry['file']
            if Destination.exists():
                if hashlib.sha256(Destination.read_bytes()).hexdigest() == Entry['sha256']:
                    continue
                raise RuntimeError('Modified pinned font file: ' + str(Destination))
            if Arguments.verify:
                raise RuntimeError('Missing pinned font file: ' + str(Destination))

            SourceUrl = Entry['url']
            if SourceUrl in DownloadedArchives:
                Payload = DownloadedArchives[SourceUrl]
            else:
                Headers = {'Accept': 'application/vnd.github.raw+json', 'User-Agent': 'Frontier-font-setup'}
                if SourceUrl.startswith('https://api.github.com/') and os.environ.get('GH_TOKEN'):
                    Headers['Authorization'] = 'Bearer ' + os.environ['GH_TOKEN']
                Request = urllib.request.Request(SourceUrl, headers=Headers)
                with urllib.request.urlopen(Request, timeout=60) as Response:
                    Payload = Response.read()
                    if 'json' in Response.headers.get('Content-Type', ''):
                        Payload = base64.b64decode(json.loads(Payload)['content'])
                DownloadedArchives[SourceUrl] = Payload

            if 'archive_member' in Entry:
                if hashlib.sha256(Payload).hexdigest() != Entry['archive_sha256']:
                    raise RuntimeError('Font archive SHA-256 mismatch for ' + Entry['file'])
                with tarfile.open(fileobj=io.BytesIO(Payload), mode='r:gz') as Archive:
                    ArchiveEntry = Archive.getmember(Entry['archive_member'])
                    if not ArchiveEntry.isfile() or ArchiveEntry.size > 4194304:
                        raise RuntimeError('Invalid font archive entry: ' + Entry['archive_member'])
                    Payload = Archive.extractfile(ArchiveEntry).read()
                if Entry.get('decode') == 'woff':
                    from fontTools.ttLib import TTFont
                    with TTFont(io.BytesIO(Payload), recalcTimestamp=False) as Typeface:
                        Typeface.flavor = None
                        Converted = io.BytesIO()
                        Typeface.save(Converted)
                        Payload = Converted.getvalue()

            if hashlib.sha256(Payload).hexdigest() != Entry['sha256']:
                raise RuntimeError('Font SHA-256 mismatch: ' + Entry['file'])
            WriteAtomic(Destination, Payload)
            print('[Font]     {0}/{1}'.format(Entry['family'], Entry['file']), flush=True)

    GenerateCatalogue(Arguments.root)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(Main())
    except Exception as Error:
        print('Font preparation failed:', Error)
        raise SystemExit(1)
