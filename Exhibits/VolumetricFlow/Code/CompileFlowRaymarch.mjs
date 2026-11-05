// Compile the standalone raymarch graphics stages and verify their view/texture ABI.
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
    const Library = Session.loadModuleFromSource(fs.readFileSync("Engine/Render/Shader/VolumetricFlowRaymarch.slang", "utf8"),
                                                 "VolumetricFlowRaymarch", "/VolumetricFlowRaymarch.slang");
    assert.ok(Library, JSON.stringify(Compiler.getLastError()));
    for (const Name of ["ProjectFullscreenCorner", "IntegrateVolumeRadiance"])
    {
        const Entry = Library.findEntryPointByName(Name);
        assert.ok(Entry, JSON.stringify(Compiler.getLastError()));
        const Linked = Session.createCompositeComponentType([Library, Entry]).link();
        assert.ok(Linked, JSON.stringify(Compiler.getLastError()));
        const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
        assert.equal(Bytes.readUInt32LE(0), 0x07230203);
        const Layout = Linked.getLayout(0).toJsonObject();
        for (const [Caption, Index] of [["View",0], ["ScalarVolume",1], ["VelocityVolume",2],
                                        ["TrilinearFetch",3], ["PaletteRamp",4], ["IrradianceVolume",5]])
        {
            const Parameter = Layout.parameters.find(Element => Element.name === Caption);
            assert.ok(Parameter, `${Name}: missing ${Caption}`);
            assert.equal(Parameter.binding.index, Index, Caption);
            assert.equal(Parameter.binding.space ?? 0, 0, Caption);
        }
        const View = Layout.parameters.find(Parameter => Parameter.name === "View").type.elementType;
        assert.equal(View.sizes.find(Size => Size.kind === "uniform").value, 208);
        assert.deepEqual(View.fields.map(Field => Field.binding.offset),
            [0,12,16,28,32,44,48,60,64,76,80,84,88,92,96,100,104,108,112,124,128,132,136,140,144,148,152,156,160,164,168,172,176,180,184,188,192]);
        fs.writeFileSync(path.join(Destination, `${Name}.spv`), Bytes);
        console.log(`${Name}: ${Bytes.length} bytes; 208-byte view uniform and six bindings verified`);
    }
    console.log(`Compiler: ${Compiler.getVersionString()}`);
}
finally { Session.delete(); Global.delete(); }
