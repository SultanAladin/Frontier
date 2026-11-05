//============================================================================================================================================
//                                                            DIHEDRALEDGES.JS
//============================================================================================================================================
// 🧩 Feature-edge wireframe extractor matching THREE.EdgesGeometry(geo, thresholdAngle) on the ControlVehicle facet sets:
//    14° dihedral threshold on body/glass facets and 28° dihedral threshold on wheel tyre/rim facets.

import { CourseMaterial } from "../MaterialSpecification.js";

export const BodyEdgeThresholdDegrees  = 14.0;   // reference Truck.tsx BodyPart EdgesGeometry(..., 14)
export const WheelEdgeThresholdDegrees = 28.0;   // reference Truck.tsx WheelAssembly EdgesGeometry(..., 28)

// 📝 Each emitted edge record packs 8 floats (32 B):
//    [Ax, Ay, Az, PackedMetaA, Bx, By, Bz, PackedMetaB]
//    where PackedMeta = (OccupantSlot & 0xFF) | ((Family & 0xFFFF) << 8) so the vertex shader can deform wheel & cabin
//    edges with the exact same ResolveWheelVertex / DeformBodyVertex path as SurfaceRaster.wgsl.
export const EdgeRecordFloats = 8;
export const EdgeRecordBytes  = 32;

function ComputeFacetNormal(Corners)
{
    const [A, B, C] = Corners;
    const Ex = B[0] - A[0], Ey = B[1] - A[1], Ez = B[2] - A[2];
    const Fx = C[0] - A[0], Fy = C[1] - A[1], Fz = C[2] - A[2];
    const Nx = Ey * Fz - Ez * Fy;
    const Ny = Ez * Fx - Ex * Fz;
    const Nz = Ex * Fy - Ey * Fx;
    const Length = Math.hypot(Nx, Ny, Nz);
    return Length > 1.0e-8 ? [Nx / Length, Ny / Length, Nz / Length] : [0.0, 0.0, 1.0];
}

function QuantizeVertex(Point)
{
    return `${Math.round(Point[0] * 2000)},${Math.round(Point[1] * 2000)},${Math.round(Point[2] * 2000)}`;
}

export function ExtractDihedralEdges(Facets, ThresholdDegrees, SkipFamilies = null)
{
    const ThresholdDot = Math.cos((ThresholdDegrees * Math.PI) / 180.0);
    const EdgeMap = new Map();

    for (const Facet of Facets)
    {
        if (SkipFamilies && SkipFamilies.has(Facet.Family))
            continue;
        const Corners = Facet.Corners;
        const Normal = ComputeFacetNormal(Corners);
        for (let EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const A = Corners[EdgeIndex];
            const B = Corners[(EdgeIndex + 1) % 3];
            const KeyA = QuantizeVertex(A);
            const KeyB = QuantizeVertex(B);
            if (KeyA === KeyB)
                continue;
            const Key = KeyA < KeyB ? `${KeyA}|${KeyB}` : `${KeyB}|${KeyA}`;
            const Existing = EdgeMap.get(Key);
            if (!Existing)
            {
                EdgeMap.set(Key, { A, B, Normal0: Normal, Normal1: null, Family: Facet.Family >>> 0 });
            }
            else if (!Existing.Normal1)
            {
                Existing.Normal1 = Normal;
            }
        }
    }

    const Selected = [];
    for (const Entry of EdgeMap.values())
    {
        if (!Entry.Normal1)
        {
            Selected.push(Entry);
            continue;
        }
        const Dot = Entry.Normal0[0] * Entry.Normal1[0]
                  + Entry.Normal0[1] * Entry.Normal1[1]
                  + Entry.Normal0[2] * Entry.Normal1[2];
        if (Dot <= ThresholdDot)
            Selected.push(Entry);
    }
    return Selected;
}

export function BuildVehicleEdgeBuffer(Scene)
{
    const BodySetIndex = Scene.VehicleBodySet;
    const WheelOccupantIndex = Scene.VehicleOccupants.Wheels[0];
    const WheelSetIndex = Scene.Occupants[WheelOccupantIndex].FacetSet;

    // Lamps remain unpatched/solid in reference Truck.tsx; interior steering/screen widgets skip exterior wireframe clutter.
    const SkipBody = new Set([CourseMaterial.Headlight, CourseMaterial.Taillight]);
    const BodyEdges  = ExtractDihedralEdges(Scene.FacetSets[BodySetIndex].Facets, BodyEdgeThresholdDegrees, SkipBody);
    const WheelEdges = ExtractDihedralEdges(Scene.FacetSets[WheelSetIndex].Facets, WheelEdgeThresholdDegrees, null);

    const TotalEdges = BodyEdges.length + 4 * WheelEdges.length;
    const Buffer = new ArrayBuffer(TotalEdges * EdgeRecordBytes);
    const Scalars = new Float32Array(Buffer);
    const Words   = new Uint32Array(Buffer);

    let Cursor = 0;
    const WriteEdge = (Edge, OccupantSlot) =>
    {
        const PackedMeta = ((OccupantSlot & 0xFF) | ((Edge.Family & 0xFFFF) << 8)) >>> 0;
        const Base = Cursor * EdgeRecordFloats;
        Scalars[Base + 0] = Edge.A[0];
        Scalars[Base + 1] = Edge.A[1];
        Scalars[Base + 2] = Edge.A[2];
        Words[Base + 3]   = PackedMeta;
        Scalars[Base + 4] = Edge.B[0];
        Scalars[Base + 5] = Edge.B[1];
        Scalars[Base + 6] = Edge.B[2];
        Words[Base + 7]   = PackedMeta;
        Cursor++;
    };

    for (const Edge of BodyEdges)
        WriteEdge(Edge, 0);
    for (let Wheel = 0; Wheel < 4; ++Wheel)
        for (const Edge of WheelEdges)
            WriteEdge(Edge, 1 + Wheel);

    return {
        Bytes: Buffer,
        Scalars,
        EdgeCount: TotalEdges,
        BodyEdgeCount: BodyEdges.length,
        WheelEdgeCount: WheelEdges.length
    };
}
