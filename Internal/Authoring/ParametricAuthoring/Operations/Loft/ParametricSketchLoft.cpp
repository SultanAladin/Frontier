//==========================================================================================================================================
//                                                              ParametricSketchLoft.cpp
//==========================================================================================================================================
// 🧩 Lofting kernel — interpolate a 3D display surface / solid through 2+ section profiles. See ParametricSketchLoft.h for the orchestration
//    contract. This pass ships the CORE tier: two-profile / multi-section / point-apex / closed-loop / open→sheet, correlated by the
//    Automatic anti-twist search and interpolated ruled (straight-v) or smooth (Catmull-Rom in v). GuideCurves / Centerline / non-Normal
//    end conditions / Connectors are accepted on the LoftSpecification but interpolated in the P4 advanced tier (noted at each seam).

#include "ParametricSketchLoft.h"

#include <array>
#include <cmath>
#include <cstdio>

#include "earcut.hpp"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Sections flatten at this dense fixed budget so the correlation + interpolation see a smooth ring (mirrors the boolean / offset
    //    flatten budget). The (u,v) grid then resamples to CommonSampleCount below, so this is only the pre-resample source density.
    constexpr int   SectionFlattenBudget = 256;   // [-] - dense per-section flatten budget (curved sections)
    constexpr int   CommonSampleCount    = 96;    // [-] - the common ring resolution every section resamples to (the "u" count)
    constexpr int   SmoothStepsPerSpan   = 12;    // [-] - interpolated v-rings inserted between adjacent sections for Smooth transition
    constexpr float DistinctPointEpsilon = 1e-4f; // [mm] - below this two ring points count as coincident (apex / degenerate test)

    // 📝 One flattened section: its ring of world-mm outline points (XY) + its Z plane (from the shape's Elevation) + whether it was a
    //    closed loop. Apex sections collapse to a single distinct point (PointApex true) — the interpolation fans every ring point to it.
    struct LoftSection
    {
        std::vector<ImVec2> Ring;                    // [mm] - the section outline in world XY (already resampled to CommonSampleCount)
        float               Elevation   = 0.0f;      // [mm] - the section's Z plane
        bool                ClosedRing  = false;     // [-]  - the section was a closed loop
        bool                PointApex   = false;     // [-]  - the section collapsed to a single point (cone / dome end)
        ImVec2              ApexPoint   = ImVec2(0, 0); // [mm] - the apex location (valid when PointApex)
    };

    // A 3D position accumulated into the body's flat float arrays.
    struct LoftVertex
    {
        float X = 0.0f, Y = 0.0f, Z = 0.0f;
    };

    void RaiseNotice(ParametricSketchShapeStore& Store, const char* Message)
    {
        std::snprintf(Store.Notice, sizeof(Store.Notice), "%s", Message);
        Store.NoticeTimer = 4.0f;   // [s] - the view decays this each frame
    }

    // Perimeter length of a ring (world mm), closing the loop when Closed. Used to resample a section by arc length.
    float ResolveRingLength(const std::vector<ImVec2>& Ring, bool Closed)
    {
        float Length = 0.0f;
        const int Count = (int)Ring.size();
        for (int Index = 0; Index + 1 < Count; ++Index)
            Length += std::hypot(Ring[Index + 1].x - Ring[Index].x, Ring[Index + 1].y - Ring[Index].y);
        if (Closed && Count >= 2)
            Length += std::hypot(Ring[0].x - Ring[Count - 1].x, Ring[0].y - Ring[Count - 1].y);
        return Length;
    }

    // 📝 Resample a source polyline to exactly SampleCount points evenly by ARC LENGTH, so two sections with wildly different source
    //    densities pair point-for-point. A closed ring wraps (SampleCount points spread around the whole loop); an open run spans end to
    //    end (endpoints included). Degenerate (zero-length) input yields SampleCount copies of the first point (an apex).
    std::vector<ImVec2> ResampleRing(const std::vector<ImVec2>& Source, bool Closed, int SampleCount)
    {
        std::vector<ImVec2> Result;
        Result.reserve(SampleCount);
        const int SourceCount = (int)Source.size();
        if (SourceCount == 0)
            return Result;

        const float Total = ResolveRingLength(Source, Closed);
        if (Total <= DistinctPointEpsilon)
        {
            for (int Index = 0; Index < SampleCount; ++Index)
                Result.push_back(Source.front());
            return Result;
        }

        // Walk the source segments accumulating arc length, appending a point each time we cross the next even target distance.
        const int   SegmentCount = Closed ? SourceCount : SourceCount - 1;
        const float Step         = Total / (float)(Closed ? SampleCount : (SampleCount - 1));
        float       NextTarget   = 0.0f;
        float       Walked       = 0.0f;
        int         Appended     = 0;

        for (int Segment = 0; Segment < SegmentCount && Appended < SampleCount; ++Segment)
        {
            const ImVec2 A = Source[Segment];
            const ImVec2 B = Source[(Segment + 1) % SourceCount];
            const float  SegLength = std::hypot(B.x - A.x, B.y - A.y);
            if (SegLength <= 0.0f)
                continue;

            while (NextTarget <= Walked + SegLength + 1e-6f && Appended < SampleCount)
            {
                const float Fraction = (NextTarget - Walked) / SegLength;
                const float Clamped  = Fraction < 0.0f ? 0.0f : (Fraction > 1.0f ? 1.0f : Fraction);
                Result.push_back(ImVec2(A.x + (B.x - A.x) * Clamped, A.y + (B.y - A.y) * Clamped));
                ++Appended;
                NextTarget += Step;
            }
            Walked += SegLength;
        }

        // Arc-length rounding can leave us one short of the target; pad with the final source point so every ring matches in count.
        while ((int)Result.size() < SampleCount)
            Result.push_back(Closed ? Source.front() : Source.back());
        return Result;
    }

    // 📝 Count the DISTINCT points in a flattened outline (coincident-within-epsilon points collapse). An outline with a single distinct
    //    point is an apex (a cone / dome end); two or fewer overall is too degenerate to loft a ring from.
    int CountDistinctPoints(const std::vector<ImVec2>& Outline)
    {
        int Distinct = 0;
        const int Count = (int)Outline.size();
        for (int Index = 0; Index < Count; ++Index)
        {
            bool Duplicate = false;
            for (int Prior = 0; Prior < Index; ++Prior)
                if (std::hypot(Outline[Index].x - Outline[Prior].x, Outline[Index].y - Outline[Prior].y) <= DistinctPointEpsilon)
                {
                    Duplicate = true;
                    break;
                }
            if (!Duplicate)
                ++Distinct;
        }
        return Distinct;
    }

    // 📝 Anti-twist: rotate Ring's start index so its points best line up with Reference (both already resampled to the same count),
    //    minimising the summed squared XY distance over all offsets. This defuses the "candy-wrapper" twist between adjacent sections.
    //    Automatic correlation only; Connectors (explicit pins) is the P4 tier.
    void CorrelateSectionPoints(std::vector<ImVec2>& Ring, const std::vector<ImVec2>& Reference)
    {
        const int Count = (int)Ring.size();
        if (Count == 0 || (int)Reference.size() != Count)
            return;

        int   BestOffset = 0;
        float BestCost   = -1.0f;
        for (int Offset = 0; Offset < Count; ++Offset)
        {
            float Cost = 0.0f;
            for (int Index = 0; Index < Count; ++Index)
            {
                const ImVec2 P = Ring[(Index + Offset) % Count];
                const ImVec2 Q = Reference[Index];
                const float  Dx = P.x - Q.x;
                const float  Dy = P.y - Q.y;
                Cost += Dx * Dx + Dy * Dy;
            }
            if (BestCost < 0.0f || Cost < BestCost)
            {
                BestCost   = Cost;
                BestOffset = Offset;
            }
        }
        if (BestOffset == 0)
            return;

        std::vector<ImVec2> Rotated;
        Rotated.reserve(Count);
        for (int Index = 0; Index < Count; ++Index)
            Rotated.push_back(Ring[(Index + BestOffset) % Count]);
        Ring.swap(Rotated);
    }

    // Catmull-Rom interpolation of one scalar across four control rings at parameter T in [0,1] (the smooth-v basis).
    float EvaluateCatmullRom(float P0, float P1, float P2, float P3, float T)
    {
        const float T2 = T * T;
        const float T3 = T2 * T;
        return 0.5f * ((2.0f * P1) +
                       (-P0 + P2) * T +
                       (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2 +
                       (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * T3);
    }

    // Push a 3D position + a (temporary zero) normal onto the body arrays; returns the new vertex index.
    uint32_t PushVertex(ParametricSketchLoftBody& Body, float X, float Y, float Z)
    {
        const uint32_t Index = (uint32_t)(Body.Positions.size() / 3);
        Body.Positions.push_back(X);
        Body.Positions.push_back(Y);
        Body.Positions.push_back(Z);
        Body.Normals.push_back(0.0f);
        Body.Normals.push_back(0.0f);
        Body.Normals.push_back(0.0f);
        return Index;
    }

    // Accumulate the triangle (A,B,C)'s geometric normal onto each of its vertices (area-weighted by the cross product's magnitude).
    void AccumulateTriangleNormal(ParametricSketchLoftBody& Body, uint32_t A, uint32_t B, uint32_t C)
    {
        const float* P = Body.Positions.data();
        const float Ax = P[A * 3], Ay = P[A * 3 + 1], Az = P[A * 3 + 2];
        const float Bx = P[B * 3], By = P[B * 3 + 1], Bz = P[B * 3 + 2];
        const float Cx = P[C * 3], Cy = P[C * 3 + 1], Cz = P[C * 3 + 2];
        const float Ux = Bx - Ax, Uy = By - Ay, Uz = Bz - Az;
        const float Vx = Cx - Ax, Vy = Cy - Ay, Vz = Cz - Az;
        const float Nx = Uy * Vz - Uz * Vy;
        const float Ny = Uz * Vx - Ux * Vz;
        const float Nz = Ux * Vy - Uy * Vx;
        const uint32_t Trio[3] = { A, B, C };
        for (uint32_t Which : Trio)
        {
            Body.Normals[Which * 3]     += Nx;
            Body.Normals[Which * 3 + 1] += Ny;
            Body.Normals[Which * 3 + 2] += Nz;
        }
    }

    // Append one triangle into the body and fold its normal into the three shared vertices.
    void AppendTriangle(ParametricSketchLoftBody& Body, uint32_t A, uint32_t B, uint32_t C)
    {
        Body.Indices.push_back(A);
        Body.Indices.push_back(B);
        Body.Indices.push_back(C);
        AccumulateTriangleNormal(Body, A, B, C);
    }

    // Normalize every accumulated vertex normal in place (leaving a zero normal as up so a degenerate vertex still shades).
    void NormalizeBodyNormals(ParametricSketchLoftBody& Body)
    {
        const size_t VertexCount = Body.Normals.size() / 3;
        for (size_t Index = 0; Index < VertexCount; ++Index)
        {
            float& Nx = Body.Normals[Index * 3];
            float& Ny = Body.Normals[Index * 3 + 1];
            float& Nz = Body.Normals[Index * 3 + 2];
            const float Length = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
            if (Length > 1e-8f)
            {
                Nx /= Length;
                Ny /= Length;
                Nz /= Length;
            }
            else
            {
                Nx = 0.0f;
                Ny = 0.0f;
                Nz = 1.0f;
            }
        }
    }

    // 📝 Triangulate one closed section ring (world XY at Elevation) into an end CAP, appending its triangles to the body. Earcut
    //    consumes the ring in 2D; the resulting fan lifts to the section's Z plane. Winding is left to the shared normal accumulation
    //    (a double-sided sheet / solid reads correctly either way once normals average). Skips a degenerate (< 3 point) ring.
    void AppendCap(ParametricSketchLoftBody& Body, const std::vector<ImVec2>& Ring, float Elevation)
    {
        if (Ring.size() < 3)
            return;

        using EarPoint = std::array<double, 2>;
        std::vector<std::vector<EarPoint>> Rings(1);
        Rings[0].reserve(Ring.size());
        for (const ImVec2& Point : Ring)
            Rings[0].push_back({ (double)Point.x, (double)Point.y });

        const std::vector<uint32_t> Local = mapbox::earcut<uint32_t>(Rings);
        if (Local.size() < 3)
            return;

        // Push the cap's own vertex ring (a separate copy so the side-wall vertices keep their averaged normals distinct from the flat cap).
        std::vector<uint32_t> Base;
        Base.reserve(Ring.size());
        for (const ImVec2& Point : Ring)
            Base.push_back(PushVertex(Body, Point.x, Point.y, Elevation));

        for (size_t Index = 0; Index + 2 < Local.size(); Index += 3)
            AppendTriangle(Body, Base[Local[Index]], Base[Local[Index + 1]], Base[Local[Index + 2]]);
    }
}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       SECTION RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Resolve + flatten the ordered sections for a recipe into LoftSection rings, all resampled to CommonSampleCount and correlated
    //    against the prior section (anti-twist). Returns false (with an outcome + notice already raised on Store) when the sections are
    //    too few, mix open + closed, or degenerate. On success OutSections holds >= 2 rings in the recipe's order.
    bool ResolveLoftSections(ParametricSketchShapeStore&       Store,
                             const LoftSpecification& Spec,
                             std::vector<LoftSection>& OutSections,
                             LoftOutcome&             OutFailure)
    {
        // Work over the recipe's explicit section list, or fall back to the current multi-select / lone selected shape.
        std::vector<uint32_t> SectionIds = Spec.SectionProfiles;
        if (SectionIds.empty())
        {
            SectionIds = Store.SelectionSet;
            if (SectionIds.empty() && Store.Selected != 0)
                SectionIds.push_back(Store.Selected);
        }
        if (SectionIds.empty())
        {
            RaiseNotice(Store, "Loft needs selected profiles");
            OutFailure = LoftOutcome::NothingSelected;
            return false;
        }

        int ClosedCount = 0;
        int OpenCount   = 0;
        std::vector<LoftSection> Sections;
        Sections.reserve(SectionIds.size());
        for (uint32_t Identifier : SectionIds)
        {
            ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
            if (!Shape)
                continue;

            std::vector<ImVec2> Outline;
            if (Shape->ClosedEnabled)
                EvaluateFilledPolygon(*Shape, Outline, SectionFlattenBudget);
            else
                EvaluateShapePolyline(*Shape, Outline, SectionFlattenBudget);
            if (Outline.empty())
                continue;

            const int Distinct = CountDistinctPoints(Outline);

            LoftSection Section;
            Section.Elevation  = Shape->Elevation;
            Section.ClosedRing = Shape->ClosedEnabled;
            if (Distinct <= 1)
            {
                // A section that collapses to one point is an apex (cone / dome end) — record it as a fan target, not a ring.
                Section.PointApex = true;
                Section.ApexPoint = Outline.front();
            }
            else
            {
                Section.Ring = ResampleRing(Outline, Shape->ClosedEnabled, CommonSampleCount);
                if (Shape->ClosedEnabled)
                    ++ClosedCount;
                else
                    ++OpenCount;
            }
            Sections.push_back(std::move(Section));
        }

        // An apex-only run, or a single ring, cannot span a surface.
        int RingCount = 0;
        for (const LoftSection& Section : Sections)
            if (!Section.PointApex)
                ++RingCount;
        if ((int)Sections.size() < 2 || RingCount == 0)
        {
            RaiseNotice(Store, "Loft needs at least two profiles");
            OutFailure = LoftOutcome::TooFewProfiles;
            return false;
        }
        if (ClosedCount > 0 && OpenCount > 0)
        {
            RaiseNotice(Store, "Loft needs sections all open or all closed");
            OutFailure = LoftOutcome::NeedsUniformClosure;
            return false;
        }

        // Correlate each ring against the previous ring so the surface does not twist. Apex sections carry no ring to correlate.
        const std::vector<ImVec2>* Previous = nullptr;
        for (LoftSection& Section : Sections)
        {
            if (Section.PointApex)
                continue;
            if (Previous)
                CorrelateSectionPoints(Section.Ring, *Previous);
            Previous = &Section.Ring;
        }

        // Diagnostic: report WHICH curves resolved + HOW they read (ring vs apex, closure, correlation choice) so a loft's inputs are visible.
        {
            char IdList[256];
            int  Written = 0;
            IdList[0] = '\0';
            for (uint32_t Identifier : SectionIds)
            {
                if (Written >= (int)sizeof(IdList) - 12)
                    break;
                Written += std::snprintf(IdList + Written, sizeof(IdList) - (size_t)Written,
                                         Written == 0 ? "#%u" : ",#%u", Identifier);
            }
            fprintf(stderr, "[loft] sections resolved: %d total (rings %d, apex %d), closure=%s, correlation=%s, profiles=[%s]\n",
                    (int)Sections.size(),
                    RingCount,
                    (int)Sections.size() - RingCount,
                    ClosedCount > 0 ? "closed" : "open",
                    Spec.Correlation == LoftCorrelationCategory::Connectors ? "connectors" : "automatic",
                    IdList);
        }

        OutSections = std::move(Sections);
        return true;
    }

    // 📝 Resolve the u-count (ring resolution) shared by every non-apex section. Apex sections adopt this same count (their fan target is a
    //    single point repeated), so the (u,v) grid stays rectangular and side-wall quads stitch uniformly.
    int ResolveRingResolution(const std::vector<LoftSection>& Sections)
    {
        for (const LoftSection& Section : Sections)
            if (!Section.PointApex)
                return (int)Section.Ring.size();
        return 0;
    }

    // 📝 Sample section S at ring index U as a 3D point. An apex section returns its single point for every U (the fan target); a ring
    //    section returns its correlated ring point lifted to the section's Z plane.
    LoftVertex SampleSection(const LoftSection& Section, int U, int RingResolution)
    {
        LoftVertex Vertex;
        if (Section.PointApex)
        {
            Vertex.X = Section.ApexPoint.x;
            Vertex.Y = Section.ApexPoint.y;
            Vertex.Z = Section.Elevation;
            return Vertex;
        }
        const int Count = (int)Section.Ring.size();
        const int Index = Count > 0 ? (U % Count) : 0;
        Vertex.X = Section.Ring[Index].x;
        Vertex.Y = Section.Ring[Index].y;
        Vertex.Z = Section.Elevation;
        return Vertex;
    }
}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       SURFACE INTERPOLATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Build the interpolated (u,v) vertex grid for the body and stitch it into side-wall triangles. Each ROW is one v-ring (a section,
    //    plus SmoothStepsPerSpan interpolated rings between adjacent sections when Transition is Smooth). Each COLUMN is one u sample around
    //    the ring. Adjacent rows × adjacent columns form quads split into two triangles. ClosedLoopEnabled wraps the last section back to the
    //    first; a closed RING wraps the u seam. GuideCurves / Centerline reshaping is the P4 tier (the rows here are the plain section blend).
    void InterpolateLoftSurface(ParametricSketchLoftBody&                Body,
                                const std::vector<LoftSection>& Sections,
                                const LoftSpecification&        Spec,
                                int                             RingResolution)
    {
        const int SectionCount = (int)Sections.size();
        if (SectionCount < 2 || RingResolution < 2)
            return;

        bool RingClosed = false;
        for (const LoftSection& Section : Sections)
            if (!Section.PointApex)
            {
                RingClosed = Section.ClosedRing;
                break;
            }
        const bool LoopClosed = Spec.ClosedLoopEnabled;

        // Assemble the ordered v-rows. For Ruled each section is one row; for Smooth we insert interpolated rows between sections using a
        //    Catmull-Rom blend of the four surrounding sections (clamped at the ends), so the body bulges continuously.
        std::vector<std::vector<LoftVertex>> Rows;

        const int SpanCount = LoopClosed ? SectionCount : SectionCount - 1;
        for (int Span = 0; Span < SpanCount; ++Span)
        {
            const int I1 = Span;
            const int I2 = (Span + 1) % SectionCount;

            std::vector<LoftVertex> RowStart;
            RowStart.reserve(RingResolution);
            for (int U = 0; U < RingResolution; ++U)
                RowStart.push_back(SampleSection(Sections[I1], U, RingResolution));
            Rows.push_back(std::move(RowStart));

            if (Spec.Transition == LoftTransitionCategory::Smooth)
            {
                const int I0 = (I1 - 1 + SectionCount) % SectionCount;
                const int I3 = (I2 + 1) % SectionCount;
                const int Prev = LoopClosed ? I0 : (I1 > 0 ? I0 : I1);
                const int Next = LoopClosed ? I3 : (I2 < SectionCount - 1 ? I3 : I2);
                for (int Step = 1; Step < SmoothStepsPerSpan; ++Step)
                {
                    const float T = (float)Step / (float)SmoothStepsPerSpan;
                    std::vector<LoftVertex> Between;
                    Between.reserve(RingResolution);
                    for (int U = 0; U < RingResolution; ++U)
                    {
                        const LoftVertex A = SampleSection(Sections[Prev], U, RingResolution);
                        const LoftVertex B = SampleSection(Sections[I1],  U, RingResolution);
                        const LoftVertex C = SampleSection(Sections[I2],  U, RingResolution);
                        const LoftVertex D = SampleSection(Sections[Next], U, RingResolution);
                        LoftVertex V;
                        V.X = EvaluateCatmullRom(A.X, B.X, C.X, D.X, T);
                        V.Y = EvaluateCatmullRom(A.Y, B.Y, C.Y, D.Y, T);
                        V.Z = EvaluateCatmullRom(A.Z, B.Z, C.Z, D.Z, T);
                        Between.push_back(V);
                    }
                    Rows.push_back(std::move(Between));
                }
            }
        }
        // Close the v-run by appending the final section as the last row (open loft only; a closed loop already wrapped above).
        if (!LoopClosed)
        {
            std::vector<LoftVertex> RowEnd;
            RowEnd.reserve(RingResolution);
            for (int U = 0; U < RingResolution; ++U)
                RowEnd.push_back(SampleSection(Sections[SectionCount - 1], U, RingResolution));
            Rows.push_back(std::move(RowEnd));
        }

        // Push every row's vertices, remembering each row's base index so the quad stitch can address them.
        const int RowCount = (int)Rows.size();
        std::vector<uint32_t> RowBase(RowCount, 0);
        for (int Row = 0; Row < RowCount; ++Row)
        {
            RowBase[Row] = (uint32_t)(Body.Positions.size() / 3);
            for (const LoftVertex& Vertex : Rows[Row])
                PushVertex(Body, Vertex.X, Vertex.Y, Vertex.Z);
        }

        // Stitch adjacent rows into side-wall quads (two triangles each). The u seam wraps only when the ring is closed.
        const int URange = RingClosed ? RingResolution : RingResolution - 1;
        const int VRange = LoopClosed ? RowCount : RowCount - 1;
        for (int Row = 0; Row < VRange; ++Row)
        {
            const uint32_t BaseA = RowBase[Row];
            const uint32_t BaseB = RowBase[(Row + 1) % RowCount];
            for (int U = 0; U < URange; ++U)
            {
                const int UNext = (U + 1) % RingResolution;
                const uint32_t A = BaseA + U;
                const uint32_t B = BaseA + UNext;
                const uint32_t C = BaseB + U;
                const uint32_t D = BaseB + UNext;
                AppendTriangle(Body, A, C, B);
                AppendTriangle(Body, B, C, D);
            }
        }
    }

    // 📝 Fill the loft body's geometry for a resolved section run: side walls (InterpolateLoftSurface) + end caps when the sections are
    //    closed (a watertight solid) and the loft is not a closed loop (a ring has no ends to cap). Bumps TessellationRevision + normalizes.
    void GenerateLoftGeometry(ParametricSketchLoftBody&                Body,
                              const std::vector<LoftSection>& Sections,
                              const LoftSpecification&        Spec)
    {
        Body.Positions.clear();
        Body.Normals.clear();
        Body.Indices.clear();

        const int RingResolution = ResolveRingResolution(Sections);
        if (RingResolution >= 2)
            InterpolateLoftSurface(Body, Sections, Spec, RingResolution);

        // Closed sections cap into a solid; open sections stay a double-sided sheet. A closed LOOP (ring) has no free ends to cap.
        const bool SectionsClosed = Sections.front().ClosedRing;
        Body.SolidEnabled = SectionsClosed && !Spec.ClosedLoopEnabled;
        if (Body.SolidEnabled)
        {
            const LoftSection& First = Sections.front();
            const LoftSection& Last  = Sections.back();
            if (!First.PointApex)
                AppendCap(Body, First.Ring, First.Elevation);
            if (!Last.PointApex)
                AppendCap(Body, Last.Ring, Last.Elevation);
        }

        NormalizeBodyNormals(Body);
        ++Body.TessellationRevision;

        // Diagnostic: report the interpolation CHOICE + the tessellated output size so the loft algorithm + result are visible in the log.
        fprintf(stderr, "[loft] geometry: transition=%s, loop=%s, u-count=%d, %s -> %zu verts / %zu tris (rev %u)\n",
                Spec.Transition == LoftTransitionCategory::Smooth ? "smooth(catmull-rom)" : "ruled(linear)",
                Spec.ClosedLoopEnabled ? "closed" : "open",
                RingResolution,
                Body.SolidEnabled ? "capped solid" : "double-sided sheet",
                Body.Positions.size() / 3,
                Body.Indices.size() / 3,
                Body.TessellationRevision);
    }
}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        FREE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LoftOutcome AppendLoftResult(ParametricSketchShapeStore& Store, const LoftSpecification& Spec)
{
    fprintf(stderr, "[loft] AppendLoftResult: %d requested profiles, %d guides, centerline=%s\n",
            (int)Spec.SectionProfiles.size(),
            (int)Spec.GuideCurves.size(),
            Spec.Centerline != 0 ? "yes" : "none");

    std::vector<LoftSection> Sections;
    LoftOutcome              Failure = LoftOutcome::EmptyResult;
    if (!ResolveLoftSections(Store, Spec, Sections, Failure))
    {
        fprintf(stderr, "[loft] aborted at section resolution (outcome %d)\n", (int)Failure);
        return Failure;
    }

    // Inherit tint / folder from the FIRST resolved section (the base), mirroring the offset / boolean convention.
    uint32_t BaseTint   = 0;
    uint32_t BaseFolder = 0;
    {
        std::vector<uint32_t> SectionIds = Spec.SectionProfiles;
        if (SectionIds.empty())
        {
            SectionIds = Store.SelectionSet;
            if (SectionIds.empty() && Store.Selected != 0)
                SectionIds.push_back(Store.Selected);
        }
        for (uint32_t Identifier : SectionIds)
            if (ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier))
            {
                BaseTint   = Shape->TintIndex;
                BaseFolder = Shape->FolderIdentifier;
                break;
            }
    }

    ParametricSketchLoftBody Body;
    Body.Identifier       = Store.NextLoftIdentifier++;
    Body.Recipe           = Spec;
    // Persist the RESOLVED section ids into the recipe so reflow re-solves from the same sources even when the caller passed an empty list.
    if (Body.Recipe.SectionProfiles.empty())
    {
        Body.Recipe.SectionProfiles = Store.SelectionSet;
        if (Body.Recipe.SectionProfiles.empty() && Store.Selected != 0)
            Body.Recipe.SectionProfiles.push_back(Store.Selected);
    }
    Body.TintIndex        = BaseTint;
    Body.FolderIdentifier = BaseFolder;
    Body.Displayed        = true;
    std::snprintf(Body.Title, sizeof(Body.Title), "Loft %u", Body.Identifier);

    GenerateLoftGeometry(Body, Sections, Body.Recipe);
    if (Body.Indices.empty())
    {
        RaiseNotice(Store, "Loft produced no surface");
        // Roll the id source back so a failed loft does not burn an identifier.
        --Store.NextLoftIdentifier;
        return LoftOutcome::EmptyResult;
    }

    const uint32_t NewIdentifier = Body.Identifier;
    fprintf(stderr, "[loft] committed Loft %u (%d bodies now on the store)\n", NewIdentifier, (int)Store.LoftBodies.size() + 1);
    Store.LoftBodies.push_back(std::move(Body));
    Store.SelectedLoft = NewIdentifier;

    char LogLabel[64];
    std::snprintf(LogLabel, sizeof(LogLabel), "Added Loft %u", NewIdentifier);
    AppendParametricSketchEdit(Store, 0, LogLabel, "spline");
    return LoftOutcome::Committed;
}

