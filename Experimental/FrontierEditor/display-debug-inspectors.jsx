import React from "react";
import { PzIcon } from "./project-zero-icons.jsx";

export const DisplayChannels = Object.freeze([
    { id: 0, code: "00", label: "Final image", detail: "Tone-mapped ACES composite · direct + ReSTIR GI + BVH reflections + TAA" },
    { id: 1, code: "01", label: "Direct sun only", detail: "Primary directional + clustered IES illuminance without indirect bounce" },
    { id: 2, code: "02", label: "Indirect diffuse (GI)", detail: "Multi-bounce ReSTIR GI diffuse radiance modulated by surface albedo" },
    { id: 3, code: "03", label: "Indirect irradiance (denoised)", detail: "Demodulated diffuse irradiance after SVGF à-trous wavelet passes" },
    { id: 4, code: "04", label: "ReSTIR irradiance (raw, no denoise)", detail: "Unfiltered spatio-temporal reservoir radiance before SVGF" },
    { id: 5, code: "05", label: "Glossy reflections", detail: "Multi-bounce BVH GGX VNDF & mirror reflection buffer" },
    { id: 6, code: "06", label: "BVH / Distance-field inspection", detail: "TLAS/BLAS traversal heat or 4-cascade toroidal SDF distance bands" },
    { id: 7, code: "07", label: "Surface cards (texel space)", detail: "2048² surface-card atlas irradiance & multi-bounce radiosity tiles" },
    { id: 8, code: "08", label: "Normals", detail: "Octahedral world-space shading normals from G-buffer" },
    { id: 9, code: "09", label: "Albedo", detail: "Base diffuse reflectance & procedural surface micro-textures" },
    { id: 10, code: "10", label: "Fresnel reflectance (environment)", detail: "View-dependent F0 / clearcoat specular lobe response" }
]);

export const AccelerationSubchannels = Object.freeze([
    { id: 0, label: "Cascade bands (0..3)" },
    { id: 1, label: "BVH TLAS / BLAS traversal heat" },
    { id: 2, label: "Dynamic brick rewrites" },
    { id: 3, label: "Sphere-trace step count" }
]);

export const displayDebugDefaults = Object.freeze({
    // Display settings (mirrors FrontierEngine)
    rayTracing: true,
    rayTracedReflections: true,
    rayTracedShadows: true,
    multiBounceNee: true,
    russianRoulette: true,
    fireflyFilter: true,
    giBounces: 3,
    reflectionBounces: 2,
    risCandidates: 2,
    qualityGrade: "High",
    renderScale: 1.0,
    traceScale: 2,
    autoExposure: true,
    exposureComp: 0.0,
    svgfDenoiser: true,
    diffuseWavelets: 3,
    glossyWavelets: 2,
    colourSigma: 4.0,
    taaEnabled: true,
    // Debug channel settings (mirrors FrontierEngine)
    debugChannel: 0,
    accelInspection: 1,
    hudOverlay: false,
    sliceInset: false,
    sliceHeight: 0.6
});

