import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { DecodePhotometricDistribution, PhotometricWidth, PhotometricHeight } from "../Source/PhotometricDistribution.js";
import { LightingSystem, LightBufferBytes } from "../Source/LightingSystem.js";

const Text = fs.readFileSync(new URL("../Archive/Photometry/BEGA-50975.ies", import.meta.url), "utf8");
const Fixture = (Vertical, Horizontal, Samples, Multiplier = 1) => `IESNA:LM-63-2002\nTILT=NONE\n1 -1 ${Multiplier} ${Vertical.length} ${Horizontal.length} 1 2 0 0 0\n1 1 10\n${Vertical.join(" ")}\n${Horizontal.join(" ")}\n${Samples.join(" ")}`;

test("real BEGA profile retains absolute candela, angular samples and integrated flux", () =>
{
    const Profile = DecodePhotometricDistribution(Text);
    assert.match(Profile.Description, /50975/);
    assert.equal(Profile.Vertical.length, 73);
    assert.equal(Profile.Horizontal.length, 1);
    assert.equal(Profile.Lumens, -1);
    assert.equal(Profile.Evaluate(0, 0), 330.8);
    assert.equal(Profile.Evaluate(0, 127), 330.8);
    assert.equal(Profile.Samples.length, PhotometricWidth * PhotometricHeight);
    assert.ok(Profile.Flux > 200 && Profile.Flux < 400, Profile.Flux);
    assert.equal(Profile.Evaluate(180, 0), 0);
});

test("LM-63 interpolation, candela multiplier, quadrant and bilateral symmetry", () =>
{
    const Quadrant = DecodePhotometricDistribution(Fixture([0, 90, 180], [0, 90], [10, 20, 30, 30, 40, 50], 2));
    assert.equal(Quadrant.Evaluate(45, 45), 50);
    assert.equal(Quadrant.Evaluate(45, 135), 50);
    assert.equal(Quadrant.Evaluate(45, 225), 50);
    const Half = DecodePhotometricDistribution(Fixture([0, 180], [0, 180], [10, 10, 30, 30]));
    assert.equal(Half.Evaluate(90, 90), Half.Evaluate(90, 270));
    const Full = DecodePhotometricDistribution(Fixture([0, 180], [0, 180, 360], [10, 10, 20, 20, 10, 10]));
    assert.equal(Full.Evaluate(20, 0), Full.Evaluate(20, 360));
    assert.ok(Math.abs(DecodePhotometricDistribution(Fixture([0, 180], [0], [1, 1])).Flux - 4 * Math.PI) < 1e-8);
});

test("malformed or unsupported photometry is rejected without corrupting a loaded profile", () =>
{
    const Lighting = new LightingSystem();
    Lighting.AssignPhotometry(Text);
    const Profile = Lighting.Profile;
    for (const Invalid of ["junk", Text.replace("TILT=NONE", "TILT=INCLUDE"),
        Fixture([0, 0], [0], [1, 2]), Fixture([0, 180], [0], [-1, 2]),
        Fixture([0, 180], [0], [1]), Fixture([0, 180], [0], [1, Infinity]),
        Fixture([0, 180], [0], [1, 2]).replace("2 1 1 2 0", "2 1 2 2 0")])
        assert.throws(() => Lighting.AssignPhotometry(Invalid));
    assert.equal(Lighting.Profile, Profile);
});

test("uniform light table fits WebGPU's 64KiB binding and agrees with WGSL scalar offsets", () =>
{
    const Lighting = new LightingSystem();
    Lighting.AssignPhotometry(Text);
    Lighting.BufferData[15] = 1;
    Lighting.BufferData[7] = 1000;
    const Table = Lighting.EncodeLightTable();
    assert.ok(LightBufferBytes < 65536);
    assert.equal(Table[1544], 1);
    assert.equal(Table[1548], Math.fround(330.8));
    Lighting.PhotometryEnabled = false;
    assert.equal(Lighting.EncodeLightTable()[1544], 0);
});

test("spatial masks retain all 32 lights and conservatively cover sampled contributing points", () =>
{
    const Lighting = new LightingSystem();
    for (let Index = 0; Index < 32; ++Index)
    {
        const Offset = Index * 16;
        Lighting.BufferData.set([50, 0, 10], Offset);
        Lighting.BufferData[Offset + 7] = 1000;
        Lighting.BufferData[Offset + 15] = 1;
    }
    Lighting.EncodeLightTable();
    assert.ok(Lighting.Masks.every(Mask => Mask === 0xffffffff));
    Lighting.BufferData.fill(0);
    for (let Index = 0; Index < 32; ++Index)
    {
        const Offset = Index * 16;
        Lighting.BufferData.set([-75 + Index * 7, -75 + (Index % 8) * 20, 5], Offset);
        Lighting.BufferData[Offset + 7] = 15;
        Lighting.BufferData[Offset + 15] = 1;
    }
    Lighting.EncodeLightTable();
    for (let Z = 0; Z < 30; Z += 3)
        for (let Y = -80; Y < 80; Y += 5)
            for (let X = -160; X < 180; X += 5)
            {
                const Cell = Math.floor((X + 160) / 340 * 16) + 16 * (Math.floor((Y + 80) / 160 * 16) + 16 * Math.floor((Z + 2) / 30 * 4));
                if (Z >= 28) continue; // Outside the grid uses the all-light fallback in WGSL.
                for (let Index = 0; Index < 32; ++Index)
                {
                    const Offset = Index * 16;
                    const Distance = (X - Lighting.BufferData[Offset]) ** 2 + (Y - Lighting.BufferData[Offset + 1]) ** 2 + (Z - 5) ** 2;
                    if (Distance < 225) assert.notEqual(Lighting.Masks[Cell] & (1 << Index), 0);
                }
            }
});

test("screen, smoke and encoded bounce fields have disjoint feature bits", () =>
{
    const Source = fs.readFileSync(new URL('../Source/IlluminationHost.js', import.meta.url), 'utf8');
    const Table = Source.match(/const FeatureBits = \{([\s\S]*?)\n\};/)[1];
    const Bits = [...Table.matchAll(/\w+:\s*(\d+)/g)].map(Match => Number(Match[1]));
    assert.equal(new Set(Bits).size, Bits.length);
    const EncodedCounts = (7 << 20) | (7 << 24) | (7 << 28);
    for (const Bit of Bits) {
        assert.equal(Bit & EncodedCounts, 0);
        assert.notEqual(Bit, 4096); // Smoke owns this bit in the inherited compositor.
        assert.equal(Bit & (Bit - 1), 0);
    }
    assert.match(Source, /Scalars\[128\] = Camera.Near/);
});
