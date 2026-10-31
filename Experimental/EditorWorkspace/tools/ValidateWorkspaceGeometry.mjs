/*====================================================================================================================================
                                              VALIDATEWORKSPACEGEOMETRY.MJS
====================================================================================================================================*/
// 🧩 Headless checks for placement algebra, projection recovery, and procedural surface construction used by the workspace viewport.

import {
    MatrixIdentity,
    MatrixInverse,
    MatrixModelPlacement,
    MatrixMultiply,
    MatrixNormalRecovery,
    MatrixOrthographic,
    MatrixPerspective,
    MatrixViewOrientation,
    ProjectPoint,
    RayFromScreen,
    VecCross,
    VecDot,
    VecNormalize,
    VecSubtract
} from "../scripts/SpatialMathematics.js";

import { ConstructPrimitiveSurface, PrimitiveCategory } from "../scripts/PolygonSurfaceConstruction.js";

let FailureTotal = 0;
let CheckTotal = 0;

function AssertCriterion(Label, Condition, Detail = "")
{
    CheckTotal += 1;
    if (!Condition)
    {
        FailureTotal += 1;
        console.error(`FAIL  ${Label} ${Detail}`);
        return;
    }
    console.log(`PASS  ${Label} ${Detail}`);
}

function AssertClose(Label, Actual, Expected, Tolerance = 1.0e-6)
{
    AssertCriterion(Label, Math.abs(Actual - Expected) <= Tolerance, `actual=${Actual.toFixed(6)} expected=${Expected.toFixed(6)}`);
}

