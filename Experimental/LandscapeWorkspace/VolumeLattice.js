/*==========================================================================================================================================
                                                              VOLUMELATTICE.JS
==========================================================================================================================================*/
// 🧩 Per-region XYZ cell counts and metre-scale sampling shared by authoring, working detail and sparse export.

import { LayerVerticalBounds } from './LandformStack.js';

export function ValidateLatticeDimensions(Dimensions)
{
    if (!Array.isArray(Dimensions) || Dimensions.length !== 3 || Dimensions.some(Count =>
        !Number.isInteger(Count) || Count < 8 || Count > 4096 || Count % 8))
        throw Error('Landscape resolution must contain X, Y and Z cell counts, multiples of eight from 8 to 4096.');
    return Dimensions;
}

export function ResolveLatticeDimensions(Layer, Region)
{
    return [...ValidateLatticeDimensions(Region?.Resolution || [Layer.Resolution, Layer.Vertical, Layer.Resolution])];
}

export function ConstructVolumeLattice(Layer, Region)
{
    const Dimensions = ResolveLatticeDimensions(Layer, Region);
    const [MinimumY, MaximumY] = LayerVerticalBounds(Layer, Region);
    const Minimum = [Region.X0, MinimumY, Region.Z0], Maximum = [Region.X1, MaximumY, Region.Z1];
    const Pitch = Dimensions.map((Count, Axis) => (Maximum[Axis] - Minimum[Axis]) / Count);
    if (Pitch.some(Spacing => !Number.isFinite(Spacing) || Spacing <= 0)) throw Error('Invalid landscape lattice extent.');
    return { Dimensions, Minimum, Maximum, Pitch, Cells: Dimensions.reduce((Product, Count) => Product * Count, 1) };
}