export function DisplayInspector({ v, set, slider })
{
    const bvh = v("rayTracing") !== false;
    const reflOn = v("rayTracedReflections") !== false;
    const scale = Number(v("renderScale") ?? 1.0);
    const traceDiv = Number(v("traceScale") ?? 2);
    const outW = Math.round(1920 * Math.min(scale, 1.25));
    const outH = Math.round(1080 * Math.min(scale, 1.25));

    return (
        <div className="lighting-panel pz-inspector-stack">
            <section className="workspace-card">
                <h2>
                    <i />
                    Display &amp; ray-tracing pipeline
                    <small>{bvh ? "BVH · PT" : "SDF · GI"}</small>
                </h2>
                <div className="workspace-number">
                    {outW}×{outH}
                    <small>px · trace 1/{traceDiv}</small>
                </div>
                <p>
                    Mirrors FrontierEngine’s active display &amp; multi-bounce path-tracing contract before C++ port.
                    BVH reflections and 3-bounce ReSTIR GI are enabled by default.
                </p>
                <div className="mp-rail">
                    <div className="mp-pill">
                        <span className="k">ARCHITECTURE</span>
                        <span className="v">{bvh ? "BVH ReSTIR PT" : "SDF Multi-Bounce"}</span>
                    </div>
                    <div className="mp-pill">
                        <span className="k">BOUNCE BUDGET</span>
                        <span className="v">{v("giBounces")}B GI · {v("reflectionBounces")}B Refl</span>
                    </div>
                    <div className="mp-pill">
                        <span className="k">REFLECTIONS</span>
                        <span className="v">{reflOn ? "BVH GGX (Default)" : "SDF Cone"}</span>
                    </div>
                    <div className="mp-pill">
                        <span className="k">RECONSTRUCTION</span>
                        <span className="v">{v("svgfDenoiser") !== false ? `${v("diffuseWavelets")}/${v("glossyWavelets")} Wavelets` : "Raw"}</span>
                    </div>
                </div>
            </section>

            <section className="lp-card">
                <header>
                    <h3><PzIcon name="graphics-card-2d" size={16} /> Renderer &amp; multi-bounce budget</h3>
                    <span className="small-pill">{v("qualityGrade")}</span>
                </header>
                <div className="pz-segmented" role="group" aria-label="Renderer architecture">
                    {[["BVH ReSTIR Path Tracer", true], ["SDF Distance-Field GI", false]].map(([label, mode]) => (
                        <button key={label} type="button" aria-pressed={bvh === mode} onClick={() => set("rayTracing", mode)}>
                            {label}
                        </button>
                    ))}
                </div>
                <div className="pz-field-list">
                    <label className="pz-slider-row">
                        <div><span>GI path bounces</span><strong>{v("giBounces")} <small>bounces</small></strong></div>
                        {slider("giBounces", 1, 4, 1)}
                    </label>
                    <label className="pz-slider-row">
                        <div><span>Reflection bounces</span><strong>{v("reflectionBounces")} <small>bounces</small></strong></div>
                        {slider("reflectionBounces", 1, 4, 1)}
                    </label>
                    <label className="pz-slider-row">
                        <div><span>Initial RIS candidates (M)</span><strong>{v("risCandidates")}</strong></div>
                        {slider("risCandidates", 1, 4, 1)}
                    </label>
                </div>
                <div className="pz-toggle-grid">
                    {[
                        ["rayTracedReflections", "BVH reflections (default on)"],
                        ["rayTracedShadows", "Ray-traced direct & IES shadows"],
                        ["multiBounceNee", "Multi-bounce NEE + RIS"],
                        ["russianRoulette", "Russian roulette"],
                        ["fireflyFilter", "Firefly suppression"]
                    ].map(([key, label]) => {
                        const on = v(key) !== false;
                        return (
                            <button key={key} type="button" className={`pz-chip-toggle ${on ? "is-on" : ""}`} aria-pressed={on} onClick={() => set(key, !on)}>
                                <i />
                                <span>{label}</span>
                                <small>{on ? "ON" : "OFF"}</small>
                            </button>
                        );
                    })}
                </div>
            </section>

            <section className="lp-card">
                <header>
                    <h3><PzIcon name="environment-exposure" size={16} /> Resolution, exposure &amp; SVGF quality</h3>
                </header>
                <div className="pz-segmented" role="group" aria-label="Quality grade">
                    {["Low", "Medium", "High"].map((grade) => (
                        <button key={grade} type="button" aria-pressed={v("qualityGrade") === grade} onClick={() => set("qualityGrade", grade)}>
                            {grade}
                        </button>
                    ))}
                </div>
                <div className="pz-segmented" role="group" aria-label="Trace resolution">
                    {[["Full (1/1)", 1], ["Half (1/2)", 2], ["Quarter (1/4)", 4]].map(([label, scaleDiv]) => (
                        <button key={label} type="button" aria-pressed={Number(v("traceScale")) === scaleDiv} onClick={() => set("traceScale", scaleDiv)}>
                            {label}
                        </button>
                    ))}
                </div>
                <div className="pz-field-list">
                    <label className="pz-slider-row">
                        <div><span>Render scale</span><strong>{Number(v("renderScale")).toFixed(2)}<small>×</small></strong></div>
                        {slider("renderScale", 0.25, 2.0, 0.05)}
                    </label>
                    <label className="pz-slider-row">
                        <div><span>Exposure compensation</span><strong>{Number(v("exposureComp")) >= 0 ? "+" : ""}{Number(v("exposureComp")).toFixed(1)} <small>EV</small></strong></div>
                        {slider("exposureComp", -4, 4, 0.1)}
                    </label>
                    <label className="pz-slider-row">
                        <div><span>SVGF diffuse wavelet passes</span><strong>{v("diffuseWavelets")}</strong></div>
                        {slider("diffuseWavelets", 1, 5, 1)}
                    </label>
                    <label className="pz-slider-row">
                        <div><span>SVGF glossy wavelet passes</span><strong>{v("glossyWavelets")}</strong></div>
                        {slider("glossyWavelets", 1, 4, 1)}
                    </label>
                    <label className="pz-slider-row">
                        <div><span>Luminance edge-stopping sigma</span><strong>{Number(v("colourSigma")).toFixed(2)} <small>σ</small></strong></div>
                        {slider("colourSigma", 1.0, 8.0, 0.25)}
                    </label>
                </div>
                <div className="pz-toggle-grid">
                    {[
                        ["autoExposure", "Auto exposure Metering"],
                        ["svgfDenoiser", "SVGF spatio-temporal denoiser"],
                        ["taaEnabled", "Temporal antialiasing (TAA)"]
                    ].map(([key, label]) => {
                        const on = v(key) !== false;
                        return (
                            <button key={key} type="button" className={`pz-chip-toggle ${on ? "is-on" : ""}`} aria-pressed={on} onClick={() => set(key, !on)}>
                                <i />
                                <span>{label}</span>
                                <small>{on ? "ON" : "OFF"}</small>
                            </button>
                        );
                    })}
                </div>
            </section>
        </div>
    );
}