function MatrixClose(Left, Right, Tolerance = 1.0e-6)
{
    for (let Index = 0; Index < 16; Index += 1)
    {
        if (Math.abs(Left[Index] - Right[Index]) > Tolerance)
        {
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATRIX ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

const IdentitySample = MatrixIdentity();
AssertCriterion("identity matrix is stable", IdentitySample[0] === 1.0 && IdentitySample[15] === 1.0 && IdentitySample[1] === 0.0);

const RotationPlacement90 = MatrixModelPlacement([ 0, 0, 0 ], [ 0, 90, 0 ], [ 1, 1, 1 ]);
const RotatedAxis = ProjectPoint(RotationPlacement90, [ 1, 0, 0 ]);
AssertClose("90 degree Y rotation maps +X onto -Z (x)", RotatedAxis[0], 0.0);
AssertClose("90 degree Y rotation maps +X onto -Z (z)", RotatedAxis[2], -1.0);

const ScaledPlacement = MatrixModelPlacement([ 2, -1, 0.5 ], [ 0, 0, 0 ], [ 2, 3, 4 ]);
const ScaledPoint = ProjectPoint(ScaledPlacement, [ 1, 1, 1 ]);
AssertClose("placement scale x", ScaledPoint[0], 4.0);
AssertClose("placement scale y", ScaledPoint[1], 2.0);
AssertClose("placement scale z", ScaledPoint[2], 4.5);

const ArbitraryMatrix = MatrixMultiply(
    MatrixMultiply(MatrixModelPlacement([ 0.3, -2.0, 1.2 ], [ 22, -47, 61 ], [ 1.4, 0.8, 2.2 ]), MatrixPerspective(0.9, 1.7, 0.1, 60.0)),
    MatrixViewOrientation([ 3, 2, 5 ], [ 0, 0, 0 ], [ 0, 1, 0 ])
);
const InvertedArbitrary = MatrixInverse(ArbitraryMatrix);
const RecoveredProduct = MatrixMultiply(ArbitraryMatrix, InvertedArbitrary);
AssertCriterion("4x4 inverse recovers identity", MatrixClose(RecoveredProduct, MatrixIdentity(), 1.0e-4));

const TangentDirection = VecNormalize([ 0.4, -0.6, 0.7 ]);
const SurfaceNormal = VecNormalize(VecCross(TangentDirection, [ 0.2, 0.9, -0.3 ]));
const NonUniformPlacement = MatrixModelPlacement([ 0, 0, 0 ], [ 12, -35, 8 ], [ 2.5, 0.4, 1.7 ]);

function TransformDirection(Matrix, Vector)
{
    return [
        Matrix[0] * Vector[0] + Matrix[3] * Vector[1] + Matrix[6] * Vector[2],
        Matrix[1] * Vector[0] + Matrix[4] * Vector[1] + Matrix[7] * Vector[2],
        Matrix[2] * Vector[0] + Matrix[5] * Vector[1] + Matrix[8] * Vector[2]
    ];
}

const TransformedTangent = VecNormalize(TransformDirection(NonUniformPlacement, TangentDirection));
const TransformedNormal = VecNormalize(TransformDirection(MatrixNormalRecovery(NonUniformPlacement), SurfaceNormal));
AssertClose("normal recovery keeps normals perpendicular to tangents", VecDot(TransformedTangent, TransformedNormal), 0.0, 1.0e-5);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROJECTION AND RAY RECOVERY
//------------------------------------------------------------------------------------------------------------------------

const OrthographicMatrix = MatrixOrthographic(2.0, 1.0, 0.1, 100.0);
const OrthographicCentre = ProjectPoint(OrthographicMatrix, [ 1.0, 0.5, 0.0 ]);
AssertClose("orthographic projection maps half extent to NDC x", OrthographicCentre[0], 0.5);
AssertClose("orthographic projection maps half extent to NDC y", OrthographicCentre[1], 0.5);
AssertClose("orthographic projection is distance independent", ProjectPoint(OrthographicMatrix, [ 1.0, 0.5, -60.0 ])[0], 0.5);

const EyePosition = [ 0.0, 1.4, 7.0 ];
const LookTarget = [ 0.0, 0.0, 0.0 ];
const ViewMatrix = MatrixViewOrientation(EyePosition, LookTarget, [ 0.0, 1.0, 0.0 ]);
const ProjectionMatrix = MatrixPerspective(34.0 * Math.PI / 180.0, 16.0 / 9.0, 0.08, 90.0);
const ViewProjection = MatrixMultiply(ProjectionMatrix, ViewMatrix);

const CenterRay = RayFromScreen(ViewProjection, 800.0, 450.0, 1600.0, 900.0);
const ExpectedForward = VecNormalize(VecSubtract(LookTarget, EyePosition));
AssertClose("centre ray aligns with view forward (x)", CenterRay.Direction[0], ExpectedForward[0], 1.0e-4);
AssertClose("centre ray aligns with view forward (y)", CenterRay.Direction[1], ExpectedForward[1], 1.0e-4);
AssertClose("centre ray aligns with view forward (z)", CenterRay.Direction[2], ExpectedForward[2], 1.0e-4);

const TargetWorldPoint = [ 0.6, 0.35, -0.2 ];
const TargetProjection = ProjectPoint(ViewProjection, TargetWorldPoint);
const TargetScreenX = (TargetProjection[0] * 0.5 + 0.5) * 1600.0;
const TargetScreenY = (-TargetProjection[1] * 0.5 + 0.5) * 900.0;
const RecoveredRay = RayFromScreen(ViewProjection, TargetScreenX, TargetScreenY, 1600.0, 900.0);
const ToTargetPoint = VecSubtract(TargetWorldPoint, RecoveredRay.Origin);
const AlongRecovered = VecDot(ToTargetPoint, RecoveredRay.Direction);
const MissDistance = Math.hypot(
    ToTargetPoint[0] - RecoveredRay.Direction[0] * AlongRecovered,
    ToTargetPoint[1] - RecoveredRay.Direction[1] * AlongRecovered,
    ToTargetPoint[2] - RecoveredRay.Direction[2] * AlongRecovered
);
AssertCriterion("unprojected centre ray passes through the projected point", MissDistance < 1.0e-4, `miss=${MissDistance.toExponential(2)}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROCEDURAL SURFACES
//------------------------------------------------------------------------------------------------------------------------

for (const PrimitiveIdentity of [ "knot", "sphere", "torus", "cube", "cylinder", "halo" ])
{
    const Surface = ConstructPrimitiveSurface(PrimitiveIdentity);
    const VertexTotal = Surface.Positions.length / 3;

    AssertCriterion(`${PrimitiveIdentity}: geometry populated`, Surface.TriangleCount > 0 && VertexTotal === Surface.TriangleCount * 3,
        `triangles=${Surface.TriangleCount} vertices=${VertexTotal}`);
    AssertCriterion(`${PrimitiveIdentity}: labelled`, typeof PrimitiveCategory[PrimitiveIdentity] === "string");

    let MaximumNormalError = 0.0;
    let NonFiniteTotal = 0;
    let BarycentricErrorTotal = 0;

    for (let Index = 0; Index < Surface.Normals.length; Index += 3)
    {
        const NormalLength = Math.hypot(Surface.Normals[Index], Surface.Normals[Index + 1], Surface.Normals[Index + 2]);
        MaximumNormalError = Math.max(MaximumNormalError, Math.abs(NormalLength - 1.0));

        for (let Axis = 0; Axis < 3; Axis += 1)
        {
            if (!Number.isFinite(Surface.Positions[Index + Axis]) || !Number.isFinite(Surface.Normals[Index + Axis]))
            {
                NonFiniteTotal += 1;
            }
        }

        const TriangleVertex = (Index / 3) % 3;
        const BarycentricSum = Surface.Barycentrics[Index] + Surface.Barycentrics[Index + 1] + Surface.Barycentrics[Index + 2];
        if (Math.abs(BarycentricSum - 1.0) > 1.0e-6 || Math.max(Surface.Barycentrics[Index], Surface.Barycentrics[Index + 1], Surface.Barycentrics[Index + 2]) !== 1.0)
        {
            BarycentricErrorTotal += 1;
        }
    }

    AssertCriterion(`${PrimitiveIdentity}: normals are unit length`, MaximumNormalError < 1.0e-5, `max error=${MaximumNormalError.toExponential(2)}`);
    AssertCriterion(`${PrimitiveIdentity}: no non-finite samples`, NonFiniteTotal === 0);
    AssertCriterion(`${PrimitiveIdentity}: barycentric wireframe corners valid`, BarycentricErrorTotal === 0);

    // 📝 A corrected winding must flip whole corners, keeping one basis vector per triangle corner.
    let PermutationErrorTotal = 0;
    for (let Triangle = 0; Triangle < Surface.TriangleCount; Triangle += 1)
    {
        const Basis = [ 0, 1, 2 ].map((Corner) =>
        {
            const Offset = Triangle * 9 + Corner * 3;
            return `${Surface.Barycentrics[Offset]},${Surface.Barycentrics[Offset + 1]},${Surface.Barycentrics[Offset + 2]}`;
        }).sort().join("|");
        if (Basis !== "0,0,1|0,1,0|1,0,0")
        {
            PermutationErrorTotal += 1;
        }
    }
    AssertCriterion(`${PrimitiveIdentity}: every triangle keeps one basis vector per corner`, PermutationErrorTotal === 0);

    const BoundsValid = Surface.MinimumBoundary.every(Number.isFinite)
        && Surface.MaximumBoundary.every(Number.isFinite)
        && Surface.MaximumBoundary.every((Component, Axis) => Component > Surface.MinimumBoundary[Axis]);
    const Extent = Surface.MaximumBoundary.map((Component, Axis) => Component - Surface.MinimumBoundary[Axis]);
    AssertCriterion(`${PrimitiveIdentity}: bounds are finite and non-degenerate`, BoundsValid,
        `extent=${Extent.map((Component) => Component.toFixed(2)).join(" × ")}`);
}

// 📝 Confirms the default camera framing keeps the hero record inside the stage at roughly the reference scale.
const HeroSurface = ConstructPrimitiveSurface("knot");
const HeroPlacement = MatrixModelPlacement([ 0.0, 0.55, 0.0 ], [ 0.0, 0.0, 0.0 ], [ 1.0, 1.0, 1.0 ]);
const HeroCentre = [ 0.0, 0.55, 0.0 ];
const HeroDistance = Math.max(HeroSurface.MaximumBoundary[1] - HeroSurface.MinimumBoundary[1],
    HeroSurface.MaximumBoundary[0] - HeroSurface.MinimumBoundary[0]) * 2.72;
const HeroPitch = 0.24, HeroYaw = 0.68;
const HeroEye = [
    HeroCentre[0] + HeroDistance * Math.cos(HeroPitch) * Math.sin(HeroYaw),
    HeroCentre[1] + HeroDistance * Math.sin(HeroPitch),
    HeroCentre[2] + HeroDistance * Math.cos(HeroPitch) * Math.cos(HeroYaw)
];
const HeroView = MatrixViewOrientation(HeroEye, HeroCentre, [ 0.0, 1.0, 0.0 ]);
const HeroProjection = MatrixPerspective(34.0 * Math.PI / 180.0, 1.09, 0.08, 90.0);
const HeroViewProjection = MatrixMultiply(HeroProjection, HeroView);

let HeroMinimumY = Infinity, HeroMaximumY = -Infinity, HeroVisible = true;
for (let Corner = 0; Corner < 8; Corner += 1)
{
    const LocalCorner = [
        (Corner & 1) ? HeroSurface.MaximumBoundary[0] : HeroSurface.MinimumBoundary[0],
        (Corner & 2) ? HeroSurface.MaximumBoundary[1] : HeroSurface.MinimumBoundary[1],
        (Corner & 4) ? HeroSurface.MaximumBoundary[2] : HeroSurface.MinimumBoundary[2]
    ];
    const Projected = ProjectPoint(HeroViewProjection, ProjectPoint(HeroPlacement, LocalCorner).slice(0, 3));
    if (Projected[3] <= 0.0) { HeroVisible = false; break; }
    HeroMinimumY = Math.min(HeroMinimumY, Projected[1]);
    HeroMaximumY = Math.max(HeroMaximumY, Projected[1]);
}
const HeroHeightFraction = (HeroMaximumY - HeroMinimumY) / 2.0;
AssertCriterion("hero framing keeps the record on screen", HeroVisible && HeroMinimumY > -1.02 && HeroMaximumY < 1.02,
    `min=${HeroMinimumY.toFixed(2)} max=${HeroMaximumY.toFixed(2)}`);
AssertCriterion("hero framing fills most of the stage without colliding with the chrome", HeroHeightFraction > 0.66 && HeroHeightFraction < 0.84,
    `height=${HeroHeightFraction.toFixed(2)} distance=${HeroDistance.toFixed(2)}`);

const KnotSurface = ConstructPrimitiveSurface("knot");
AssertCriterion("knot stays inside the reference frame", Math.max(...KnotSurface.MaximumBoundary.map(Math.abs)) < 2.2,
    `max=${Math.max(...KnotSurface.MaximumBoundary.map(Math.abs)).toFixed(3)}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    REPORT
//------------------------------------------------------------------------------------------------------------------------

console.log("");
console.log(`Checks executed: ${CheckTotal}`);
console.log(`Failures: ${FailureTotal}`);
if (FailureTotal > 0)
{
    process.exitCode = 1;
}
