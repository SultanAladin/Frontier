# RockFormation — working notes and deferred work

Running notes for the node-based 3D rock/erosion editor. **What works, what is deliberately deferred, and
why.** Kept beside the code rather than in `EngineDocs/` (which holds only `FolderStructure.md`,
`Backlog.md` and `AgenticInstuctions/`).

---

## 🟢 Confirmed working — do not regress these

Author feedback from the 2026-07-31 review, against the current build:

| Species | Verdict | What specifically works |
|---|---|---|
| `JointNetwork` | 🟢 **strong** | Genuinely drives erosion. **Changing Spacing yields different erosion *types*** — a discovery worth protecting: one dial reaches distinct weathering regimes, not just a finer or coarser version of one look. |
| `StratumBand` | 🟢 working | Alternating hard/soft beds express themselves through differential erosion rate as intended. |
| `TunnelMass` | 🟢 working | Bores the arch. GPU-probed open across 3928 grid lanes. |
| `HullMass` + `HullRelief` | 🟢 verified | Blender-generator port. 26% occupancy, faceted (support CoV 0.111), unclipped by the domain wall. |
| erosion chain | 🟢 "significantly better" | Thermal → hydraulic → aeolian → spheroidal → saltfreeze. |
| `StratumTint` | 🟢 "not bad" | Munsell-derived palettes indexed by resistance. |

📝 **Why Spacing changing erosion *type* matters.** Joint spacing sets how many low-resistance planes
intersect a given volume, so it controls whether weathering proceeds as isolated pitting, as slab release
along widely-spaced joints, or as granular disintegration where joints are dense enough to overlap. That
is three distinct mechanisms from one dial, which is exactly the leverage a node-based editor wants. Any
future change to `EvaluateJointNetwork` should re-check that the *range* of behaviours survives, not just
that a single default still looks right.

---

## 🚧 Deferred — agreed, not yet started

### ① Stratification is not linear — `SedimentBedding`

**Author:** *"they can be put through a node that warps/distorts the banding (of strata + colour) as it is
too uniform"*

🔴 **DO NOT solve this with `DomainWarp` — or with any warp node.** Author, explicitly: *"don't use
DomainWarp (this is terrible) — I was just giving you an idea of what you should do, as stratification
isn't very linear, is it?"*

The warp framing was **the wrong reading of the problem**. `EvaluateStratumBand` computes a band index from
a dot product against a dip direction — a **strictly linear function of position**. Warping its input
distorts that linear stack, but the stack is still one global periodic function underneath: every bed keeps
the same thickness everywhere, so every bed still ends and begins in lockstep. Bending parallel lines does
not make them stop being parallel.

⚠️ Compounding it, `DomainWarp` is isotropic fractal displacement, so it smears a bed's *thickness* as much
as its boundary — beds stop reading as beds. Even the "right" warp is still cosmetics over a wrong model.

**The real answer is `SedimentBedding` — deposition as a PROCESS, not a formula.** Stratification is a
history of episodes: each depositional event lays a bed of its own thickness, its own material, over
whatever surface existed at the time. That is why real strata are not linear:

| Reality | Why a linear band function cannot express it |
|---|---|
| beds have **individually different thicknesses** | one `Thickness` dial gives every bed the same one |
| a bed **thins out and disappears** laterally (pinch-out) | a periodic function cannot terminate a single period |
| beds **onlap** older topography, draping it | there is no "older surface" in a formula — no history |
| an **unconformity** truncates everything below at an angle | needs two stacks with different dips and an erosion surface between |
| **cross-bedding** sets laminae at an angle *inside* one bed | requires structure at two scales, per bed |
| **channel fills** cut down into beds below and fill with something else | a local body replacing part of the stack |

📝 **Model shape:** an accumulating stack rather than a periodic function. Walk N episodes; each carries
thickness, a material/resistance value, a dip, and a lateral thickness modulation (which is what produces
pinch-out and onlap for free — where the modulated thickness reaches zero, the bed genuinely ends).
Evaluated at a probe, the species asks *which episode's deposit occupies this point* and returns that
episode's resistance. Episode count is the "how many beds" control; the per-episode draw is what makes
them unequal.

🔴 **This subsumes both the banding and the colour complaint, and that is the point of doing it this way.**
Colour and hardness both derive from *which bed you are in* and *what that bed is made of* — the same
question. So `SedimentBedding` should yield an episode identity, not just a scalar, letting `StratumTint`
colour by material rather than by a resistance value that has already thrown the material away. It also
removes the trap noted earlier of having to link one warp to two separate intakes consistently.

⚠️ **Lane budget.** Four lanes per entry will not hold episode count + thickness range + dip + material
variation + pinch strength. Expect a split (`SedimentBedding` for the stack, something like
`BeddingDetail` for cross-bedding and channels), the way `HullMass`/`HullRelief` split. Decide before
implementing.

📝 **Keep `StratumBand`.** It is confirmed working and it is the right cheap tool for a uniform layered
sandstone. `SedimentBedding` is an additional species for when the author wants real stratigraphy, not a
replacement.

#### 🔍 Measured values — sedimentology literature, 2026-07-31

🔴 **CORRECTION — lateral pinch-out is SUB-PERCEPTUAL at 5 m, so the model sketch above is wrong about it.**
A sandstone bed halves in thickness over **~100 m** (Point Loma Fm, San Diego, 700 m continuous outcrop, 10
correlated sections; *The Depositional Record* 2021/2022 — partial citation). That is a **1:200 thinning
gradient**, so across a 5 m panel a tabular bed thins by **2.5%** — invisible. The "lateral thickness
modulation reaching zero" idea above therefore cannot produce visible pinch-out for sheet beds.