export function DebugChannelsInspector({ v, set, slider })
{
    const activeId = Number(v("debugChannel") ?? 0);
    const activeEntry = DisplayChannels.find((c) => c.id === activeId) || DisplayChannels[0];

    return (
        <div className="lighting-panel pz-inspector-stack">
            <section className="workspace-card">
                <h2>
                    <i className={activeId > 0 ? "warm" : ""} />
                    Active debug channel
                    <small>F3 / Shift+F3</small>
                </h2>
                <div className="workspace-number">
                    {activeEntry.code}
                    <small>{activeEntry.label}</small>
                </div>
                <p>{activeEntry.detail}</p>
            </section>

            <section className="lp-card">
                <header>
                    <h3><PzIcon name="editor-debug-bug" size={16} /> Inspection channels</h3>
                    {activeId !== 0 && (
                        <button type="button" className="channel-reset" onClick={() => set("debugChannel", 0)}>
                            Return to Final image
                        </button>
                    )}
                </header>
                <div className="pz-channel-list" role="radiogroup" aria-label="Debug channels">
                    {DisplayChannels.map((ch) => {
                        const selected = ch.id === activeId;
                        return (
                            <button
                                key={ch.id}
                                type="button"
                                role="radio"
                                aria-checked={selected}
                                className={`pz-channel-row ${selected ? "is-selected" : ""}`}
                                onClick={() => set("debugChannel", ch.id)}
                            >
                                <span className="pz-channel-code">{ch.code}</span>
                                <span className="pz-channel-text">
                                    <strong>{ch.label}</strong>
                                    <small>{ch.detail}</small>
                                </span>
                                <i className={selected ? "on" : ""} />
                            </button>
                        );
                    })}
                </div>
            </section>

            <section className="lp-card">
                <header>
                    <h3><PzIcon name="editor-grid" size={16} /> BVH / SDF diagnostic overlays</h3>
                </header>
                <label className="pz-select-label">
                    <span>BVH / SDF sub-inspection</span>
                    <select value={v("accelInspection") ?? 1} onChange={(e) => set("accelInspection", Number(e.target.value))}>
                        {AccelerationSubchannels.map((sub) => (
                            <option key={sub.id} value={sub.id}>{sub.label}</option>
                        ))}
                    </select>
                </label>
                <div className="pz-field-list">
                    <label className="pz-slider-row">
                        <div><span>Distance slice height</span><strong>{Number(v("sliceHeight")).toFixed(2)} <small>m</small></strong></div>
                        {slider("sliceHeight", 0.05, 4.0, 0.05)}
                    </label>
                </div>
                <div className="pz-toggle-grid">
                    {[
                        ["hudOverlay", "Viewport telemetry readout (H)"],
                        ["sliceInset", "Toroidal GDF slice inset (J)"]
                    ].map(([key, label]) => {
                        const on = !!v(key);
                        return (
                            <button key={key} type="button" className={`pz-chip-toggle ${on ? "is-on" : ""}`} aria-pressed={on} onClick={() => set(key, !on)}>
                                <i />
                                <span>{label}</span>
                                <small>{on ? "ON" : "OFF"}</small>
                            </button>
                        );
                    })}
                </div>
            </section>
        </div>
    );
}
