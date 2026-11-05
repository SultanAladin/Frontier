// Compile native plume entries and check the original WebGPU resource and uniform ABI.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

const [CompilerPath, Destination] = process.argv.slice(2);
assert.ok(CompilerPath && Destination, "Expected the slang-loader WASM entry and an output directory");
const Compiler = await (await import(pathToFileURL(path.resolve(CompilerPath)).href)).default();
const Global = Compiler.createGlobalSession();
const Target = Compiler.getCompileTargets().find(Entry => Entry.name === "SPIRV");
const Session = Global.createSession(Target.value);
const Programs = [
    { Name: "PlumePressure", Entries: ["ResolveDivergence", "RelaxPressure", "ProjectVelocity"],
      Resources: [["VelocityLattice", 1], ["Divergence", 2], ["PressureSource", 3], ["PressureTarget", 4], ["LatticeTarget", 5]],
      ImageEntry: "ProjectVelocity" },
    { Name: "PlumeVorticity", Entries: ["ResolveCurl", "ConfineVorticity"],
      Resources: [["VelocityLattice", 1], ["Curl", 2], ["LatticeTarget", 3]], ImageEntry: "ConfineVorticity" }
];
try
{
    fs.mkdirSync(Destination, { recursive: true });
    for (const Program of Programs)
    {
        const Source = fs.readFileSync(`Engine/Render/Shader/${Program.Name}.slang`, "utf8");
        const Library = Session.loadModuleFromSource(Source, Program.Name, `/${Program.Name}.slang`);
        assert.ok(Library, JSON.stringify(Compiler.getLastError()));
        for (const Name of Program.Entries)
        {
            const Entry = Library.findEntryPointByName(Name);
            assert.ok(Entry, JSON.stringify(Compiler.getLastError()));
            const Composite = Session.createCompositeComponentType([Library, Entry]);
            assert.ok(Composite, JSON.stringify(Compiler.getLastError()));
            const Linked = Composite.link();
            assert.ok(Linked, JSON.stringify(Compiler.getLastError()));
            const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
            assert.equal(Bytes.readUInt32LE(0), 0x07230203);
            const Layout = Linked.getLayout(0).toJsonObject();
            const Uniform = Layout.parameters.find(Parameter => Parameter.name === "Plume");
            assert.equal(Uniform.binding.index, 0);
            assert.equal(Uniform.type.elementType.sizes.find(Size => Size.kind === "uniform").value, 256);
            const Fields = ["VelocityOrigin", "PriorVelocityOrigin", "VelocityExtent", "ThermoExtent", "Lattice",
                            "Forces", "Retention", "Wind", "SourceMinimum", "SourceMaximum", "Optical", "Sun",
                            "Combustion", "Scattering", "Detail", "Flame"];
            assert.deepEqual(Uniform.type.elementType.fields.map(Field => [Field.name, Field.binding.offset]),
                             Fields.map((Caption, Index) => [Caption, Index * 16]));
            // OpExecutionMode LocalSize 4,4,4 and the output OpTypeImage Rgba16f.
            let Workgroup = false, StorageFormat = false;
            for (let Offset = 20; Offset < Bytes.length;)
            {
                const Header = Bytes.readUInt32LE(Offset), Words = Header >>> 16, Opcode = Header & 0xffff;
                assert.ok(Words > 0 && Offset + Words * 4 <= Bytes.length, "Malformed SPIR-V instruction");
                if (Opcode === 16 && Words >= 6 && Bytes.readUInt32LE(Offset + 8) === 17)
                    Workgroup = [12, 16, 20].every(Byte => Bytes.readUInt32LE(Offset + Byte) === 4);
                if (Opcode === 25 && Words >= 9 && Bytes.readUInt32LE(Offset + 28) === 2 && Bytes.readUInt32LE(Offset + 32) === 2)
                    StorageFormat = true; // Sampled=2 (storage), format=2 (Rgba16f).
                Offset += Words * 4;
            }
            assert.ok(Workgroup, `${Name} must use 4×4×4 workgroups`);
            if (Name === Program.ImageEntry) assert.ok(StorageFormat, "Output image must be rgba16float");
            for (const [Resource, Index] of Program.Resources)
            {
                const Parameter = Layout.parameters.find(Entry => Entry.name === Resource);
                assert.equal(Parameter.binding.index, Index, Resource);
                assert.equal(Parameter.binding.space ?? 0, 0, Resource);
            }
            fs.writeFileSync(path.join(Destination, `${Name}.spv`), Bytes);
            fs.writeFileSync(path.join(Destination, `${Name}.json`), JSON.stringify(Layout, null, 2));
            console.log(`${Name}: SPIR-V ${Bytes.length} bytes; uniform 256 bytes; ${Program.Resources.length + 1} resources verified`);
        }
    }
    console.log(`Compiler: ${Compiler.getVersionString()}`);
}
finally
{
    Session.delete();
    Global.delete();
}
