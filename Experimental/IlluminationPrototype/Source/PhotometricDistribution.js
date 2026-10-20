// LM-63 type-C photometry decoded to absolute candela. Unsupported formats fail explicitly.
export const PhotometricWidth = 181;
export const PhotometricHeight = 37;

export function DecodePhotometricDistribution(Text)
{
    if (!/^IESNA(?::LM-63-\d{4})?/i.test(Text.trim())) throw new Error("Expected an LM-63 IES file");
    const Tilt = /TILT\s*=\s*([^\r\n]+)/i.exec(Text);
    if (!Tilt || Tilt[1].trim().toUpperCase() !== "NONE") throw new Error("Only TILT=NONE is supported");
    const Numbers = Text.slice(Tilt.index + Tilt[0].length).trim().split(/[\s,]+/).map(Number);
    if (Numbers.some(N => !Number.isFinite(N))) throw new Error("Invalid photometric numbers");
    const [Lamps, Lumens, Multiplier, VerticalCount, HorizontalCount, Category, Units] = Numbers;
    if (Category !== 1) throw new Error("Only type-C photometry is supported");
    if (![1, 2].includes(Units) || !Number.isInteger(Lamps) || Lamps < 1 || Multiplier <= 0) throw new Error("Invalid LM-63 header");
    if (!Number.isInteger(VerticalCount) || !Number.isInteger(HorizontalCount) || VerticalCount < 2 || HorizontalCount < 1 || VerticalCount > 4096 || HorizontalCount > 4096) throw new Error("Invalid angular dimensions");
    if (Numbers.length !== 13 + VerticalCount + HorizontalCount + VerticalCount * HorizontalCount) throw new Error("Truncated or extra candela table");
    const Vertical = Numbers.slice(13, 13 + VerticalCount);
    const Horizontal = Numbers.slice(13 + VerticalCount, 13 + VerticalCount + HorizontalCount);
    const Candela = Numbers.slice(13 + VerticalCount + HorizontalCount).map(C => C * Multiplier * Numbers[10]);
    for (const Angles of [Vertical, Horizontal])
    {
        if (Angles.some((Angle, Index) => !Number.isFinite(Angle) || Angle < 0 || (Index && Angle <= Angles[Index - 1]))) throw new Error("Angles must strictly increase");
    }
    if (Vertical.at(-1) > 180 || Horizontal[0] !== 0 || ![0, 90, 180, 360].includes(Horizontal.at(-1))) throw new Error("Unsupported angular coverage/symmetry");
    if (Candela.some(C => !Number.isFinite(C) || C < 0)) throw new Error("Invalid candela");
    const Bracket = (Angles, Angle) =>
    {
        let High = 1;
        while (High < Angles.length - 1 && Angles[High] < Angle) ++High;
        return [High - 1, High, (Angle - Angles[High - 1]) / (Angles[High] - Angles[High - 1])];
    };
    const Evaluate = (Theta, Phi) =>
    {
        if (Theta < Vertical[0] || Theta > Vertical.at(-1)) return 0;
        Phi = ((Phi % 360) + 360) % 360;
        const Extent = Horizontal.at(-1);
        if (Extent === 90) { Phi %= 180; if (Phi > 90) Phi = 180 - Phi; }
        if (Extent === 180 && Phi > 180) Phi = 360 - Phi;
        const [V0, V1, VWeight] = Bracket(Vertical, Theta);
        const Slice = H => Candela[H * VerticalCount + V0] * (1 - VWeight) + Candela[H * VerticalCount + V1] * VWeight;
        if (HorizontalCount === 1) return Slice(0);
        const [H0, H1, HWeight] = Bracket(Horizontal, Phi);
        return Slice(H0) * (1 - HWeight) + Slice(H1) * HWeight;
    };
    const Samples = new Float32Array(PhotometricWidth * PhotometricHeight);
    for (let Phi = 0; Phi < PhotometricHeight; ++Phi)
        for (let Theta = 0; Theta < PhotometricWidth; ++Theta)
            Samples[Phi * PhotometricWidth + Theta] = Evaluate(Theta, Phi * 10);
    // Integrate the resampled distribution over solid angle; lumens=-1 denotes absolute photometry.
    let Flux = 0;
    const Radians = Math.PI / 180;
    for (let Phi = 0; Phi < 360; ++Phi)
        for (let Theta = 0; Theta < 180; ++Theta)
            Flux += 0.25 * (Evaluate(Theta, Phi) + Evaluate(Theta + 1, Phi) + Evaluate(Theta, Phi + 1) + Evaluate(Theta + 1, Phi + 1)) * Radians * (Math.cos(Theta * Radians) - Math.cos((Theta + 1) * Radians));
    return { Samples, Evaluate, Flux, Lamps, Lumens, Multiplier, Vertical, Horizontal,
             Description: /\[LUMINAIRE\]\s*([^\r\n]*)/i.exec(Text)?.[1] || "Imported LM-63" };
}
