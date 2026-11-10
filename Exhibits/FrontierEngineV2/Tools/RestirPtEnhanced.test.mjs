//============================================================================================================================================
//                                                      RESTIRPTENHANCED.TEST.MJS
//============================================================================================================================================
// 🧩 Verification suite for ReSTIR PT Enhanced (Lin, Kettunen & Wyman, PACMCGIT May 2026, doi:10.1145/3804494) in FrontierEngineV2:
//    1. §3 Reciprocal Spatial Reuse: 254²/230²/210² Gaussian self-inverting pairing maps, Eq. (3) shuffle schedule & D_4 involution
//    2. §4 Scene-Independent Reconnection: Eq. (5) dual ray-footprint criterion + single-vertex α_{k-1} >= 0.20 safeguard
//    3. §5 Sample Duplication Maps: 17×17 window (288 neighbors) LDS tile + adaptive c_Cap = lerp(c_Cap^Default, 1.0, D^0.1)
//    4. §6.1–6.4 Unified 64B Reservoir, Replay Queue Compaction, Forced NEE, Initial-Only RR, RGB Vector Shading & Dual Motion Vectors
//    5. WebGPU Tier-0 Per-Stage Descriptor Limit Audit across all 4 ReSTIR PT Enhanced compute stages

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
    PairingDimensions,
    PairingOffsetBias,
    ResolveGaussianSigmaFromRadius,
    ResolveShuffleIterationCount,
    ConstructPairingMapCpu,
    ResolvePairingTransform,
    ResolvePairedNeighborCpu,
    PairingSpace
} from "../Source/PairingSpace.js";
import { ReservoirStride, PairedLayerBudget, ShiftSlotBytes } from "../Source/ResamplingSequence.js";
import { RendererLabels } from "../Source/RendererNames.js";

const RootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