void ReflowLoftBodies(ParametricSketchShapeStore& Store)
{
    if (Store.LoftBodies.empty())
        return;

    fprintf(stderr, "[loft] reflow: re-solving %d body(ies) from live sources\n", (int)Store.LoftBodies.size());

    // Re-solve each body in place from its live sections; erase a body whose sections dropped below two survivors (the loft is gone).
    for (size_t Index = 0; Index < Store.LoftBodies.size();)
    {
        ParametricSketchLoftBody& Body = Store.LoftBodies[Index];

        std::vector<LoftSection> Sections;
        LoftOutcome              Failure = LoftOutcome::EmptyResult;
        LoftSpecification        Recipe  = Body.Recipe;   // resolve strictly from the recipe's own section ids (never the live selection)

        // ResolveLoftSections falls back to the selection only when the recipe list is empty; a reflow must not, so guard an empty list.
        if (Recipe.SectionProfiles.size() < 2)
        {
            Store.LoftBodies.erase(Store.LoftBodies.begin() + Index);
            continue;
        }

        if (!ResolveLoftSections(Store, Recipe, Sections, Failure))
        {
            Store.LoftBodies.erase(Store.LoftBodies.begin() + Index);
            continue;
        }

        GenerateLoftGeometry(Body, Sections, Recipe);
        if (Body.Indices.empty())
        {
            Store.LoftBodies.erase(Store.LoftBodies.begin() + Index);
            continue;
        }
        ++Index;
    }

    // Drop a dangling loft selection when its body was erased.
    if (Store.SelectedLoft != 0 && !ResolveLoftBody(Store, Store.SelectedLoft))
        Store.SelectedLoft = 0;
}

ParametricSketchLoftBody* ResolveLoftBody(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return nullptr;
    for (ParametricSketchLoftBody& Body : Store.LoftBodies)
        if (Body.Identifier == Identifier)
            return &Body;
    return nullptr;
}

void DetachLoftBody(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    for (size_t Index = 0; Index < Store.LoftBodies.size(); ++Index)
        if (Store.LoftBodies[Index].Identifier == Identifier)
        {
            Store.LoftBodies.erase(Store.LoftBodies.begin() + Index);
            if (Store.SelectedLoft == Identifier)
                Store.SelectedLoft = 0;
            char LogLabel[64];
            std::snprintf(LogLabel, sizeof(LogLabel), "Detached Loft %u", Identifier);
            AppendParametricSketchEdit(Store, 0, LogLabel, "trash-2");
            return;
        }
}

} // namespace Frontier
