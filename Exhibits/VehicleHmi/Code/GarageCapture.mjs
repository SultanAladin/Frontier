// Executes the real Garage renderer on a WebGPU device and reads its offscreen texture.
// The headless host has no 2D tablet canvas; these are preview captures, not tablet UI captures.
import assert from "node:assert/strict";
import path from "node:path";
import { EnableTextureReadback, ReadTexture, Summarise } from "../../../Experimental/VehicleHmiPrototype/Tools/GpuProbe.mjs";

await EnableTextureReadback();

const Captured = new Map();

async function Capture(Host, Label)
{
    const [Width, Height] = Host.Cabin.PreviewExtent;
    const Texture = Host.Cabin.PreviewTexture;
    const Pixels = await ReadTexture(Host.Exchange.Device, Texture, Width, Height, Texture.format.replace("-srgb", ""));
    const Summary = Summarise(Pixels, Width, Height);
    assert.equal(Summary.NonFinite, 0);
    assert.ok(Summary.Max[0] - Summary.Min[0] > 0.05, `${Label} must contain visible variation`);
    const Bytes = new Uint8ClampedArray(Pixels.length);
    for (let Index = 0; Index < Pixels.length; ++Index)
        Bytes[Index] = Math.round(Math.min(1, Math.max(0, Pixels[Index])) * 255);
    Captured.set(Label, Bytes);
    document.createElement("canvas").getContext("2d").putImageData(new ImageData(Bytes, Width, Height), 0, 0);
    if (process.env.HMI_CAPTURE_DIR)
        globalThis.SaveCapture(path.join(process.env.HMI_CAPTURE_DIR, `${Label}.png`));
    console.log(`Garage ${Label}:`, JSON.stringify({ Width, Height, Summary, Frames: Host.HmiViewport.FrameIndex }));
}

function DifferentPixels(Left, Right)
{
    let Changed = 0;
    for (let Index = 0; Index < Left.length; Index += 4)
        if (Math.max(Math.abs(Left[Index] - Right[Index]), Math.abs(Left[Index + 1] - Right[Index + 1]),
                     Math.abs(Left[Index + 2] - Right[Index + 2])) >= 4)
            ++Changed;
    return Changed;
}

export default {
    async BeforeFrame(Host, Frame)
    {
        Host.FixedStep = 1 / 60;
        if (Frame === 1 && !Captured.has("Solid"))
        {
            await Capture(Host, "Solid");
            Host.Cabin.Appearance.Apply({ Colour: "#8a1a1d", Matte: false, RippleOrigin: [1.2, -0.6, 0.75] });
            for (let Index = 0; Index < 8; ++Index)
                Host.MaterialRipple.Advance(0.05, Host.Vehicle.QueryBodyTransform());
        }
        if (Frame === 2 && !Captured.has("PaintRipple"))
        {
            await Capture(Host, "PaintRipple");
            Host.Cabin.Appearance.View = 3;
            for (let Index = 0; Index < 46; ++Index)
                Host.ScanEffect.Advance(0.05, 3, 3);
        }
    },
    async Report(Host)
    {
        assert.ok(Host.HmiViewport.FrameIndex >= 3, "the dedicated Garage renderer must have run");
        await Capture(Host, "XRayScan");
        const PaintChanged = DifferentPixels(Captured.get("Solid"), Captured.get("PaintRipple"));
        const ScanChanged = DifferentPixels(Captured.get("PaintRipple"), Captured.get("XRayScan"));
        assert.ok(PaintChanged > 100, `paint ripple changed too few pixels: ${PaintChanged}`);
        assert.ok(ScanChanged > 100, `scan changed too few pixels: ${ScanChanged}`);
        console.log(`Executed comparison: paint ${PaintChanged} pixels; X-ray ${ScanChanged} pixels changed by at least 4/255.`);
    }
};