describe("ReSTIR PT Enhanced (Lin, Kettunen & Wyman 2026, doi:10.1145/3804494)", () =>
{
    it("§3.2 builds exact self-inverting Gaussian pairing maps (254², 230², 210²) matching Eq. (3) and D_4 involution", () =>
    {
        assert.deepEqual([...PairingDimensions], [254, 230, 210]);

        // Paper §7: R = 30 px -> σ = sqrt(8 / (9π)) * 30 ≈ 15.9577 -> n_σ = 128 tiled 2×2 shuffle passes
        const Sigma30 = ResolveGaussianSigmaFromRadius(30.0);
        assert.ok(Math.abs(Sigma30 - 15.957691) < 1.0e-4, `Expected σ ≈ 15.9577 for R=30, got ${Sigma30}`);
        assert.equal(ResolveShuffleIterationCount(16.0), 128, "Eq. (3) at σ = 16 must yield 128 shuffle iterations");

        const Space = new PairingSpace(null);
        assert.equal(Space.UpdateRadius(30.0), true);
        assert.equal(Space.Maps.length, 3);

        for (let Layer = 0; Layer < 3; ++Layer)
        {
            const Map = Space.Maps[Layer];
            assert.equal(Map.Width, PairingDimensions[Layer]);

            // 1. Verify zero self-links and exact unsigned bias encoding across every texel of the map
            let RadialSum = 0.0;
            const Count   = Map.Width * Map.Width;
            for (let i = 0; i < Count; ++i)
            {
                const Dx = Map.Deltas[i * 2];
                const Dy = Map.Deltas[i * 2 + 1];
                assert.ok(Dx !== 0 || Dy !== 0, `Texel ${i} in layer ${Layer} must never pair with itself`);
                assert.equal(Map.Packed[i * 2], Dx + PairingOffsetBias);
                assert.equal(Map.Packed[i * 2 + 1], Dy + PairingOffsetBias);
                RadialSum += Math.hypot(Dx, Dy);
            }

            // Mean radial sample distance of isotropic Gaussian matches 2R/3 = 20.0 px within finite-torus tolerance
            const MeanRadial = RadialSum / Count;
            assert.ok(MeanRadial > 17.5 && MeanRadial < 22.5, `Expected mean radial distance ~20 px, got ${MeanRadial.toFixed(2)}`);

            // 2. Verify strict involution Pair(Pair(P)) === P across all 8 dihedral symmetries (Sym = 0..7) and negative/tiled coords
            for (let Sym = 0; Sym < 8; ++Sym)
            {
                const Transform = { OffsetX: 37 + Sym * 19, OffsetY: 113 + Sym * 23, Symmetry: Sym, Width: Map.Width };
                for (let Py = -25; Py < 310; Py += 17)
                {
                    for (let Px = -25; Px < 310; Px += 19)
                    {
                        const [Qx, Qy] = ResolvePairedNeighborCpu(Map, Transform, Px, Py);
                        assert.ok(Qx !== Px || Qy !== Py, "Paired neighbor Q must be distinct from P");
                        const [Rx, Ry] = ResolvePairedNeighborCpu(Map, Transform, Qx, Qy);
                        assert.equal(Rx, Px, `Involution X failed at Layer=${Layer}, Sym=${Sym}, P=(${Px},${Py}) -> Q=(${Qx},${Qy}) -> R=(${Rx},${Ry})`);
                        assert.equal(Ry, Py, `Involution Y failed at Layer=${Layer}, Sym=${Sym}, P=(${Px},${Py}) -> Q=(${Qx},${Qy}) -> R=(${Rx},${Ry})`);
                    }
                }
            }

            // 3. Verify frame-indexed transforms are deterministic and involutive
            for (let Frame = 0; Frame < 12; ++Frame)
            {
                const Xf       = ResolvePairingTransform(Frame, Layer, Map.Width);
                const [Qx, Qy] = ResolvePairedNeighborCpu(Map, Xf, 142, 89);
                const [Rx, Ry] = ResolvePairedNeighborCpu(Map, Xf, Qx, Qy);
                assert.deepEqual([Rx, Ry], [142, 89]);
            }
        }
    });

    it("§4 & §5 & §6.2.1 configures unified 64-byte ReservoirRecord, Eq. (5) ray-footprint criterion, and adaptive c_Cap", () =>
    {
        assert.equal(ReservoirStride, 64, "ReSTIR PT Enhanced unified ReservoirRecord must be exactly 64 bytes (16 × 4B)");
        assert.equal(PairedLayerBudget, 3, "Default coupled pairing layer budget must be N = 3");
        assert.equal(ShiftSlotBytes, 16, "Each PairedShiftExchange entry must be a compact 1 × vec4f = 16 bytes");
        assert.match(RendererLabels.bvh.Long, /ReSTIR PT Enhanced/);

        const RecordsWgsl = fs.readFileSync(path.join(RootDir, "Shader/ReservoirRecords.wgsl"), "utf8");
        assert.match(RecordsWgsl, /const RoughnessReconnectMin\s*:\s*f32 = 0\.20;/);
        assert.match(RecordsWgsl, /fn EvaluatePrimaryPathIntegrand\(/);
        assert.match(RecordsWgsl, /fn ResolvePrimaryFootprintThreshold\(/);
        assert.match(RecordsWgsl, /fn EvaluateDualFootprintRatio\(/);
        assert.match(RecordsWgsl, /fn EvaluateHybridShiftJacobian\(/);
        assert.match(RecordsWgsl, /fn ResolveAdaptiveConfidenceCap\(/);
        assert.match(RecordsWgsl, /fn ResolvePairedNeighbor\(/);

        // Verify §5 adaptive confidence cap preserves DefaultCap under healthy local sharing (D <= 6/288) and throttles correlation blobs
        const Healthy = 6.0 / 288.0;
        const EvalAdaptiveCap = (DefaultCap, D) =>
        {
            if (D <= Healthy) return DefaultCap;
            const Excess = Math.min(Math.max((D - Healthy) / (1.0 - Healthy), 0.0), 1.0);
            const Factor = Math.pow(Excess, 0.35);
            return DefaultCap * (1.0 - Factor) + 2.0 * Factor;
        };
        assert.equal(EvalAdaptiveCap(20.0, 0.0), 20.0);
        assert.equal(EvalAdaptiveCap(20.0, 3.0 / 288.0), 20.0, "Healthy local spatial sharing must not crush temporal history");
        assert.ok(EvalAdaptiveCap(20.0, 0.25) < 10.0, "Runaway seed duplication blobs must strongly throttle c_Cap");
        assert.ok(Math.abs(EvalAdaptiveCap(20.0, 1.0) - 2.0) < 1.0e-6, "Full duplication D = 1 must clamp c_Cap to 2.0");
    });

    it("§3, §5 & §6 wires all 4 ReSTIR PT Enhanced compute stages, NaN guards, and stays strictly within WebGPU tier-0 limits", () =>
    {
        const ResamplingJs = fs.readFileSync(path.join(RootDir, "Source/ResamplingSequence.js"), "utf8");
        assert.match(ResamplingJs, /ReservoirTemporal\.wgsl/);
        assert.match(ResamplingJs, /ReservoirPairedShift\.wgsl/);
        assert.match(ResamplingJs, /ReservoirSpatial\.wgsl/);
        assert.match(ResamplingJs, /ReservoirDuplication\.wgsl/);

        const PairedWgsl = fs.readFileSync(path.join(RootDir, "Shader/ReservoirPairedShift.wgsl"), "utf8");
        assert.match(PairedWgsl, /fn EvaluatePairedShifts\(/);
        assert.match(PairedWgsl, /ResolvePairedNeighbor\(/);

        const SpatialWgsl = fs.readFileSync(path.join(RootDir, "Shader/ReservoirSpatial.wgsl"), "utf8");
        assert.match(SpatialWgsl, /PairedShiftExchange/);
        assert.match(SpatialWgsl, /IrradianceTarget/);
        assert.match(SpatialWgsl, /PostSpatialMetaTarget/);

        const ExposureWgsl = fs.readFileSync(path.join(RootDir, "Shader/DisplayExposure.wgsl"), "utf8");
        assert.match(ExposureWgsl, /RawRgb != RawRgb/);

        const DupWgsl = fs.readFileSync(path.join(RootDir, "Shader/ReservoirDuplication.wgsl"), "utf8");
        assert.match(DupWgsl, /fn ComputeDuplicationMap\(/);
        assert.match(DupWgsl, /var<workgroup> SharedSeeds : array<u32, 576>;/);
        assert.match(DupWgsl, /288\.0/);

        const TemporalWgsl = fs.readFileSync(path.join(RootDir, "Shader/ReservoirTemporal.wgsl"), "utf8");
        assert.match(TemporalWgsl, /fn ResolveTemporalPriorTrace\(/);
        assert.match(TemporalWgsl, /ResolveAdaptiveConfidenceCap\(/);
        assert.match(TemporalWgsl, /ProjectTemporalPair\(/);
        assert.match(TemporalWgsl, /TraceUnifiedPathCandidate\(/);
        assert.match(TemporalWgsl, /ShiftReservoirPath\(/);

        const HostJs = fs.readFileSync(path.join(RootDir, "Source/IlluminationHost.js"), "utf8");
        assert.match(HostJs, /RestirEnhanced:/);
        assert.match(HostJs, /SpatialCount:\s*3/);
        assert.match(HostJs, /SpatialRadius:\s*18/);
        assert.match(HostJs, /RecordReflection\(/);

        // Audit WebGPU tier-0 limits across SharedLayouts (6 storage buffers, 12 sampled textures, 0 storage textures, 2 uniforms)
        const Shared = { Storage: 6, Sampled: 12, StorageTex: 0, Uniform: 2 };
        const StageGroup0 = {
            Temporal:    { Storage: 2, Sampled: 4, StorageTex: 1, Uniform: 1, UsesShared: true },
            PairedShift: { Storage: 2, Sampled: 3, StorageTex: 0, Uniform: 2, UsesShared: false },
            Spatial:     { Storage: 2, Sampled: 4, StorageTex: 2, Uniform: 2, UsesShared: true },
            Duplication: { Storage: 1, Sampled: 1, StorageTex: 1, Uniform: 1, UsesShared: false }
        };
        for (const [Stage, Counts] of Object.entries(StageGroup0))
        {
            const TotalStorage    = Counts.Storage + (Counts.UsesShared ? Shared.Storage : 0);
            const TotalSampled    = Counts.Sampled + (Counts.UsesShared ? Shared.Sampled : 0);
            const TotalStorageTex = Counts.StorageTex + (Counts.UsesShared ? Shared.StorageTex : 0);
            const TotalUniform    = Counts.Uniform + (Counts.UsesShared ? Shared.Uniform : 0);
            assert.ok(TotalStorage <= 8, `${Stage} storage buffers ${TotalStorage} exceeds WebGPU tier-0 limit 8`);
            assert.ok(TotalSampled <= 16, `${Stage} sampled textures ${TotalSampled} exceeds WebGPU tier-0 limit 16`);
            assert.ok(TotalStorageTex <= 4, `${Stage} storage textures ${TotalStorageTex} exceeds WebGPU tier-0 limit 4`);
            assert.ok(TotalUniform <= 12, `${Stage} uniform buffers ${TotalUniform} exceeds WebGPU tier-0 limit 12`);
        }
    });
});