What actually makes beds visibly begin and end at this scale is **erosional truncation** — and that is the
cheapest thing on this list, one extra cut pass before each episode. It also generates apparent
thickness variation for free: a constant-thickness bed cut by an irregular surface *reads* as variable. Real
pinch-out inside the panel comes only from **lenses** (W:T ≈ 12) and from compensational stacking below.

**Bed thickness is LOG-NORMAL, not power-law.** Sylvester 2007 (*Sedimentology* 54:847–870) re-examined **18
published plots** read as power laws and found the power law "the exception rather than the rule" — a
high-variance log-normal *plots as a straight line* on a log-log exceedance plot, so the apparent power laws
were fitting artefacts. Straub et al. 2012 (*JGR Earth Surf.* 117) show exponential is the better *process*
answer, but its mode at zero makes it awkward to sample; log-normal has a finite mode and an art-directable
median. Sample `t = exp(μ + σ·Z)`, `μ = ln(median)`.

| Style | median [m] | σ (of ln t) | p5–p95 [m] |
|---|---|---|---|
| Thin flaggy | 0.06 | 0.6 | 0.022–0.16 |
| **Mixed (default)** | **0.15** | **0.8** | **0.040–0.56** |
| Thick / massive | 0.5 | 0.7 | 0.16–1.6 |

