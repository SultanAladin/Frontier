//============================================================================================================================================
//                                                      COMPILEMESHBAKECOMPUTE.MJS
//============================================================================================================================================
// 🧩 Compile and authenticate the GPU mesh-bake compute entry point via a portable Slang WASM compiler, the
// same pattern Automation/CompileVectorSurface.mjs already uses for VectorSurfaceCompute.

import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import {fileURLToPath, pathToFileURL} from 'node:url';
import Assert from 'node:assert/strict';

const [CompilerPath, Destination] = process.argv.slice(2);
Assert(CompilerPath && Destination, 'Expected Slang WASM entry path and archive destination');
const Compiler = await (await import(pathToFileURL(Path.resolve(CompilerPath)).href)).default();
const Global = Compiler.createGlobalSession();
const Target = Compiler.getCompileTargets().find(Choice => Choice.name === 'SPIRV');
Assert(Global && Target, 'Expected portable SPIR-V compiler');
const Root = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)), '..');
const SourcePath = Path.join(Root, 'Engine/Render/Shader/MeshBakeCompute.slang');
const Source = Fs.readFileSync(SourcePath, 'utf8');
const Session = Global.createSession(Target.value);
const Module = Session.loadModuleFromSource(Source, 'MeshBakeCompute', '/MeshBakeCompute.slang');
Assert(Module, JSON.stringify(Compiler.getLastError()));
const Entry = Module.findEntryPointByName('MeshBakeCompute');
Assert(Entry, JSON.stringify(Compiler.getLastError()));
const Linked = Session.createCompositeComponentType([Module, Entry]).link();
Assert(Linked, JSON.stringify(Compiler.getLastError()));
const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
Assert(Bytes.length >= 20 && Bytes.readUInt32LE(0) === 0x07230203, 'Expected valid SPIR-V');
const Reflection = Linked.getLayout(0).toJsonObject();
// 📝 Every binding is a plain storage buffer at set 0, indices 0..7 - see Engine/Render/MeshBakeGpu.cpp's
// descriptor set layout, which must stay in this exact order.
const ExpectedBindings = ['LowVertices', 'LowIndices', 'HighTriangles', 'BvhNodes', 'BvhOrder', 'Settings', 'ChannelSlots', 'Output'];
for (let Index = 0; Index < ExpectedBindings.length; ++Index)
{
    const Parameter = Reflection.parameters?.find(Candidate => Candidate.name === ExpectedBindings[Index]);
    Assert(Parameter, `Missing expected binding ${ExpectedBindings[Index]}`);
    Assert.equal(Parameter.binding?.index, Index, `${ExpectedBindings[Index]} must sit at binding ${Index}`);
}
Fs.mkdirSync(Destination, {recursive: true});
Fs.writeFileSync(Path.join(Destination, 'MeshBakeCompute.spv'), Bytes);
const Manifest = {compiler: Compiler.getVersionString(), entry: 'MeshBakeCompute', target: 'SPIRV', bindings: ExpectedBindings,
    source_sha256: Crypto.createHash('sha256').update(Source).digest('hex'), binary_sha256: Crypto.createHash('sha256').update(Bytes).digest('hex')};
Fs.writeFileSync(Path.join(Destination, 'MeshBakeCompilation.json'), JSON.stringify(Manifest, null, 2) + '\n');
console.log(`PASS: MeshBakeCompute compiled to ${Bytes.length} bytes of valid SPIR-V with every expected binding in place.`);
