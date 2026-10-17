#============================================================================================================================================
#                                                       INSPECTWINDOWSPACKAGE.PY
#============================================================================================================================================
# 🧩 Inspect a Windows bundle and retain reproducible integrity evidence without claiming runtime execution.

from pathlib import Path
import argparse
import hashlib
import json
import zipfile

import pefile

RepositoryRoot = Path(__file__).resolve().parents[3]


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--package', type=Path, default=RepositoryRoot / 'Binaries/ControlCentre-Windows-x64.zip')
    Arguments = Parser.parse_args()
    with zipfile.ZipFile(Arguments.package) as Archive:
        if Archive.testzip() is not None:
            raise RuntimeError('Corrupt archive entry')
        Metadata = json.loads(Archive.read('Package.json'))
        for Name, Digest in Metadata['files'].items():
            if hashlib.sha256(Archive.read(Name)).hexdigest() != Digest:
                raise RuntimeError('Bundled file digest differs: ' + Name)
        for Name, Digest in Metadata['source_sha256'].items():
            if hashlib.sha256((RepositoryRoot / Name).read_bytes()).hexdigest() != Digest:
                raise RuntimeError('Current source differs from package source: ' + Name)
        Executable = Archive.read('FrontierUI.exe')
        Image = pefile.PE(data=Executable)
        if Image.FILE_HEADER.Machine != 0x8664 or Image.OPTIONAL_HEADER.Magic != 0x20b:
            raise RuntimeError('Expected Windows x64 PE32+')
        Imports = [Entry.dll.decode('ascii') for Entry in Image.DIRECTORY_ENTRY_IMPORT]
        AllowedLibraries = {'d3d11.dll', 'd3dcompiler_47.dll', 'dwmapi.dll', 'kernel32.dll',
                            'user32.dll', 'gdi32.dll', 'shell32.dll'}
        for Name in Imports:
            if Name.lower() not in AllowedLibraries and not Name.lower().startswith('api-ms-win-crt-'):
                raise RuntimeError('Unexpected runtime dependency: ' + Name)
        Catalogue = json.loads(Archive.read('UI/Assets/Fonts/FontCatalogue.json'))
        for Face in Catalogue['faces']:
            if hashlib.sha256(Archive.read('UI/Assets/Fonts/' + Face['file'])).hexdigest() != Face['sha256']:
                raise RuntimeError('Bundled face differs from the catalogue')
        Results = RepositoryRoot / 'Exhibits/ControlCentre/Results'
        Results.mkdir(parents=True, exist_ok=True)
        BinaryReport = {'machine': hex(Image.FILE_HEADER.Machine), 'format': 'PE32+ Windows x64',
                        'sha256': hashlib.sha256(Executable).hexdigest(), 'imports': Imports,
                        'runtime_execution': 'not run; no Windows runtime available'}
        (Results / 'WindowsBinary.json').write_text(json.dumps(BinaryReport, indent=2) + '\n')
        Report = {'archive_sha256': hashlib.sha256(Arguments.package.read_bytes()).hexdigest(),
                  'archive_bytes': Arguments.package.stat().st_size, 'entries': len(Archive.namelist()),
                  'verified_payload_hashes': len(Metadata['files']),
                  'verified_current_source_hashes': len(Metadata['source_sha256']),
                  'font_faces': len(Catalogue['faces']), 'font_families': sorted({Face['family'] for Face in Catalogue['faces']}),
                  'binary_sha256': BinaryReport['sha256'], 'windows_runtime_verified': False,
                  'source_revision_at_packaging': Metadata['source_revision'],
                  'uncommitted_changes_at_packaging': Metadata['uncommitted_changes'],
                  'toolchain': Metadata['toolchain']}
        (Results / 'WindowsPackage.json').write_text(json.dumps(Report, indent=2) + '\n')
        Sources = dict(Metadata['source_sha256'])
        for Folder in ('Exhibits/ControlCentre/Code',):
            for FilePath in sorted((RepositoryRoot / Folder).rglob('*')):
                if FilePath.is_file() and FilePath.suffix in ('.cpp', '.h', '.py'):
                    Sources[FilePath.relative_to(RepositoryRoot).as_posix()] = hashlib.sha256(
                        FilePath.read_bytes()).hexdigest()
        (Results / 'SourceHashes.json').write_text(json.dumps(Sources, indent=2, sort_keys=True) + '\n')
        print(json.dumps(Report, indent=2))


if __name__ == '__main__':
    Main()
