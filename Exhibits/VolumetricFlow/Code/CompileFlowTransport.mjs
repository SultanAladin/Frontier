// Compile the retained flow editor's native Slang transport/source entries and inspect their ABI.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

const [CompilerPath, Destination] = process.argv.slice(2);
assert.ok(CompilerPath && Destination, "Expected the slang-loader WASM entry and an output directory");
const Compiler = await (await import(pathToFileURL(path.resolve(CompilerPath)).href)).default();
const Global = Compiler.createGlobalSession();
const Session = Global.createSession(Compiler.getCompileTargets().find(Entry => Entry.name === "SPIRV").value);
try
{
    fs.mkdirSync(Destination, { recursive: true });
    const Name = "VolumetricFlowTransport";
    const Source = fs.readFileSync(`Engine/Render/Shader/${Name}.slang`, "utf8");
    const Library = Session.loadModuleFromSource(Source, Name, `/${Name}.slang`);
    assert.ok(Library, JSON.stringify(Compiler.getLastError()));
    for (const Name of ["AdvectVelocityCarrier", "AdvectScalarCarrier", "AccumulateInfluence", "InjectScalarSource"])
    {
        const Entry = Library.findEntryPointByName(Name);
        assert.ok(Entry, JSON.stringify(Compiler.getLastError()));
        const Linked = Session.createCompositeComponentType([Library, Entry]).link();
        assert.ok(Linked, JSON.stringify(Compiler.getLastError()));
        const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
        assert.equal(Bytes.readUInt32LE(0), 0x07230203);
        const Layout = Linked.getLayout(0).toJsonObject();
        for (const [Caption, Index] of [["Flow",0], ["VelocitySource",1], ["ScalarSource",2], ["TrilinearFetch",3],
                                        ["VelocityTarget",4], ["ScalarTarget",5]])
        {
            const Parameter = Layout.parameters.find(Element => Element.name === Caption);
            assert.ok(Parameter, `${Name}: missing ${Caption}`);
            assert.equal(Parameter.binding.index, Index, Caption);
            assert.equal(Parameter.binding.space ?? 0, 0, Caption);
        }
        const Flow = Layout.parameters.find(Parameter => Parameter.name === "Flow").type.elementType;
        assert.equal(Flow.fields.length, 47);
        assert.deepEqual(Flow.fields.map(Field => Field.binding.offset), Array.from({ length: 47 }, (_, Index) => Index * 4));
        let Workgroup = false, ImageFormat = false;
        for (let Offset = 20; Offset < Bytes.length;)
        {
            const Header = Bytes.readUInt32LE(Offset), Words = Header >>> 16, Opcode = Header & 0xffff;
            assert.ok(Words > 0 && Offset + Words * 4 <= Bytes.length, "Malformed SPIR-V instruction");
            if (Opcode === 16 && Words >= 6 && Bytes.readUInt32LE(Offset + 8) === 17)
                Workgroup = [8, 8, 4].every((Expected, Index) => Bytes.readUInt32LE(Offset + 12 + Index * 4) === Expected);
            if (Opcode === 25 && Words >= 9 && Bytes.readUInt32LE(Offset + 28) === 2 && Bytes.readUInt32LE(Offset + 32) === 2)
                ImageFormat = true;
            Offset += Words * 4;
        }
        assert.ok(Workgroup && ImageFormat, `${Name}: expected 8×8×4 and rgba16f output`);
        fs.writeFileSync(path.join(Destination, `${Name}.spv`), Bytes);
        fs.writeFileSync(path.join(Destination, `${Name}.json`), JSON.stringify(Layout, null, 2));
        console.log(`${Name}: ${Bytes.length} bytes; 47 flow fields, 6 bindings and rgba16f output verified`);
    }
    console.log(`Compiler: ${Compiler.getVersionString()}`);
}
finally { Session.delete(); Global.delete(); }
