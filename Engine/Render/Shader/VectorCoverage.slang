/*============================================================================================================================================
                                                           VECTORCOVERAGE.SLANG
============================================================================================================================================*/
// 🧩 GPU contour winding, primitive coverage, gradients, Boolean evaluation, masking and ordered transparent artwork.

struct VectorCoverageInstruction
{
    uint4 Topology;
    float4 Bounds;
    float4 Transform;
    float4 Colour;
    float4 ColourEnd;
    float4 Paint;
};
struct VectorCubicSegment { float4 StartControl; float4 ControlEnd; };
struct CoverageProjection { uint Width; uint Height; uint Count; uint Root; };
[[vk::push_constant]] ConstantBuffer<CoverageProjection> Projection;
[[vk::binding(0, 0)]] StructuredBuffer<VectorCoverageInstruction> Instructions;
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Coverage;
[[vk::binding(2, 0)]] StructuredBuffer<VectorCubicSegment> Segments;

float SegmentDistance(float2 Point, float2 Start, float2 End)
{
    float2 Direction = End - Start;
    return length(Point - Start - Direction * saturate(dot(Point - Start, Direction) / max(dot(Direction, Direction), 1e-20)));
}
float2 CubicPoint(VectorCubicSegment Segment, float Parameter)
{
    float2 First = lerp(Segment.StartControl.xy, Segment.StartControl.zw, Parameter);
    float2 Second = lerp(Segment.StartControl.zw, Segment.ControlEnd.xy, Parameter);
    float2 Third = lerp(Segment.ControlEnd.xy, Segment.ControlEnd.zw, Parameter);
    return lerp(lerp(First, Second, Parameter), lerp(Second, Third, Parameter), Parameter);
}
float CubicWinding(VectorCubicSegment Segment, float2 Point)
{
    // 📝 Partition at analytic Y extrema, then solve each monotonic crossing on the GPU.
    float4 Heights = float4(Segment.StartControl.y, Segment.StartControl.w, Segment.ControlEnd.y, Segment.ControlEnd.w);
    float A = -Heights.x + 3 * Heights.y - 3 * Heights.z + Heights.w;
    float B = 3 * Heights.x - 6 * Heights.y + 3 * Heights.z;
    float C = -3 * Heights.x + 3 * Heights.y;
    float Cuts[4]; Cuts[0] = 0; uint Count = 1;
    if (abs(A) > 1e-8)
    {
        float Discriminant = B * B - 3 * A * C;
        if (Discriminant > 0)
        {
            float First = (-B - sqrt(Discriminant)) / (3 * A);
            float Second = (-B + sqrt(Discriminant)) / (3 * A);
            if (First > Second) { float Exchange = First; First = Second; Second = Exchange; }
            if (First > 0 && First < 1) Cuts[Count++] = First;
            if (Second > 0 && Second < 1) Cuts[Count++] = Second;
        }
    }
    else if (abs(B) > 1e-8)
    {
        float Critical = -C / (2 * B);
        if (Critical > 0 && Critical < 1) Cuts[Count++] = Critical;
    }
    Cuts[Count++] = 1;
    float Winding = 0;
    for (uint Interval = 1; Interval < Count; ++Interval)
    {
        float Lower = Cuts[Interval - 1], Upper = Cuts[Interval];
        float StartY = CubicPoint(Segment, Lower).y, EndY = CubicPoint(Segment, Upper).y;
        bool Ascending = EndY > StartY;
        if (!(Point.y >= min(StartY, EndY) && Point.y < max(StartY, EndY))) continue;
        for (uint Iteration = 0; Iteration < 24; ++Iteration)
        {
            float Centre = (Lower + Upper) * .5;
            if ((CubicPoint(Segment, Centre).y < Point.y) == Ascending) Lower = Centre; else Upper = Centre;
        }
        if (CubicPoint(Segment, (Lower + Upper) * .5).x > Point.x) Winding += Ascending ? 1 : -1;
    }
    return Winding;
}
float ShapeDistance(VectorCoverageInstruction Instruction, float2 Local)
{
    uint Category = Instruction.Topology.x;
    float2 HalfExtent = Instruction.Bounds.zw * .5;
    if (Category == 0)
    {
        float2 Outside = abs(Local) - HalfExtent;
        return length(max(Outside, 0)) + min(max(Outside.x, Outside.y), 0);
    }
    if (Category == 1) return (length(Local / HalfExtent) - 1) * min(HalfExtent.x, HalfExtent.y);
    if (Category == 6)
    {
        float Winding = 0, Distance = 1e10;
        for (uint Curve = 0; Curve < Instruction.Topology.z; ++Curve)
        {
            VectorCubicSegment Segment = Segments[Instruction.Topology.y + Curve];
            Segment.StartControl *= float4(Instruction.Bounds.zw, Instruction.Bounds.zw);
            Segment.ControlEnd *= float4(Instruction.Bounds.zw, Instruction.Bounds.zw);
            Winding += CubicWinding(Segment, Local);
            float2 Start = Segment.StartControl.xy;
            for (uint Interval = 1; Interval <= 32; ++Interval)
            {
                float2 End = CubicPoint(Segment, Interval / 32.0);
                Distance = min(Distance, SegmentDistance(Local, Start, End)); Start = End;
            }
        }
        bool Interior = Instruction.Topology.w == 1 ? (uint(abs(Winding)) % 2 != 0) : abs(Winding) > .5;
        return Interior ? -Distance : Distance;
    }
    uint Count = Category == 2 ? 4 : Category == 3 ? 3 : uint(Instruction.Paint.z);
    if (Category == 5) Count *= 2;
    float Distance = 1e10; bool Inside = false;
    float2 Previous = 0;
    for (uint Corner = 0; Corner <= Count; ++Corner)
    {
        float Angle = -1.57079632679 + 6.28318530718 * (Corner % Count) / Count;
        float Radius = Category == 5 && Corner % 2 == 1 ? Instruction.Paint.y : 1;
        float2 Current = float2(cos(Angle), sin(Angle)) * HalfExtent * Radius;
        if (Corner > 0)
        {
            Distance = min(Distance, SegmentDistance(Local, Previous, Current));
            if ((Previous.y > Local.y) != (Current.y > Local.y) && Local.x < (Current.x - Previous.x) * (Local.y - Previous.y) / (Current.y - Previous.y) + Previous.x) Inside = !Inside;
        }
        Previous = Current;
    }
    return Inside ? -Distance : Distance;
}
void EvaluateVectorPoint(float2 Point, float Antialias, out float4 Evaluations[128])
{
    for (uint Index = 0; Index < Projection.Count; ++Index)
    {
        VectorCoverageInstruction Instruction = Instructions[Index];
        uint Category = Instruction.Topology.x;
        float4 Result = 0;
        if (Category <= 6)
        {
            float2 Translation = Point - Instruction.Bounds.xy;
            float Sine = sin(Instruction.Transform.x), Cosine = cos(Instruction.Transform.x);
            float2 Local = float2(Cosine * Translation.x + Sine * Translation.y, -Sine * Translation.x + Cosine * Translation.y);
            float Distance = ShapeDistance(Instruction, Local);
            float Fill = Instruction.Paint.w != 0 ? saturate(.5 - Distance / Antialias) : 0;
            float Stroke = Instruction.Transform.y > 0 ? saturate(.5 - (abs(Distance) - Instruction.Transform.y * .5) / Antialias) : 0;
            float Fraction = Instruction.Transform.w == 1 ? saturate(.5 + dot(Local / Instruction.Bounds.zw, float2(cos(Instruction.Paint.x), sin(Instruction.Paint.x)))) : Instruction.Transform.w == 2 ? saturate(length(Local / Instruction.Bounds.zw) * 2) : 0;
            Result = lerp(Instruction.Colour, Instruction.ColourEnd, Fraction);
            Result.a *= max(Fill, Stroke) * Instruction.Transform.z;
        }
        else
        {
            float4 Lower = Evaluations[Instruction.Topology.y], Upper = Evaluations[Instruction.Topology.z];
            Result = Lower;
            if (Category == 16) Result.a = max(Lower.a, Upper.a);
            if (Category == 17) Result.a = min(Lower.a, 1 - Upper.a);
            if (Category == 18) Result.a = min(Lower.a, Upper.a);
            if (Category == 19) Result.a = max(min(Lower.a, 1 - Upper.a), min(Upper.a, 1 - Lower.a));
            if (Category == 24 || Category == 25)
            {
                float Mask = Upper.a * (Category == 25 ? dot(Upper.rgb, float3(.2126, .7152, .0722)) : 1);
                if (Instruction.Topology.w != 0) Mask = 1 - Mask;
                Result.a = Lower.a * Mask * Instruction.Transform.z;
            }
            if (Category == 28)
            {
                Result.a = Upper.a + Lower.a * (1 - Upper.a);
                Result.rgb = Result.a > 0 ? (Upper.rgb * Upper.a + Lower.rgb * Lower.a * (1 - Upper.a)) / Result.a : 0;
            }
        }
        Evaluations[Index] = Result;
    }
}
[shader("compute")]
[numthreads(8, 8, 1)]
void VectorCoverageCompute(uint3 Pixel : SV_DispatchThreadID)
{
    if (Pixel.x >= Projection.Width || Pixel.y >= Projection.Height) return;
    float2 Point = (float2(Pixel.xy) + .5) / float2(Projection.Width, Projection.Height) * 512;
    float4 Evaluations[128];
    EvaluateVectorPoint(Point, 512.0 / min(Projection.Width, Projection.Height), Evaluations);
    Coverage[Pixel.xy] = Evaluations[Projection.Root];
}
[shader("compute")]
[numthreads(8, 8, 1)]
void VectorSelectionCompute(uint3 Pixel : SV_DispatchThreadID)
{
    if (Pixel.x != 0 || Pixel.y != 0) return;
    float4 Evaluations[128];
    float2 Point = Instructions[128].Bounds.xy;
    if (any(Point < 0) || any(Point > 512))
    {
        Coverage[uint2(0, 0)] = 0; return;
    }
    EvaluateVectorPoint(Point, 1, Evaluations);
    uint Token = 0;
    for (uint Index = 0; Index < Projection.Root; ++Index)
    {
        uint4 Candidate = Instructions[128 + Index].Topology;
        // 📝 Match nonzero coverage at the authored tile's antialias footprint; keep holes and masks on the GPU.
        if (Evaluations[Candidate.x].a >= (1.0 / 255.0)) Token = Candidate.y;
    }
    Coverage[uint2(0, 0)] = float4(Token & 255, (Token >> 8) & 255, (Token >> 16) & 255, Token >> 24) / 255.0;
}