🚩 σ and medians are **SELF-TUNED** (Sylvester's fitted mixture parameters are paywalled), calibrated against
Ingram 1954's bedding scale — which is itself **geometric, ~3× per class**, independent support for a
multiplicative distribution. σ = 0.8 gives p95/p5 ≈ 14:1, inside the sourced 10–30:1 outcrop range. **σ below
~0.5 still reads as uniform — that is the current problem restated statistically.**

⚠️ **Voxel floor.** At 5 m / 256³ = **19.5 mm per voxel**; at 128³, **39 mm**. Laminae (<1 cm, Ingram 1954)
and cross-bed foresets (~15–30 mm) are **at or below one voxel** — they must be shading, never geometry.
Resolvable bed range is ~4 cm to 2 m.

⚠️ **Do not couple extent to thickness deterministically.** Width and thickness correlate only weakly across
the fluvial data pool — **Pearson R = 0.31** (Colombera et al., *AAPG Bulletin*). The scatter *is* the
realism. Aspect ratios: deep-water channels **4:1–32:1**; Blackhawk sandbodies **50% below 13:1** (Gibling
2006, *JSR* 76:731–770). **W:T ≈ 12 is the money geometry** — a 0.4 m bed ~5 m wide pinches out inside frame.

📝 **Bedding surfaces are far from planar.** HCS gives the cleanest numbers: wavelength **1–2 m**, amplitude
**10–30 cm** on **20–80 cm** beds → **relief is 25–50% of bed thickness**, aspect ~10:1 (Yang, Dalrymple &
Chun 2006, *JSR* 76:2–8). So perturb each depositional surface by **0.2–0.4 × its own thickness** at 1–2 m
wavelength — 25–50 voxels at 256³, comfortably resolvable. **This alone breaks the lockstep look.**

**Erodibility contrast — the ledge former.** 🔴 **Cap at ~10:1**; higher is sourced but reads cartoonish.

| Pair | Ratio |
|---|---|
| Shale : cemented sandstone | 4:1–10:1 |
| Shale : silicified bed | 10:1–30:1 |
| Friable : cemented sandstone | 3:1–6:1 |
| Marl : limestone | 3:1–8:1 |

🔴 **Within-bed variation must exist too.** 90th/10th percentile strength ratios **exceed 100** for shale,
limestone and conglomerate (*Sci. Adv.* adr2610) — **within-lithology variability can exceed between-lithology
differences**. One uniform scalar per bed yields machined ledges. Cementation, not grain size or bulk
mineralogy, is the dominant strength control (silicification raised UCS **an order of magnitude**).

**Colour is iron oxidation state, not grain size.** Red = **Fe³⁺ hematite** grain coatings, tinting strength
30–60 m²/kg, so a tiny mass fraction saturates — hence red beds' uniform intensity. Green/grey = **Fe²⁺**,
reduced, poorly drained. Red beds are formally **2.5YR–5R** (Blodgett et al. 1993, SSSA Spec. Pub. 31).
Hematite/goethite ratio sets hue: high → red, mixed → orange, goethite-dominant → **yellow-buff**.

🔴 **Do NOT wire "coarse = red, fine = grey".** In the Old Red Sandstone the correlation runs the *opposite*
way — **mudstones and palaeosols are red while sandstones are grey**, and every white/grey rock with h/g < 1
was a sandstone (Bábek et al. 2025, *Sedimentology*, doi:10.1111/sed.13244). Reddening is *pedogenic*, in
fine floodplain material; the sandstones were water-saturated channel bodies. Colour tracks **drainage and
redox**, which ties it to cyclicity below: dry/well-drained → red, wet/poorly-drained → grey-green-black.

🚩 Hex values and non-red Munsell notations are **SELF-TUNED**; only the 2.5YR–5R red-bed range is sourced.
Mn and glauconite colour data could not be sourced at all.

**Cyclicity: a first-order Markov chain over 4–6 material states, near-symmetric.** Published transition
matrices across geological time show **marginal homogeneity and symmetry — a reversible process**, so a
symmetric matrix is the supported default and strong always-fining-upward directionality is the *special*
case. Cyclicity is a continuum from damped to periodic, measured by the **eigenvalues** of the matrix
(Schwarzacher 1969, *Math. Geology* 1:17–39). Keep the diagonal dominant so beds cluster into *packages* —
that is what gives an outcrop character rather than noise. Add small (~0.05–0.10) asymmetry for
fining-upward, no more. 🚩 Specific probabilities are self-tuned.

⚠️ Older published matrices may overstate cyclicity: for **embedded** chains an independent-trials null is
inappropriate, and reanalysis under quasi-independence "gave quite different results" (Powers & Easterling
1982, *JSR* 52:913–923). Also sobering — Karharbari fining-upward cycles classed **C-type, essentially
random**, and the Kolhan Group came out **non-Markovian**. Real cyclicity is weak.

📝 **Compensational stacking — the highest value per line of code, and it is sourced.** Event beds thin over
topography left by the previous bed, so deposition is **thickness-anticorrelated with existing relief**:

```
t(x,y) = t_bed * (1 + k * (h_mean - h_surface(x,y)) / t_bed)     // k ~ 0.5-1.0, SELF-TUNED
t(x,y) = clamp(t(x,y), 0, 2 * t_bed)                             // zero => real pinch-out
```

Thicker in hollows, thinner over highs; the surface **self-flattens** over several episodes so relief does
not diverge — structurally reproducing Straub et al.'s "extreme variability largely cancels itself out";
where a bed thins to zero over a high you get **genuine pinch-out inside the 5 m panel**, which §2's gradient
argument says gradual thinning cannot deliver; and it **couples episodes with no explicit cyclicity at all**.
Set `k = 0` on a subset for draping/pelagic episodes.

📝 **Structures ranked by payoff per cost** — ① erosional truncation (near-free, and the only thing that ends
beds at 5 m) · ② non-planar surfaces (one noise call) · ③ differential erodibility ledges · ④ graded beds
(⚠️ Bouma **never published division thicknesses** — any you use are invented; complete sequences are *rare*,
partial AE/BCE/CE the norm, completeness rising with thickness) · ⑤ channel fills, scour **0.3–1 m deep × 3–10
m wide** · ⑥ lenticular-vs-tabular class · ⑦ cross-bedding **as shader only** (set thickness 0.25–0.35 m is
resolvable, its foresets are not; `h_dune = 2.9 × h_set`, Leclair & Bridge 2001) · ⑧ onlap only as part of
channel fill · ⑨ nodular horizons (**40–120 cm, spaced 2–3 m, on specific horizons**, McBride et al. 1999).

🔴 **Colour must sometimes cross-cut bedding.** If colour is a pure function of bed index you have asserted
colour is 100% depositional; it is not — bleached pale zones inside red beds are post-depositional iron
stripping, limonitic staining follows fractures, and reddening continues after burial. One diagenetic overlay
that ignores bed boundaries is cheap, high-payoff, and almost never done in procedural work.

⚠️ **Remaining fake-tells:** every contact the same kind (real ones are independently sharp/gradational **and**
planar/wavy — four combinations, Mazzullo & Graham 1988; suggest ~60% sharp, 40% gradational over 5–15% of
bed thickness); a purely additive conformal stack (reads as a bar chart); every cycle textbook-complete;
every bed sharing one global dip (the current model's flaw — let each episode's surface perturb
independently); and no massive beds — structureless beds are authentic and common.

### ② Water-level erosion — `WaterlineWeather`

**Author:** *"I also want water erosion, but this water erosion must work at a specific height, where water
should [rise] from bottom to top, to whatever height — or we can just use full model."*

🔴 **This is NOT what the existing `Hydraulic` process does, and the distinction matters.** `Hydraulic` is
**rainfall-driven**: a catchment proxy counts rock standing above a cell, water is assumed to run downhill
over the whole body, and abrasion applies anywhere exposed. There is **no water level in it at all**. What
is being asked for is *immersion* erosion — a standing water surface at a controllable height that attacks
the rock **in a band around that height** and leaves rock above and below it alone.

They are different mechanisms with different signatures, and both are wanted:

| | `Hydraulic` (exists) | `WaterlineWeather` (wanted) |
|---|---|---|
| driver | rain falling on the whole body | a standing water surface at height H |
| where it cuts | anywhere exposed, most in channels | a **band** at the waterline |
| signature | channels, gullies, runnels | **notch, bench, undercut, sea stack** |
| controls | flow, hardness | **height H**, band thickness, wave energy |

📝 **Why a band and not a half-space.** Erosion is fastest *at* the water surface, not everywhere beneath
it. Wave and swash energy concentrate in the splash zone; wetting-and-drying cycling — repeatedly wet, then
dry — is chemically and mechanically far more aggressive than permanent submersion, which is why a
**notch** cuts in at the waterline instead of the whole submerged mass dissolving evenly. Fully submerged
rock, with no wave action and no drying, erodes slowly.

🔴 **Rising water is what makes this a landform generator rather than a groove cutter.** A *static* level
carves one notch. A level that **rises over the run** sweeps the erosive band upward through the body,
which is what produces stacked benches, wave-cut platforms and stepped terraces — each level recording a
stillstand. This is exactly the kind of history-dependent structure the M2 iterative approach exists to
capture and an analytic field cannot express. The height must therefore be a **function of round index**,
not just a constant dial.

📝 **Cheap and exact to implement, unlike most of this list.** The waterline is a world-space Y plane, and
the compute shader already knows each cell's world position (`CellToPosition`). So the process is a gather
that weights removal by distance from the plane — no flow simulation, no extra buffer, no scatter. Sketch:

```
Height    = the current level [m]            <- rises with round index
Offset    = WorldY - Height                  <- signed distance from the waterline
Band      = exp(-(Offset/Thickness)^2)       <- fastest AT the line
Submerged = a small constant below, ~0 above <- still water does little
Removed   = StepDelta * (Band * Wave + Submerged) * Exposure * Susceptibility
```

⚠️ **`Exposure` and `EvaluateSusceptibility` must stay in the expression**, exactly as the other five
processes use them. Dropping them would make the waterline cut a *geometrically flat* groove that ignores
both which faces are actually exposed and how hard the rock is — a machined slot around the body, which is
the authored-not-earned failure M1 died of. Kept in, the notch cuts deeper into soft beds and stalls at
hard ones, which is precisely the differential-hardness bench that makes it read as real.

📝 The "or we can just use full model" alternative — full shallow-water flow — is a much larger build
(velocity field, sediment transport, deposition) and is **not** needed for the notch/bench/terrace
signature. Recommend the waterline band first, since it delivers the visible landform at a fraction of the
cost; full flow stays open as a later option if channel *routing* over the surface is ever wanted.

#### 🔍 Measured values — coastal geomorphology literature, 2026-07-31

Researched properly rather than self-tuned. **Two items above are contradicted by the literature and are
corrected here; the sketch's `exp(-(Offset/Thickness)^2)` and its rising-height assumption are both wrong.**

🔴 **CORRECTION 1 — the band is asymmetric, skewed UPWARD, not a symmetric Gaussian.** Notch height always
*exceeds* the tidal range (**0.3–3.2× it**, Antonioli et al. 2015, *QSR* 119:66–84, 73 Mediterranean sites),
because splash, spray and run-up inflate the band **from above**. Field erosion maxima cluster at or just
below **mean higher high water**, not at mid-level; the submerged side has a fast exponential cutoff while
the aerial side has a long spray/salt tail. Use peak offset **+0.15 to +0.30 R** above the level, upward σ
≈ **1.2×** downward σ. A published alternative shape: Schneiderwind et al. 2017 (*JGR Earth Surf.* 122) use
the **roots of a tidal-range quadratic** as the erosion limits — a clipped downward parabola, which is
*cheaper than a Gaussian and has compact support*, so it is the better fit for a gather kernel.

🔴 **CORRECTION 2 — a linear rise produces NO benches.** Erosion potential at a datum is proportional to the
**total time the water level occupies that datum** (Malatesta et al. 2022, *Geology* 50(1):101–105). Constant
rise ⇒ uniform dwell at every height ⇒ a smooth cone, no steps. Benches require **dwell-time variation**:
staircase the rise (hold, jump one band, hold) or modulate its speed. Preservation is also *net* — time spent
just **below** a bench destroys it by undercutting. This overturns "height is a function of round index" as
stated: it must be a **non-uniform** function of round index.

| Quantity | Value | Source |
|---|---|---|
| Band full thickness | **0.45–0.70 m** (≈ 2.5 × tidal range) | Antonioli 2015; Trenhaile 2014 |
| — at 256³ over 5 m (19.5 mm/vox) | 23–36 voxels | derived |
| — at 128³ over 5 m (39 mm/vox) | 12–18 voxels ⚠️ tight | derived |
| Band : submerged contrast | **20–50×** (lit. 10–100×) | Otway / Kaikōura MEM |
| Submerged falloff | exponential in depth, **not zero** | Sunamura 1992; Trenhaile 2000 |
| Depth : height ratio | **1:1 fresh → 3.75:1 mature** | Antonioli 2015; Schneiderwind 2017 |
| Notch depth cap → collapse | **2–4 × band height** | Budetta et al. 2010; Trenhaile 2014 |
| Collapse cycles per run | **2–4** | Trenhaile 2014 |

📝 **Notch height is set by water-level range; notch depth is a clock.** Height fixes almost immediately from
the band thickness, while depth accumulates with dwell time (Trenhaile 2014, *Geomorphology* 224:139–151). So
a fresh notch is wide-and-shallow and a mature one deep-and-narrow — the ratio is a **read-out of elapsed
time**, not a constant to dial.

**Downwearing rates [mm/yr]** — the differential-resistance ladder. Global platform average **1.486**
(Stephenson & Finlayson 2009), published range **0.03–25.4** (Sunamura 1992):

| Rock | Rate | Source |
|---|---|---|
| Chalk | **3.0** | chalk platform compilation (18 sites, 0.791–7.202) |
| Limestone | **0.9** | Stephenson & Kirk 1998, *ESPL* 23:1071 (0.875 pre-uplift) |
| Sandstone | **0.3** | Otway 40 yr (0.25); Japan (0.35) |
| Granite | **0.05** | 🚩 **SELF-TUNED** — extrapolated from <0.001 m/yr recession |

🔴 **The ordering is NOT monotonic in hardness — sandstone measures SLOWER than limestone** (0.25–0.35 vs
0.875–1.19) because limestone dissolves and is bioeroded while sandstone merely abrades. That inversion is a
gift: it justifies **solubility as a second resistance axis independent of hardness**. Per GlobR2C2
(Prémaillon et al. 2018, *ESurf* 6:651) even granite can retreat at 1 m/yr where jointed and weathered —
**jointing and weathering grade dominate lithological category**. A single hardness scalar therefore erodes
everything into the *same shape* at different speeds, which is the absence of the differential resistance
this process exists to show.

⚠️ **Add the negative feedback or benches grow forever.** Every standard rock-coast model (Sunamura 1992;
Trenhaile 2000; Walkden & Hall 2005) predicts steady state: a wider platform dissipates more wave energy and
slows retreat. SE England chalk fell from 60–110 cm/yr at 7 ka to 1–5 cm/yr now — a **~20× slowdown** purely
from widening. Attenuate the band's removal by the horizontal distance already cut.

📝 Further failure modes worth heeding: never let the submerged zone be *perfectly* inert (a hard zero leaves
a visible horizontal discontinuity — use the decay); cap notch depth and trigger an **overhang collapse**
instead of cutting a parallel-sided slot indefinitely; jitter the band centre slightly per step, since real
backwalls are **scalloped** (frequent small level changes) or **bimodal** (infrequent large ones), and a
perfectly clean notch roof is the tell. Arches specifically need **two-sided attack** plus joint control — an
isotropic band on a homogeneous field yields a symmetric mushroom, never an arch or stack, which ties this
process to ⑤ below.

📝 Arch/stack sequence: headland → joint exploited → crack → cave → arch → collapse → stack → stump, sited by
**joint orientation and bedding attitude** (Durdle Door: near-vertical beds; Old Harry: near-horizontal).
Wave refraction concentrates energy on headlands, which is why the whole suite forms there.

### ③ `TunnelMass` needs more control + a visual debug

**Author:** *"tunnel mass too, but we need more control — perhaps add a visual debug so we can see the
shape in editor"*

The control problem and the visibility problem are the same problem: the tunnel is **invisible until it
has already cut something**, so dialling it is guesswork.

🔴 **This is why the units bug went unnoticed for a whole session.** The hull was seeded ~2.4× too small
and the 0.72 m tunnel swallowed it entirely — no arch could ever have appeared — and the shader compiled
perfectly. A carver that renders as a wireframe in the viewport would have made that obvious in one glance
instead of needing a GPU probe.

Wanted:
- **Carver visualisation** — draw `Distance` species that feed a boolean's `carver` intake as a
  translucent hull or wireframe, so the author sees the cutting volume *in place* before it cuts.
- **More shape control.** Currently radius / span / elevation / bearing — a straight capsule of constant
  radius. Missing: taper along the sweep, curvature (an arch bore is rarely straight), and elliptical
  cross-section.
- ⚠️ Extra shape parameters need lanes, and an entry has exactly **four**. A tapered curved elliptical
  tunnel exceeds that, so this likely splits into two species the way `HullMass`/`HullRelief` did — or
  takes a second slot. Decide before implementing, not during.

### ④ Rock colouring research — explicitly deferred by the author

**Author:** *"you may need to conduct research on rock colouring… but rock colour leave for later"*

Not started, by instruction. When it resumes, the open question is how non-sedimentary rocks get their
colour — the author noted sedimentation is the only mechanism they know. Igneous/metamorphic colour comes
from mineral assemblage rather than cement and bedding, so it will not reuse `StratumTint`'s
resistance-indexed palette directly.

### ⑤ Surface cracks

Research complete, nothing implemented. **The key constraint, which changes the whole approach:**

🔴 **Crack aperture cannot be voxel geometry.** At 256³ a real crack is **0.05–0.5 cells wide** — one to
two orders below Nyquist. A sub-voxel carve collides the inside and outside narrow bands and produces
**sign noise, not a crack**.

So cracks enter as **three decoupled layers sharing one pattern generator**:

| Layer | Resolution | What it represents |
|---|---|---|
| resistance field | sim | *on a joint* — aperture irrelevant, only weakness matters |
| emergent geometry | sim | widening that appears once erosion opens a joint past ~2 cells |
| shading detail | render | the visible line, below sim resolution |

Numbers worth keeping:
- joint spacing `s ≈ 0.77t` (t/s ≈ 1.3, Monterey Fm), **log-normal** distributed; cross-joint spacing ×1.81
- the master-vs-cross **"ladder pattern" termination rule** buys most of the realism
- ⚠️ **mudcracks are 90° T-junctions, four-sided.** Voronoi's 120°/hexagonal is **wrong** here — it is
  right for *columnar* jointing. Mudcrack rule = recursive midpoint subdivision, length halving each
  generation, angular jitter 5° (thick) → 20° (thin).
- columnar jointing needs **partial** Lloyd relaxation (2–4 iterations, *not* convergence — full
  relaxation over-produces hexagons)
- **exfoliation is the cheapest and best grid fit**: `frac(SDF/spacing(depth))` bands + a curvature gate
  (curvature = divergence of the normalised density gradient, a pure gather). Slabs 1–10 m, spacing
  growing to 50–100 m with depth.
- erodibility changes **3–4×** with fracture state, while intact strength differs **<2×** — fracture is
  the dominant term, which is why a resistance field is the right vehicle
- 💡 **spheroidal weathering falls out of a resistance field for free** (corners see more low-resistance
  neighbours) but would *not* from subtracted geometry

### ⑥ Erosion rate control

The timeline module exposes `Rate`, but no UI speed control is wired.

📝 **Root cause of "erodes too fast" is not the per-step aggression.** `RoundsPerFrame = 2` against
`RoundCeiling = 260` means the entire run completes in **~2 s at 60 fps** — 260 rounds elapse faster than
a person can watch, with no mechanism to hold it anywhere. It is a transport problem, which is why the
timeline was built before touching any erosion constant.

### ⑦ Timeline wiring

`Simulation/ErosionTimeline.js` is complete and unit-verified (27/27 assertions) but **nothing calls it**.
Needs: wiring into `DeviceHost.js` / `RockFormationEntry.js`, and a scrub bar + play/pause + speed control
in the viewport footer.

---

## 🔍 Research status — INCOMPLETE, but NOT for the reason recorded earlier

Asked directly on 2026-07-31 whether research was complete. **It is not.** The topics below are genuinely
unresearched. The *explanation* previously given here was wrong, and is retracted.

✔️ **Waterline erosion and sedimentation are now researched and cited** — findings recorded inline in ① and
② above, where they correct four separate design assumptions. Outstanding: raymarch shading/perf, and rock
colouring (deferred by the author, though ①'s colour findings cover much of it).

🔴 **ROOT CAUSE FOUND, and two earlier diagnoses in this document were wrong.** Re-dispatching returned a
real error instead of silence:

```
400 This session was started without a valid model-id
```

It was a **`model:` override on the Agent call**. A probe agent without the override returned instantly. So
every prior research agent **died at dispatch and never ran** — the files were empty because there was
nothing to write.

📝 **Two retracted explanations, and what they have in common.** First "the output-capture path is broken";
then "the results were produced but went uncaptured". Both were wrong, and both were reached the same way —
**promoting a single reading into a root cause without checking the neighbouring evidence.** The tell was
present throughout: in the same directory, background *shell* tasks wrote 32–79 KB while only *agent* tasks
read 0 bytes. A number that reads "0" identically for *never ran*, *ran but lost*, and *not measurable this
way* cannot distinguish them; the fix is to make the tool report its actual error, which one re-dispatch did.
Also note the `.output` path for a `local_agent` is a symlink to the subagent transcript, not a results file
— so it is the wrong place to look either way.

📝 **Therefore: write findings into this document as they arrive.** That is what makes the Blender, crack,
fracture, waterline and sedimentation rows trustworthy — they were captured in-conversation and written down
immediately, not left to be recovered from a task directory afterwards.

| Topic | State | Source |
|---|---|---|
| Blender rock generator | ✔️ solid | addon source read directly (`rockgen.py`, `randomize_texture.py`, `utils.py`, `factory.xml`); corrected 3 wrong assumptions |
| crack morphology + spacing | ✔️ solid, cited | returned in-conversation |
| fracture mechanics (separation tensor) | ✔️ verified | O'Brien & Hodgins 1999, eqs. 28–32 |
| **waterline / marine erosion** | ✔️ solid, cited | Antonioli 2015; Trenhaile 2014/2015/2016; Malatesta 2022; Sunamura 1992 — in ② |
| **sedimentation / stratigraphy** | ✔️ solid, cited | Sylvester 2007; Straub 2012; Gibling 2006; Ingram 1954; Bábek 2025 — in ① |
| **rock colouring** | 🟢 largely covered | iron redox + Munsell in ①; author deferred a dedicated pass |
| **raymarch shading + perf** | ✔️ solid, cited | Engel 2006; Hadwiger 2017; Quilez; Csébfalvi 2019/2023 — in ⑧ below |

📝 **All four research topics are now closed.** The raymarch round ran last, after the `model:` override was
removed; it read the shipped source rather than only answering the brief, and found three live defects. See ⑧.

⚠️ **What this means for numbers in the code.** The Blender, crack and fracture findings are real and
traceable. Everything else quantitative — erosion rates, the waterline band thickness, wave energy, bed
thickness distributions — is currently **self-tuned**, and must be labelled as such following the existing
`GeologyReference.js` split between cited constants and `SelfTunedReference`. Do not let a self-tuned
number get cited later as if it were measured.

📝 Take an agent's findings from its returned result and record them **here, in this document**, while they
are in hand. That is what makes the Blender/crack/fracture rows above trustworthy: they were written down
when they arrived, not left to be recovered from a task directory afterwards.

---

### ⑧ Raymarch shading + performance — researched 2026-07-31

Findings from the marine/sediment rounds corrected *design assumptions*. This round corrected **shipped
code**: three defects, each verified against the source before being written here.

#### 🔴 Three live defects — confirmed by reading the files, not taken on report

**🔴 ① `StepCeiling` truncation punches sky-coloured holes.** `TraceDensityGrid` returns
`Contact = false` when the step loop runs out (`GridMarch.js:204`) — the *same* outcome as a ray that
missed the grid entirely (`:174`). A truncated ray therefore shades as **background**. It does not degrade
gracefully; it drills a hole.

| Tier | `StepScale` | `StepCeiling` | Cells covered | Grid | Verdict |
|---|---|---|---|---|---|
| Main (`RockFormationEntry.js:73-74`) | 0.55 | 320 | 176 | 128³ ✔️ / 256³ 🔴 | fails at bake tier — needs ~465 |
| Preview (`DeviceHost.js:701-702`) | 0.9 | **96** | **86** | 128³ 🔴 | fails *now*, at ⅔ of the current grid |

🔴 **The preview tier is worse than the research reported.** The agent flagged the 256³ case; the shipped
128³ preview already truncates at 86 of 128 cells. Any card thumbnail deep enough along the view axis is
losing its back third to holes today. Fix: derive the ceiling as `Edge / StepScale` with headroom rather
than storing a constant that silently decouples from both grid size and stride.

**🔴 ② Occlusion is applied to the direct sun term.** `GridMarch.js:283` multiplies the whole `Radiance`
accumulator by `EvaluateGridOcclusion` — but line 276 has already folded the direct sun into that
accumulator. Quilez calls this out in capitals: applying occlusion to the entire lighting equation is
**"NOT A GOOD IDEA"**; AO modulates ambient terms only ([multiresaocc](https://iquilezles.org/articles/multiresaocc/)).
The sun is already shadowed by `TraceGridShadow` at `:274`, so the AO multiply is double-darkening it.
Fix: move the multiply to cover only the sky-dome and ground-bounce terms (`:279-280`).

**⚠️ ③ Preview marches at 0.9 cell — above Nyquist.** The reconstruction filter's Nyquist limit is
**0.5 cell** (Engel et al., *Real-Time Volume Graphics*, SIGGRAPH Course 28). 0.9 drops exactly the thin
bedding the preview exists to judge.

📝 **The header comment's *reasoning* is wrong even though 0.55 is nearly right.** `GridMarch.js:17` argues
a step under one cell guarantees a one-cell ligament is "sampled at least once". A fixed stride has **no
phase guarantee** — a ligament whose along-ray span above the isolevel is under the stride can fall
entirely between two samples. The correct justification for ≤0.5 is Nyquist on the filter, not geometric
coverage. Related: `GridMarch.js:14` credits an **empty-space skip that was never implemented** — the march
has no acceleration beyond the slab test.

#### 🔍 Measured findings

**Empty-space skipping under a *dynamic* field.** Most published structures assume a static volume; ours
changes every erosion step. The discriminator is whether *topology* is fixed:

| Structure | Per-step update | Verdict |
|---|---|---|
| **Fixed-block min–max mip** (8³ → 32³) | 🟢 recompute; it *is* a reduction | 🟢 **build this** — the one structure whose rebuild is cheaper than the erosion pass that dirtied it. Min–max also makes an isolevel change free |
| Sparse octree / SVO | 🔴 full rebuild — node *existence* changes | reject |
| VDB narrow-band | ⚠️ partial, but CPU-shaped | no WebGPU story (Museth, TOG 2013) |
| GPU jump-flood EDT | 🔴 8 passes × 27-tap gather over 16.7 M cells | 🔴 **reject** |

🔴 **Why the distance field loses, stated as an asymmetry worth keeping:** an EDT buys *variable step
length*, but that only helps near the surface — which is precisely where Nyquist **forbids** long steps.
You would pay 8 full passes per simulation step to accelerate the one region you may not accelerate. A
coarse mip buys *skip / don't-skip*, which is where nearly all the win is. Published ESS gain: **2–5×**
(arXiv 2407.21552), which bounds the rebuild budget.
⚠️ Dilate the occupied set by one block before use, or a surface crossing the isolevel just inside an
"empty" block gets skipped.

**Storage buffer → `r16float` 3D texture is the biggest single win.** `SampleDensity` is a *manual* 8-tap
trilinear (`GridMarch.js:71`), so one shaded pixel currently costs ~150+ loads: normal 6×8, occlusion 5×8,
curvature 7×8. Hardware filtering collapses each to 1 tap → ~19.
🟢 **`r16float` is filterable in core WebGPU with no feature flag** and halves 64 MiB → 32 MiB; `r32float`
needs the optional `float32-filterable` (Chrome 119+), which not all hardware has. Occupancy in [0,1] does
not need 32 bits. Write via `STORAGE_BINDING`, rebind as `TEXTURE_BINDING` to sample.
🚩 **SELF-TUNED** — no published 3D texture-vs-storage-buffer trilinear benchmark exists. The nearest datum
is 2D (TextureSplat, arXiv 2506.13348: ~9% gap), not applicable to an 8→1 tap collapse.

**Normals — Sobel-8 for two extra taps.** ✔️ The existing sub-cell-epsilon reasoning (`:113`) is correct:
below one cell the trilinear interpolant is exactly linear, so one cell *is* the right offset.
🔴 But 6-tap central differencing bands worst on **near-planar, near-axis-aligned** surfaces — which is
precisely what bedding produces. Sobel-8 samples the 8 corners at (±0.5,±0.5,±0.5); its isotropy is what
removes the banding, for +2 taps (free once the texture lands). Triquadratic (Csébfalvi, TOG 2019) is
better still but *requires* hardware trilinear.
⚠️ The zero-gradient guard at `:129` returns `(0,1,0)` — an up-normal reads as a lit facet rather than as
an error, so a degenerate region looks plausible instead of looking wrong.

**Shading, ranked by payoff per millisecond:** ① AO in crevices ✔️ have it · ② cavity darkening ✔️ have it
(📝 `EvaluateGridCurvature` is a *Laplacian*, not curvature — fine as a cue, mislabelled) · ③ Sobel-8
normals · ④ hemisphere-spread AO directions · ⑤ sun shadow ✔️ have it · ⑥ soft shadows 🔴 low value.
🔴 **The SDF AO and soft-shadow one-liners do not port.** Both `occ += (h-d)*sca` and `res = min(res, k*h/t)`
depend on `h` being a Euclidean distance; with density they compare metres to occupancy. ✔️ The existing
`EvaluateGridOcclusion` already uses the correct density-domain form. ⚠️ It does inherit the known
self-occlusion artefact — marching straight along the normal from a broad bedding plane samples only that
plane's own falloff, so a flat ledge and a shallow alcove score alike. Fix is spreading sample directions
toward a hemisphere, which is free.
⚠️ `TraceGridShadow` steps at **1.4 cells** — deliberately coarse, but it will leak light through the
1-cell bedding ledges that matter most.

**🔴 Resolution honesty — the real limit is ~3 voxels, not 1.** At 5 m / 256 = 19.5 mm/voxel, but Nyquist
sets the smallest *periodic* feature at 2 voxels, and trilinear attenuation means a 1-voxel slab may never
reach `SolidLevel` — so it is **absent, not blurry**. Budget **3 voxels (~59 mm)** for a ledge that should
read as a ledge.

| Feature | Real scale | @256³ | Verdict |
|---|---|---|---|
| Bedding ledge, thin | 10–30 mm | <1.5 vox | 🔴 unrepresentable — shading only |
| Bedding ledge, readable | 60 mm+ | 3+ vox | 🟢 geometry |
| Joint aperture | 1–10 mm | ≪1 vox | 🔴 never geometry — see note below |
| Waterline notch (main form) | 0.3–1 m | 15–50 vox | 🟢 well resolved |
| Spheroidal rind | 10–50 mm | 0.5–2.5 vox | ⚠️ marginal |
| Tafoni / solution pits | 5–50 mm | 0.25–2.5 vox | 🔴 mostly shading |
| Grain roughness | 0.1–2 mm | ≪1 vox | 🔴 always shading |

📝 **This does *not* invalidate `JointNetwork`** — it works precisely because joints modulate **resistance**,
driving differential erosion into features many voxels wide. The joint is never geometry. Protect that
design; it is the pattern the sub-voxel features should follow.
💡 **Consequently:** everything under ~60 mm must arrive as shading, and the procedural fields
(`resistance`, `JointNetwork`, `StratumBand`) are analytic — evaluable at *any* scale. `SampleResistance`
currently reads the voxel grid (`GridMarch.js:95`), so it is resolution-limited **for no reason**; the
function that seeded it is not. Sampling it continuously at the hit point recovers the detail geometry
cannot hold.

#### 🚩 Ruled out, with published numbers

- **JFA/EDT distance field** — 8 passes per step to accelerate where acceleration is forbidden.
- **Ray compaction** — 5× fewer warps for only **16%** gain (arXiv 2506.11273); the divergence it adds
  eats the benefit. ⚠️ Note the early-exit at `:198` already creates divergence and that is fine.
- 🚩 **ms/frame is SELF-TUNED — no figure given.** No published benchmark exists for this workload on this
  hardware class. The step-count visualisation (`ResolveMode > 2.5`, `:343`) is already the instrument to
  measure it with. Dominant cost is **texture/memory throughput**, not step count.

#### Recommended order, cheapest-first

| # | Action | Effort |
|---|---|---|
| ① | Derive `StepCeiling` from `Edge / StepScale` — fixes both tiers | trivial 🔴 |
| ② | Move the AO multiply off the direct sun term | trivial 🔴 |
| ③ | Preview `StepScale` 0.9 → 0.5 | trivial 🔴 |
| ④ | Density → `r16float` 3D texture, hardware trilinear | medium |
| ⑤ | Fixed-block min–max mip + conservative dilation | medium |
| ⑥ | Central-difference → Sobel-8 normals | small |
| ⑦ | Hemisphere-spread AO offsets | small |
| ⑧ | Analytic (not voxelised) resistance at the hit point | medium |

⚠️ ①–③ are one-line defect fixes in shipped code, not features. They are separable from the deferred
`SedimentBedding` / `WaterlineWeather` work and from the "document only" hold.

---

## 🚩 Divergences from the approved plan

- **`Resolve/SphereTrace.js` was NOT deleted** as the plan directed. The per-card preview thumbnails depend
  on its analytic march; only the main viewport marches the voxel grid. Deleting it would have broken every
  card preview.
- **`HullMass` lost its `Jitter` dial** to fund a metre-valued `Extent`. Jitter perturbed plane
  *directions*, `Spread` perturbs their *distances*, and at usable amplitudes the two are visually near
  indistinguishable — so the duplicate was spent rather than the mechanism. Jitter is fixed at its former
  default (0.55) inside the shader.

---

## 🐞 Hazards worth not rediscovering

- 🔴 **Carry units explicitly.** The hull's first version used a dimensionless extent (`1.0`) while every
  neighbouring species dialled metres. Result: a body 2.4× too small at 1.58% grid occupancy, with the
  tunnel engulfing it. **All 8 pipelines compiled and validated.** Only a dispatch-and-measure probe found
  it — and only after adding a *span-against-the-domain* assertion, because occupancy, connectivity,
  faceting and proportion checks all pass happily on a pebble.
- 🔴 **A downstream entry cannot rewrite its upstream's probe.** The transcriber emits each upstream entry
  to a `let` binding *before* the consumer's expression runs. Anisotropy therefore has to live where the
  probe *enters* (`HullMass`), fed through the `warp` intake. Scaling `Probe` in `HullRelief` would sample
  hull and noise at different points — relief sliding across the surface as the dial turns, which presents
  as a correlation bug.
- ⚠️ **One `vec4f` slot per entry, strictly.** `SlotCeiling = 48`; the host asserts against more than four
  dials. Eight hull parameters is what forced the `HullMass`/`HullRelief` split.
- ⚠️ **Probe the tunnel off-centre.** It sweeps at elevation −0.42 m, so a single centre-lane scan reports
  "no bore" on a perfectly good seed. Scan all lanes.
- 🔴 **Erosion has no inverse**, so backward scrubbing is *remembered*, never computed. Determinism is what
  legitimises restore-nearest-snapshot + re-step: a round depends only on the density it reads plus the
  profile, whose only time-varying term derives from the round number, not a wall clock. **If any process
  ever takes a genuinely random input, this breaks** and every round must be stored.
- ⚠️ **Snapshot budget is a BYTE ceiling, not a count** — the same count costs 8× at bake tier (128³ =
  8 MiB/buffer vs 256³ = 64 MiB).
- ⚠️ **WGSL has no f32 atomics** → voxel transfer is gather + ping-pong, never scatter.
- ⚠️ **A density grid is not a distance field** → fixed-step march, step ≤ 0.7 cell. And `SmoothMaximum`
  yields a *bound*, not a distance: safe for a fixed-step seed dispatch and a clamped preview, **not** for
  an unclamped sphere trace.
- ⚠️ **`mid_level 0` vs `0.5` is not cosmetic.** 0 is additive only (inflates into lumps); 0.5 is signed
  (cuts as much as it adds). Running the coarse displacement stage signed eats through thin hull parts and
  opens unrequested holes.

---

## 📝 Blender rock generator — what the source actually says

Verified against `add_mesh_rocks/rockgen.py`, `randomize_texture.py`, `utils.py`, `factory.xml`. Three of
the six assumptions carried into this port were **wrong**:

| Assumed | Actually |
|---|---|
| starts from an icosphere | **12 hand-authored 7/8/10-vertex hulls**, chosen at random |
| Musgrave type is configurable | **MULTIFRACTAL hardcoded** (fBm / hetero-terrain / ridged sit commented out) |
| `mat_hard` drives displacement | **no material code exists at all** — `mat_*` in `factory.xml` is vestigial; `mat_hard` was Blender-Internal *specular hardness*, which never touched geometry |

🔴 **The transferable idea is crease-then-subdivide, not the mesh.** Creasing edges in three jittered tiers
and *then* subdividing twice is what makes the output read as rock: some arrises survive as facets, others
round away. The SDF analogue is a plane-intersection hull where **each plane carries its own blend radius**
— which is why `DistanceToPlaneHull` smooth-maxes plane by plane instead of taking one hard max and
rounding uniformly. One global round makes every arris identically soft: the machined look the whole
exercise exists to escape.

📝 **Multifractal MULTIPLIES octaves** (unlike fBm's sum), giving position-dependent amplitude — some
regions smooth, others jagged, from one texture. A good part of why the addon's output does not look
uniformly noisy.

📝 **Fibonacci sphere for plane normals, not random.** Random normals clump; two near-parallel planes cut
nearly the same slab, and with enough planes it degenerates toward a sphere. Golden angle = 2.39996323.
