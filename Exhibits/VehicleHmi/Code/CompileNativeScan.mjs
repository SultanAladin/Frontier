// Compile all native scan scaffold entry points and verify the uniform/particle ABI.
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
    const Source = fs.readFileSync("Frontier/Engine/Rendering/Shaders/VehicleScan.slang", "utf8");
    const Library = Session.loadModuleFromSource(Source, "VehicleScan", "/VehicleScan.slang");
    assert.ok(Library, JSON.stringify(Compiler.getLastError()));
    for (const Name of ["GhostVertex", "GhostFragment", "WireframeVertex", "WireframeFragment",
                        "CurtainVertex", "CurtainFragment", "SparkleVertex", "SparkleFragment"])
    {
        const Entry = Library.findEntryPointByName(Name);
        assert.ok(Entry, JSON.stringify(Compiler.getLastError()));
        const Linked = Session.createCompositeComponentType([Library, Entry]).link();
        assert.ok(Linked, JSON.stringify(Compiler.getLastError()));
        const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
        assert.equal(Bytes.readUInt32LE(0), 0x07230203);
        const Layout = Linked.getLayout(0).toJsonObject();
        const Frame = Layout.parameters.find(Parameter => Parameter.name === "ScanFrame");
        const Particles = Layout.parameters.find(Parameter => Parameter.name === "SparkleCloud");
        assert.equal(Frame.binding.index, 0);
        assert.equal(Particles.binding.index, 1);
        assert.equal(Frame.type.elementType.sizes.find(Size => Size.kind === "uniform").value, 208);
        assert.deepEqual(Frame.type.elementType.fields.map(Field => Field.binding.offset), [0,64,80,96,112,128,192]);
        fs.writeFileSync(path.join(Destination, `${Name}.spv`), Bytes);
        console.log(`${Name}: ${Bytes.length} bytes, 208-byte frame layout verified`);
    }
    console.log(`Compiler: ${Compiler.getVersionString()}`);
}
finally { Session.delete(); Global.delete(); }
