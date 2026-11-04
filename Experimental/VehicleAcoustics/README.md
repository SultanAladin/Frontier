# Vehicle Acoustics Bench

## Purpose

Nine cars, one synthesiser, driven from physics. Every engine note is generated per audio **sample** from crank angle — no sample loops and
no dependence on the drawn frame rate.

```bash
cd Experimental/VehicleAcoustics
python3 Tools/Serve.py            # http://localhost:8080
npm test                          # 9 node tests + page smoke + worker smoke
npm run evidence                  # regenerates Validation/
```

## Why this does not go quiet at low frame rate

| Concern | Implementation | Consequence |
|---|---|---|
| Crank angle | integrated per **audio sample** from smoothed rpm | pulses keep arriving at the right angle when the picture stalls |
| Drivetrain | absolute 2 ms step grid, remainder carried as integer nanoseconds | identical trajectory at any block size or host tick |
| Synthesis | runs in a render **worker**, writes a shared ring | the device thread only copies; a slow frame cannot starve it |
| Device pull | AudioWorklet, with a message-fed and a ScriptProcessor fallback | works without `SharedArrayBuffer` |
| Frame coupling | none — the picture loop reads telemetry and draws | the "Frame budget" slider adds real stalls to prove it |

## The nine cars

| # | Car | Engine | Induction | What makes its note its own |
|---|---|---|---|---|
| 1 | Ferrari LaFerrari | 6.3 V12, 65° | NA | 12 events/cycle, 60° bank alternation, short hot primaries, 9500 rpm bypass |
| 2 | Nissan GT-R Nismo GT3 | 3.8 V6, 60° | twin-turbo | 6 events at 120°, 89 mm single exit, dog-box whine, boost 1.9 bar |
| 3 | Ford Mustang GT 5.0 | 5.0 V8, cross-plane | NA | per-bank 90/180/270/90 → low-order burble, 420 ms shift cut |
| 4 | Koenigsegg Agera RS | 5.0 V8, flat-plane | twin-turbo | per-bank 180/180 → strong ×4 order, 1.5 bar, 45 ms cut |
| 5 | Porsche 918 Spyder | 4.6 V8, flat-plane | NA | top-exit short stacks (0.34 m), 9150 rpm, hybrid front share |
| 6 | Porsche Taycan Turbo | dual PMSM | electric | no exhaust at all; pole-pass, gear mesh, inverter carrier |
| 7 | Dodge Demon | 6.2 V8, supercharged | Roots | rotor-lobe mesh at 1.55× crank, 2.8 in blower, 8-speed, 2800 rpm stall |
| 8 | Dodge Viper | 8.4 V10, odd-fire | NA | genuinely uneven firing (90/54), 0.5-order partials, long system |
| 9 | Dodge Challenger 392 | 6.4 V8, cross-plane | NA | cross-plane burble at a lower redline, heavier flywheel |

## System layout

```text
VehicleCatalogue.js    nine sheets + derived firing geometry, pipe modes, gas sound speed
VehicleDrivetrain.js   engine torque, clutch/converter, gearbox, tyre, body, boost, limiter
ExhaustVoice.js        pulse-train series + pipe resonances + bypass + crackle + nonlinear radiation
InductionVoice.js      compressor whistle/spool/blow-off, Roots mesh, intake honk, EV drive
MechanicalVoice.js     gear whine, differential, tyre squeal, wind, brakes, injectors, pump, fan, lash
VehicleVoice.js        layer stack, cabin/exterior/pass-by acoustics, limiter, telemetry mapping
AudioKernel.js         shared ring, drain algorithm, AudioWorklet source
RenderWorker.js        render thread: fixed 128-sample blocks, telemetry at ~21 ms
AudioHost.js           device: worklet → messages → processor fallbacks
Bench.js / GaugeView   page, instruments, controls
```

## Controls

`W` throttle · `S` brake · `A`/`D` steer · `Q`/`E` shift · `R` reverse · `N` neutral · `Space` handbrake · `M` auto/manual

## Measured results

See [`Validation/Results/AcousticSummary.txt`](Validation/Results/AcousticSummary.txt) and
[`Validation/Results/ValidationReport.txt`](Validation/Results/ValidationReport.txt). Highlights from the last run on this host:

| Car | Firing events | Even | Firing Hz (72% redline) | Crest | Centroid |
|---|---|---|---|---|---|
| laFerrari | 12 | yes | 666 | 3.32 | 709 Hz |
| gt-rNismoGt3 | 6 | yes | 252 | 3.17 | 469 Hz |
| mustangGt | 8 | yes | 336 | 3.27 | 531 Hz |
| ageraRs | 8 | yes | 396 | 3.16 | 572 Hz |
| spyder918 | 8 | yes | 439 | 2.76 | 382 Hz |
| taycanTurbo | 0 | — | — | 7.66 | 318 Hz |
| demonSrt | 8 | yes | 312 | 3.24 | 369 Hz |
| viperAcr | 10 | **no** | 372 | 3.41 | 331 Hz |
| challengerScat | 8 | yes | 298 | 3.14 | 364 Hz |

36/36 shared checks pass; synthesis runs 8–16× faster than real time single-threaded on this host.

## Limitations

- The catalogue's **topology** (cycle, cylinders, crank layout, idle/limiter) is manufacturer data. Pipe lengths, spool constants, bypass
  schedules and cabin insulation are engineering estimates (`Estimate: true` in the sheet) and are labelled as such — this is not a measured
  impulse response of any real car.
- The tyre model is a saturating slip curve with load transfer, not a full magic-formula implementation; it exists to drive wheelspin and
  squeal honestly, not to be a lap-time simulator.
- Aerodynamics are drag only (no downforce), and there is no gearbox synchroniser detail beyond the shift interruption.
- Evidence images are drawn from rendered samples by `Tools/RenderEvidence.mjs`; the bench itself has not been captured in a GPU browser in
  this environment, and `npm run` here was run on Linux, not on the Windows target.
