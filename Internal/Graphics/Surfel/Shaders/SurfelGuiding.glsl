/*==============================================================================================================================================
                                                            SURFELGUIDING.GLSL
==============================================================================================================================================*/
// 🧩 Spatial Lobe Guiding (SLG) — the per-surfel importance-sampling brain. Left to a plain cosine hemisphere, a surfel's integrate would fire most
//    of its rays at nothing and only occasionally stumble onto the one bright direction (a window, the sun's bounce off a wall), so its irradiance
//    estimate would be pure noise. SLG fixes this: each surfel carries a small 8x8 histogram over its hemisphere (the LOBES) recording where the
//    light it has already seen came from, and future rays are drawn preferentially toward those cells. The histogram is stored in the hemi-oct-square
//    parameterisation — a bijection that flattens the upper hemisphere onto the unit square with a known area Jacobian, so a uv cell maps cleanly to a
//    solid-angle cone and back. Sampling is MIS'd against the cosine lobe (mixPdf in the includer), and the histogram is updated by an EMA each sample.
//
//    🔴 THIS IS AN #include MODULE WITH NO main(). It reads and writes ONE SSBO, the guiding buffer, by the bare array name `SurfelGuidingBuffer[]` —
//       the includer (.comp) MUST declare a `buffer` block whose member is `float SurfelGuidingBuffer[];` before #include'ing this file, exactly as
//       webgiya's guidingBuffer.value[] is a global the wgslFn units close over. 72 floats per surfel: 64 lobe weights (row-major 8x8) + 8 row-sum
//       cache. Everything is indexed off `SurfelIndex * SLG_TOTAL_FLOATS`. Compile through the .comp with glslc -I, never standalone.
//
//    🔴 PORTED 1:1 from surfelIntegratePass.ts SLG helpers (:228-594). The learning rate (0.02), the row-sum cache maintenance, the "clamp to 1e-6..
//       1-1e-6 before decode" endpoint guard, and the hemi-oct Jacobian 2/|v|^3 are all exact. F8: one integrate lane owns one surfel, so the
//       read-modify-write of the histogram mid-loop is safe by construction (no two invocations touch the same base index) — see Phase-2 fact 2.

#ifndef FRONTIER_SURFEL_SURFELGUIDING_GLSL
#define FRONTIER_SURFEL_SURFELGUIDING_GLSL

#ifndef FRONTIER_SURFEL_PI
#define FRONTIER_SURFEL_PI
const float SURFEL_PI = 3.141592653589793238462;
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                          SLG CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Ported 1:1 from webgiya consts (:80-84). SLG_DIM is the histogram edge (8x8 = 64 lobes); SLG_TOTAL_FLOATS = 64 weights + 8 row sums = 72 floats
//    per surfel. LEARNING_RATE is the EMA rate the update splats new luminance in at. These are STRUCTURAL — they describe the histogram shape, not the
//    world, so they transfer verbatim.
const uint  SLG_DIM         = 8u;
const uint  SLG_LOBE_COUNT  = 64u;   // SLG_DIM * SLG_DIM
const uint  SLG_TOTAL_FLOATS = 72u;  // SLG_LOBE_COUNT + SLG_DIM (row-sum cache)
const float SLG_LEARNING_RATE = 0.02;

