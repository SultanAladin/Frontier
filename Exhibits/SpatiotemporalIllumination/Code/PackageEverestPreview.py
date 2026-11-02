#==========================================================================================================================================
#                                              PACKAGE EVEREST PREVIEW
#==========================================================================================================================================
# 🧩 Refresh the prior Windows package with the current cross-built executables and staged project content.

from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zipfile

Root = Path(__file__).resolve().parents[3]
Prefix = 'Everest-GTX-ReSTIR-Win64/'


def VerifyWindowsX64(Data):
    if len(Data) < 64 or Data[:2] != b'MZ':
        raise ValueError('Missing DOS header')
    Offset = struct.unpack_from('<I', Data, 0x3c)[0]
    if Offset + 26 > len(Data) or Data[Offset:Offset + 4] != b'PE\0\0':
        raise ValueError('Missing PE header')
    if struct.unpack_from('<H', Data, Offset + 4)[0] != 0x8664 or struct.unpack_from('<H', Data, Offset + 24)[0] != 0x20b:
        raise ValueError('Expected Windows x64 PE32+')


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--build', type=Path, required=True)
    Parser.add_argument('--output', type=Path, required=True)
    Args = Parser.parse_args()
    Build = Args.build.resolve()
    Revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Root, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=Root, text=True):
        raise ValueError('Commit tracked source changes before packaging')

    Previous = Root / 'Binaries/Everest-GTX-ReSTIR-Windows-x64.zip'
    with zipfile.ZipFile(Previous) as Archive:
        if Archive.testzip() is not None:
            raise ValueError('Previous content archive is damaged')
        Files = {Name: Archive.read(Name) for Name in Archive.namelist() if not Name.endswith('/')}
    for Executable, Destination in [('Everest.exe', 'Projects/Everest/Everest.exe'), ('Frontier.exe', 'Frontier.exe')]:
        Data = (Build / Executable).read_bytes()
        VerifyWindowsX64(Data)
        Files[Prefix + Destination] = Data
    Content = Root / 'Projects/Everest/Content'
    for Pathname in sorted(Content.rglob('*')):
        if Pathname.is_file():
            Files[Prefix + 'Projects/Everest/Content/' + Pathname.relative_to(Content).as_posix()] = Pathname.read_bytes()
    ShaderDirectory = Build / 'ShaderArchives/Environment'
    for Shader in ShaderDirectory.glob('*.spv'):
        Packaged = Files[Prefix + 'Projects/Everest/Content/ShaderArchives/Environment/' + Shader.name]
        if Packaged != Shader.read_bytes():
            raise ValueError('Staged shader differs from the cross-build: ' + Shader.name)
    if not (ShaderDirectory / 'SolveSurfaceRadiosity.spv').is_file():
        raise ValueError('Production shader binaries missing')
    Files[Prefix + 'Projects/Everest/Everest.projectspace'] = (Root / 'Projects/Everest/Everest.projectspace').read_bytes()
    Readme = f'''EVEREST WINDOWS X64 — UNSIGNED PARITY WORK-IN-PROGRESS\n\nSource revision: {Revision}\n\nExtract the entire archive. Launch Projects\\Everest\\Everest.exe for Everest,\nor Frontier.exe to open Everest in the Frontier hub. Vulkan 1.3 driver required.\n\nThis is not a 1:1 Webdemo port. Screen-depth traversal, ReSTIR GI reservoirs,\nreference surface cards, display TAA/exposure and environment integration remain.\nBuilt on Linux with Zig 0.13.0. No Windows startup, Radeon GPU, antivirus scan,\nor publisher-signature verification was performed. The executables are unsigned.\n'''.encode()
    Files[Prefix + 'README-TESTING.txt'] = Readme
    Files[Prefix + 'Projects/Everest/README-TESTING.txt'] = Readme
    Manifest = {'source_revision': Revision, 'toolchain': 'Zig 0.13.0, Linux cross-build',
                'windows_runtime_verified': False, 'signed': False, 'malware_scanned': False,
                'files_sha256': {Name[len(Prefix):]: hashlib.sha256(Data).hexdigest()
                                 for Name, Data in sorted(Files.items())}}
    Files[Prefix + 'PreviewManifest.json'] = (json.dumps(Manifest, indent=2) + '\n').encode()
    Args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(Args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as Archive:
        for Name, Data in sorted(Files.items()):
            Archive.writestr(Name, Data)
    with zipfile.ZipFile(Args.output) as Archive:
        if Archive.testzip() is not None:
            raise ValueError('Output archive failed CRC validation')
    Digest = hashlib.sha256(Args.output.read_bytes()).hexdigest()
    Args.output.with_name(Args.output.name + '.sha256').write_text(f'{Digest}  {Args.output.name}\n')
    print(f'{Args.output}: {len(Files)} files, SHA-256 {Digest}')


if __name__ == '__main__':
    Main()