// The sample a guided draw returns: the LOCAL-space direction (z-up, surfel frame) AND its hemi-oct-square uv — the uv is what pdfSLG and the update
// index by, so both are returned together (webgiya's SLGSample struct, :90-93).
struct SLGSample
{
    vec3 DirectionLocal;
    vec2 Uv;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       BASIS + COSINE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// getTangentBasis (:228-236). Build an orthonormal frame whose Z axis is the surfel normal, so a local z-up direction rotates into world by this matrix.
mat3 SurfelTangentBasis(vec3 Normal)
{
    vec3 NormalUnit = normalize(Normal);
    vec3 Up         = (abs(NormalUnit.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 Tangent    = normalize(cross(Up, NormalUnit));
    vec3 Bitangent  = cross(NormalUnit, Tangent);
    return mat3(Tangent, Bitangent, NormalUnit);
}

// sampleCosineHemisphereLocal (:239-258). Cosine-weighted hemisphere sample in the LOCAL z-up frame (the commented-out uniform variant is left dead).
vec3 SurfelSampleCosineHemisphereLocal(vec2 U)
{
    float Radius = sqrt(U.x);
    float Theta  = 2.0 * SURFEL_PI * U.y;
    float X = Radius * cos(Theta);
    float Y = Radius * sin(Theta);
    float Z = sqrt(max(0.0, 1.0 - U.x));
    return vec3(X, Y, Z);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HEMI-OCT-SQUARE BIJECTION
//------------------------------------------------------------------------------------------------------------------------

// hemiOctSquareEncode (:263-277). Local hemisphere direction (z>=0) -> uv in [0,1]^2. L1-normalise onto the diamond, rotate the diamond to fill the square.
vec2 SurfelHemiOctSquareEncode(vec3 Direction)
{
    float InvL1 = 1.0 / (abs(Direction.x) + abs(Direction.y) + Direction.z);
    vec2  P     = Direction.xy * InvL1;                 // diamond: |px|+|py| <= 1
    vec2  Q     = vec2(P.x + P.y, P.x - P.y);           // rotate/scale diamond -> full square [-1,1]^2
    return Q * 0.5 + 0.5;                               // [0,1]^2
}

// hemiOctSquareDecode (:279-288). uv in [0,1]^2 -> normalised hemisphere direction. Inverse of the encode; z is recovered from the L1 residual.
vec3 SurfelHemiOctSquareDecode(vec2 Uv)
{
    vec2  Q = Uv * 2.0 - 1.0;                           // [-1,1]^2
    vec2  P = vec2(Q.x + Q.y, Q.x - Q.y) * 0.5;         // diamond
    float Z = max(0.0, 1.0 - abs(P.x) - abs(P.y));
    return normalize(vec3(P.x, P.y, Z));               // guaranteed hemi
}

// slgSafeU01 (:292-297). Keep a uniform sample off the exact 0/1 endpoints (blue-noise textures often carry exact endpoints; the z==0 boundary is singular).
float SurfelSafeU01(float X)
{
    return clamp(X, 1e-6, 1.0 - 1e-6);
}

// hemiOctJacobian (:299-318). The area Jacobian J = dOmega / d(uv area) at a uv — steradians per unit uv^2. Needed so a histogram cell's discrete
// probability converts to a continuous solid-angle pdf. J = 2 / |v|^3 where v is the un-normalised decode vector; inverseSqrt for speed, clamped vs INF.
float SurfelHemiOctJacobian(vec2 Uv)
{
    vec2  Q = Uv * 2.0 - 1.0;
    vec2  P = vec2(Q.x + Q.y, Q.x - Q.y) * 0.5;
    float Z = 1.0 - abs(P.x) - abs(P.y);
    vec3  V = vec3(P.x, P.y, Z);
    float R2 = dot(V, V);
    float InvR = inversesqrt(max(1e-12, R2));
    return 2.0 * InvR * InvR * InvR;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       HISTOGRAM PDF + AXIS
//------------------------------------------------------------------------------------------------------------------------

// pdfSLG (:321-346). The continuous pdf the guided draw would produce at a given uv. P(cell) = w/slgMass; the cell covers 1/64 of uv area, so the uv
// density is P(cell)*64; divide by J to convert uv density to solid-angle density. Returns 0 when the histogram carries no mass yet.
float SurfelPdfSLG(uint SurfelIndex, vec2 Uv, float SlgMass)
{
    if (SlgMass <= 1e-6) { return 0.0; }

    uint CellX = min(uint(floor(Uv.x * float(SLG_DIM))), SLG_DIM - 1u);
    uint CellY = min(uint(floor(Uv.y * float(SLG_DIM))), SLG_DIM - 1u);
    uint Index = CellY * SLG_DIM + CellX;

    uint  BaseIndex = SurfelIndex * SLG_TOTAL_FLOATS;
    float Weight    = max(0.0, SurfelGuidingBuffer[BaseIndex + Index]);

    float Jacobian = SurfelHemiOctJacobian(Uv);
    return (Weight / SlgMass) * float(SLG_LOBE_COUNT) / Jacobian;
}

// slgGetLobeAxisLocal (:349-359). The local-space centre direction of lobe `Index` — the decode of that cell's centre uv. Used by the mean-direction derive.
vec3 SurfelLobeAxisLocal(uint Index)
{
    float X = float(Index % SLG_DIM);
    float Y = float(Index / SLG_DIM);
    vec2  Uv = vec2((X + 0.5) / float(SLG_DIM), (Y + 0.5) / float(SLG_DIM));
    return SurfelHemiOctSquareDecode(Uv);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      HISTOGRAM SAMPLE + UPDATE
//------------------------------------------------------------------------------------------------------------------------

// slgSampleLobeIndex (:433-470). Draw a lobe index proportional to its weight, using the row-sum cache for a two-level (row then column) walk. `u` is a
// uniform in [0,1); returns -1 when the histogram is empty. The "force selection at the last element" guards a floating-point shortfall from falling through.
int SurfelSampleLobeIndex(uint SurfelIndex, float U, float TotalIn)
{
    if (TotalIn <= 1e-6) { return -1; }

    uint BaseIndex    = SurfelIndex * SLG_TOTAL_FLOATS;
    uint RowSumOffset = BaseIndex + SLG_LOBE_COUNT;

    float Target = clamp(U * TotalIn, 0.0, TotalIn);

    // 1) Row select against the row-sum cache.
    uint Row = 0u;
    for (uint R = 0u; R < SLG_DIM; R = R + 1u)
    {
        float Weight = max(0.0, SurfelGuidingBuffer[RowSumOffset + R]);
        if (Target <= Weight || R == SLG_DIM - 1u) { Row = R; break; }
        Target -= Weight;
    }

    // 2) Column select within the chosen row.
    uint RowStart = BaseIndex + Row * SLG_DIM;
    uint Col = 0u;
    for (uint C = 0u; C < SLG_DIM; C = C + 1u)
    {
        float Weight = max(0.0, SurfelGuidingBuffer[RowStart + C]);
        if (Target <= Weight || C == SLG_DIM - 1u) { Col = C; break; }
        Target -= Weight;
    }

    return int(Row * SLG_DIM + Col);
}

// slgUpdateFromSample (:474-521). Splat a sample's luminance into the histogram by bilinear weights over the 2x2 cells around its uv, EMA-blending each
// touched cell toward `Luminance*w` and keeping the row-sum cache in step. Returns the net mass change so the caller can track slgMass live. F8-safe:
// this is a read-modify-write of the surfel's OWN 72 floats, and one lane owns one surfel.
float SurfelUpdateFromSample(uint SurfelIndex, vec2 Uv, float Luminance)
{
    vec2 GridPosition = Uv * float(SLG_DIM) - 0.5;
    vec2 BasePosition = floor(GridPosition);
    vec2 Fraction     = fract(GridPosition);

    uint BaseIndex    = SurfelIndex * SLG_TOTAL_FLOATS;
    uint RowSumOffset = BaseIndex + SLG_LOBE_COUNT;

    float Eta      = SLG_LEARNING_RATE;
    float MassDiff = 0.0;
    for (int Dy = 0; Dy <= 1; Dy = Dy + 1)
    {
        for (int Dx = 0; Dx <= 1; Dx = Dx + 1)
        {
            int CellX = int(BasePosition.x) + Dx;
            int CellY = int(BasePosition.y) + Dy;

            if (CellX >= 0 && CellX < int(SLG_DIM) && CellY >= 0 && CellY < int(SLG_DIM))
            {
                float WeightX = (Dx == 1) ? Fraction.x : (1.0 - Fraction.x);
                float WeightY = (Dy == 1) ? Fraction.y : (1.0 - Fraction.y);
                float Weight  = WeightX * WeightY;
                float Target  = Luminance * Weight;

                uint  Index  = uint(CellY) * SLG_DIM + uint(CellX);
                float OldVal = SurfelGuidingBuffer[BaseIndex + Index];

                // EMA update: decays the old value toward Target (Target==0 in shadow, so a lobe that stops paying off fades).
                float NewVal = mix(OldVal, Target, Eta);
                SurfelGuidingBuffer[BaseIndex + Index] = NewVal;

                // Maintain the row-sum cache.
                float Diff = NewVal - OldVal;
                SurfelGuidingBuffer[RowSumOffset + uint(CellY)] += Diff;
                MassDiff += Diff;
            }
        }
    }
    return MassDiff;
}

// sampleGuidedDirection (:524-577). The MIS draw: with probability pGuide (and if the histogram carries mass) draw a lobe proportional to its weight and
// a uniform uv within that cell; otherwise fall back to a cosine hemisphere sample and encode it. `U` supplies xy (within-cell uv), z (lobe select), w
// (guide-vs-cosine decision). Returns both the local direction and the uv (the caller needs the uv for pdfSLG and the update).
SLGSample SurfelSampleGuidedDirection(uint SurfelIndex, float SlgMass, float PGuide, vec4 U)
{
    float Ux = SurfelSafeU01(U.x);
    float Uy = SurfelSafeU01(U.y);
    float Uz = SurfelSafeU01(U.z);
    float Uw = SurfelSafeU01(U.w);

    SLGSample Out;

    // Guided branch: pick a lobe by weight, then a uniform uv within its cell.
    if (SlgMass > 1e-6 && Uw < PGuide)
    {
        int Chosen = SurfelSampleLobeIndex(SurfelIndex, Uz, SlgMass);
        if (Chosen >= 0)
        {
            uint Col = uint(Chosen) % SLG_DIM;
            uint Row = uint(Chosen) / SLG_DIM;

            vec2 CellUv = vec2((float(Col) + Ux) / float(SLG_DIM),
                               (float(Row) + Uy) / float(SLG_DIM));

            Out.Uv             = CellUv;
            Out.DirectionLocal = SurfelHemiOctSquareDecode(CellUv);
            return Out;
        }
    }

    // Fallback branch: cosine hemisphere in local space, encoded to uv for the pdf/update.
    vec3 DirectionLocal = SurfelSampleCosineHemisphereLocal(vec2(Ux, Uy));
    Out.DirectionLocal = DirectionLocal;
    Out.Uv             = SurfelHemiOctSquareEncode(DirectionLocal);
    return Out;
}

// slgGetTotalMass (:579-594). Sum the row-sum cache to get the histogram's total mass — the normaliser for pdfSLG and the guide/cosine decision.
float SurfelGetTotalMass(uint SurfelIndex)
{
    uint  BaseIndex    = SurfelIndex * SLG_TOTAL_FLOATS;
    uint  RowSumOffset = BaseIndex + SLG_LOBE_COUNT;
    float Total = 0.0;
    for (uint R = 0u; R < SLG_DIM; R = R + 1u)
    {
        Total += max(0.0, SurfelGuidingBuffer[RowSumOffset + R]);
    }
    return Total;
}

#endif
