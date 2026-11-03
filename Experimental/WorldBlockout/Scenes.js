'use strict';
//----------------------------------------------------------------------------------------------------------------------------
//                                            Frontier · World Blockout · Scenes
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: every drawing the blockout editor shows, as a string of SVG. One module, two consumers — the page
//    drops the strings into the DOM, and the evidence script writes them to disk and rasterises them, so a screenshot and a
//    validation run are literally the same drawing code.
//
//    Three scenes:
//      ① PLANET       an equirectangular map of the height field: band palette, hillshade, coast line, city pads.
//      ② OCEAN PLAN   the plan view of the adaptive ocean: every tile coloured by its cell size, the near footprint, the fan.
//      ③ SLICE        a cross-section through the near patch: sea bed, solved surface, and where the spectral field takes over.
//----------------------------------------------------------------------------------------------------------------------------

const Scenes = (function ()
{
    // 📝 Loaded as a script in the page and required in Node: one line covers both without a bundler.
    const RasterApi = typeof Raster !== 'undefined' ? Raster
        : (typeof require === 'function' ? require('./Raster.js') : null);

    const BANDS = {
        Abyss: { Fill: '#0b1620', Depth: '#0b1620' },
        Deep: { Fill: '#132c3a', Depth: '#132c3a' },
        Shelf: { Fill: '#1d4a58', Depth: '#1d4a58' },
        Coast: { Fill: '#d7c7a0', Depth: '#2a6273' },
        Plain: { Fill: '#7d9761', Depth: '#7d9761' },
        Highland: { Fill: '#a4967a', Depth: '#a4967a' },
        Peak: { Fill: '#dee3e6', Depth: '#dee3e6' }
    };

    const WATER_SHALLOW = '#2b6d7f';
    const WATER_DEEP = '#0a1420';
    const INK = '#0d1218';
    const PAPER = '#f2ede2';
    const ACCENT = '#e8a84a';
    const COOL = '#6fc7a8';

    const Escape = Text => String(Text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    PLANET MAP
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 One raster pass, one shading pass, then the vector overlay. The raster is the field; the overlay is the annotation.
    function RenderPlanetMap(Field, Options)
    {
        Options = Options || {};
        const Width = Options.Width ?? 960;
        const Height = Options.Height ?? 480;
        const RasterImage = RasterApi.CreateRaster(Width, Height, '#05080b');
        const Heights = new Float32Array(Width * Height);
        for (let Row = 0; Row < Height; ++Row)
        {
            const Lat = 90 - (Row + 0.5) / Height * 180;
            for (let Column = 0; Column < Width; ++Column)
            {
                const Lon = (Column + 0.5) / Width * 360 - 180;
                Heights[Row * Width + Column] = Field.Sample.Height(Lon, Lat);
            }
        }

        // 📝 Hillshade from the height gradient: light from the north-west, which is the convention every terrain map uses.
        const Light = Normalise(-0.55, -0.62, 0.55);
        for (let Row = 0; Row < Height; ++Row)
        {
            for (let Column = 0; Column < Width; ++Column)
            {
                const Index = Row * Width + Column;
                const Centre = Heights[Index];
                const Left = Heights[Row * Width + Math.max(0, Column - 1)];
                const Right = Heights[Row * Width + Math.min(Width - 1, Column + 1)];
                const Up = Heights[Math.max(0, Row - 1) * Width + Column];
                const Down = Heights[Math.min(Height - 1, Row + 1) * Width + Column];
                const CellKm = Math.max(1, Field.Sample.CellKm(90 - Row / Height * 180));
                const Scale = 1000 * CellKm;
                const Normal = Normalise(-(Right - Left) / (2 * Scale), -(Down - Up) / (2 * Scale), 1);
                const Shade = Math.max(0.45, Math.min(1.35, 0.72 + 0.62 * (Normal[0] * Light[0]
                    + Normal[1] * Light[1] + Normal[2] * Light[2])));
                const Band = Field.Sample.Band(Centre);
                const Tint = Centre < 0 && Options.DepthTint !== false
                    ? RasterApi.Mix(WATER_SHALLOW, WATER_DEEP, Math.min(1, Math.max(0, -Centre / 4200)))
                    : RasterApi.Colour(BANDS[Band]?.Fill ?? BANDS.Plain.Fill);
                // 📝 Half a million pixels: write the three bytes rather than calling through a helper for each one.
                const Offset = (Row * Width + Column) * 3;
                RasterImage.Pixels[Offset] = Tint[0] * Shade;
                RasterImage.Pixels[Offset + 1] = Tint[1] * Shade;
                RasterImage.Pixels[Offset + 2] = Tint[2] * Shade;
            }
        }

        const Parts = [];
        Parts.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" `
            + `viewBox="0 0 ${Width} ${Height}" font-family="'Inter',system-ui,sans-serif">`);
        Parts.push(`<image x="0" y="0" width="${Width}" height="${Height}" `
            + `href="${RasterApi.ToDataUrl(RasterApi.EncodePng(RasterImage))}"/>`);

        if (Options.Graticule !== false)
        {
            const Lines = [`<g stroke="#f2ede2" stroke-opacity="0.16" stroke-width="1" fill="none">`];
            for (let Lon = -180; Lon <= 180; Lon += 30)
            {
                const X = (Lon + 180) / 360 * Width;
                Lines.push(`<line x1="${X.toFixed(1)}" y1="0" x2="${X.toFixed(1)}" y2="${Height}"/>`);
            }
            for (let Lat = -60; Lat <= 60; Lat += 30)
            {
                const Y = (90 - Lat) / 180 * Height;
                Lines.push(`<line x1="0" y1="${Y.toFixed(1)}" x2="${Width}" y2="${Y.toFixed(1)}"/>`);
            }
            Lines.push(`<line x1="0" y1="${(Height / 2).toFixed(1)}" x2="${Width}" y2="${(Height / 2).toFixed(1)}" `
                + `stroke-opacity="0.34" stroke-dasharray="6 6"/>`);
            Lines.push('</g>');
            Parts.push(Lines.join(''));
        }

        if (Options.Cities !== false)
        {
            for (const City of Field.Cities)
            {
                const X = (City.Lon + 180) / 360 * Width;
                const Y = (90 - City.Lat) / 180 * Height;
                const Reach = Math.max(3, (City.RadiusKm / (2 * Math.PI * Field.Planet.RadiusKm)) * Width);
                Parts.push(`<g><circle cx="${X.toFixed(1)}" cy="${Y.toFixed(1)}" r="${Reach.toFixed(1)}" `
                    + `fill="none" stroke="${ACCENT}" stroke-width="1.2" stroke-opacity="0.9"/>`
                    + `<circle cx="${X.toFixed(1)}" cy="${Y.toFixed(1)}" r="2.2" fill="${ACCENT}"/>`
                    + `<text x="${(X + 6).toFixed(1)}" y="${(Y - 5).toFixed(1)}" fill="${PAPER}" font-size="11" `
                    + `fill-opacity="0.92">${Escape(City.Name)}</text></g>`);
            }
        }

        if (Options.Cursor && Field.Cursor)
        {
            const X = (Field.Cursor.Lon + 180) / 360 * Width;
            const Y = (90 - Field.Cursor.Lat) / 180 * Height;
            Parts.push(`<g stroke="${COOL}" stroke-width="1.4" fill="none" stroke-opacity="0.95">`
                + `<circle cx="${X.toFixed(1)}" cy="${Y.toFixed(1)}" r="7"/>`
                + `<line x1="${(X - 13).toFixed(1)}" y1="${Y.toFixed(1)}" x2="${(X - 3).toFixed(1)}" y2="${Y.toFixed(1)}"/>`
                + `<line x1="${(X + 3).toFixed(1)}" y1="${Y.toFixed(1)}" x2="${(X + 13).toFixed(1)}" y2="${Y.toFixed(1)}"/>`
                + `<line x1="${X.toFixed(1)}" y1="${(Y - 13).toFixed(1)}" x2="${X.toFixed(1)}" y2="${(Y - 3).toFixed(1)}"/>`
                + `<line x1="${X.toFixed(1)}" y1="${(Y + 3).toFixed(1)}" x2="${X.toFixed(1)}" y2="${(Y + 13).toFixed(1)}"/>`
                + `</g>`);
        }

        Parts.push(`<rect x="0.5" y="0.5" width="${Width - 1}" height="${Height - 1}" fill="none" `
            + `stroke="${PAPER}" stroke-opacity="0.25"/>`);
        Parts.push('</svg>');
        return Parts.join('');
    }

    function Normalise(X, Y, Z)
    {
        const Length = Math.max(1e-9, Math.hypot(X, Y, Z));
        return [X / Length, Y / Length, Z / Length];
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    OCEAN PLAN
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The resolution map: every tile painted by how coarse it is, in discrete bands so the quadtree reads at a glance,
    //    with the near footprint magnified beside it. This is the figure that answers "no wasted resolution" without words.
    const LEVEL_COLOURS = ['#96e6c3', '#6fc7a8', '#4fa3ac', '#3d80ad', '#3a6096', '#39476f', '#332f47'];

    function LevelColour(CellKm, NearCellKm)
    {
        const Level = Math.round(Math.log2(Math.max(1e-5, CellKm) / Math.max(1e-5, NearCellKm)));
        return LEVEL_COLOURS[Math.max(0, Math.min(LEVEL_COLOURS.length - 1, Level))];
    }

    function RenderOceanPlan(Ocean, Plan, Mesh, Options)
    {
        Options = Options || {};
        const Width = Options.Width ?? 760;
        const Height = Options.Height ?? 700;
        const MapHeight = Math.round(Height * 0.72);
        const SpanKm = (Options.SpanKm ?? Plan.Settings.AdaptiveKm) * 1.02;
        const Scale = Width / (2 * SpanKm);
        const RasterImage = RasterApi.CreateRaster(Width, MapHeight, '#080d12');
        const NearCell = Plan.NearCellKm;
        const ToPixel = (EastKm, NorthKm) => ({
            X: Width / 2 + EastKm * Scale,
            Y: MapHeight / 2 - NorthKm * Scale
        });

        for (const Tile of Plan.Leaves)
        {
            const Corner = ToPixel(Tile.X0, Tile.Y0 + Tile.Size);
            RasterApi.FillRect(RasterImage, Corner.X, Corner.Y, Math.max(1, Tile.Size * Scale),
                Math.max(1, Tile.Size * Scale), LevelColour(Tile.Size / Plan.Settings.TileCells, NearCell), 1);
        }
        for (let Ring = 0; Ring <= Plan.Settings.NearRings; ++Ring)
        {
            const T = Ring / Plan.Settings.NearRings;
            const Radius = Plan.Settings.AdaptiveKm * 0.98
                + (Plan.Settings.FarKm - Plan.Settings.AdaptiveKm * 0.98) * Math.pow(T, 1.8);
            if (Radius > SpanKm * 1.45) break;
            RasterApi.StrokeCircle(RasterImage, Width / 2, MapHeight / 2, Radius * Scale, '#5f7fb0', 1);
        }

        // 📝 The inset: the same drawing at the scale of the solve, where the 14 m cells and the blend ring live.
        const Inset = 190, InsetSpanKm = Math.max(0.5, (Ocean.Patch ? Ocean.Patch.ExtentKm : 1.6) * 1.1);
        const InsetScale = Inset / (2 * InsetSpanKm);
        const InsetRaster = RasterApi.CreateRaster(Inset, Inset, '#0a1016');
        for (const Tile of Plan.Leaves)
        {
            if (Math.max(Math.abs(Tile.X0), Math.abs(Tile.Y0)) > InsetSpanKm * 1.4) continue;
            const X = Inset / 2 + Tile.X0 * InsetScale;
            const Y = Inset / 2 - (Tile.Y0 + Tile.Size) * InsetScale;
            RasterApi.FillRect(InsetRaster, X, Y, Math.max(1, Tile.Size * InsetScale), Math.max(1, Tile.Size * InsetScale),
                LevelColour(Tile.Size / Plan.Settings.TileCells, NearCell), 1);
        }
        RasterApi.StrokeCircle(InsetRaster, Inset / 2, Inset / 2, Ocean.NearBlendKm * InsetScale, '#f2ede2', 1);
        RasterApi.StrokeCircle(InsetRaster, Inset / 2, Inset / 2, Ocean.FarBlendKm * InsetScale, '#e8a84a', 1);

        const Parts = [];
        Parts.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" `
            + `viewBox="0 0 ${Width} ${Height}" font-family="'Inter',system-ui,sans-serif">`);
        Parts.push(`<rect width="${Width}" height="${Height}" fill="#080d12"/>`);
        Parts.push(`<image x="0" y="0" width="${Width}" height="${MapHeight}" `
            + `href="${RasterApi.ToDataUrl(RasterApi.EncodePng(RasterImage))}"/>`);
        const Footprint = Plan.NearRadiusKm * Scale;
        Parts.push(`<circle cx="${Width / 2}" cy="${MapHeight / 2}" r="${Math.max(6, Footprint).toFixed(1)}" fill="none" `
            + `stroke="${PAPER}" stroke-width="1.3" stroke-dasharray="5 4"/>`);
        Parts.push(`<circle cx="${Width / 2}" cy="${MapHeight / 2}" r="3" fill="${ACCENT}"/>`);

        const InsetX = Width - Inset - 18, InsetY = Height - Inset - 46;
        Parts.push(`<g><rect x="${InsetX - 6}" y="${InsetY - 22}" width="${Inset + 12}" height="${Inset + 46}" rx="8" `
            + `fill="#0b1218" stroke="#2a3944"/>`);
        Parts.push(`<text x="${InsetX}" y="${InsetY - 6}" fill="${PAPER}" font-size="10.5" fill-opacity="0.8">`
            + `near field · ${InsetSpanKm.toFixed(1)} km across</text>`);
        Parts.push(`<image x="${InsetX}" y="${InsetY}" width="${Inset}" height="${Inset}" `
            + `href="${RasterApi.ToDataUrl(RasterApi.EncodePng(InsetRaster))}"/>`);
        Parts.push(`<text x="${InsetX}" y="${InsetY + Inset + 14}" fill="${PAPER}" font-size="9.5" fill-opacity="0.6">`
            + `white ${Ocean.NearBlendKm} km · amber ${Ocean.FarBlendKm} km</text></g>`);

        const LegendX = 18, LegendY = MapHeight + 18;
        const Colours = [];
        for (let Step = 0; Step < LEVEL_COLOURS.length; ++Step)
        {
            const Colour = RasterApi.Colour(LEVEL_COLOURS[Step]);
            Colours.push(`<rect x="${LegendX + Step * 30}" y="${LegendY}" width="28" height="12" `
                + `fill="rgb(${Colour[0]},${Colour[1]},${Colour[2]})"/>`);
        }
        Parts.push('<g>', Colours.join(''),
            `<text x="${LegendX}" y="${LegendY - 7}" fill="${PAPER}" font-size="10" fill-opacity="0.75">`
            + `cell size, one band per subdivision</text>`,
            `<text x="${LegendX}" y="${LegendY + 26}" fill="${PAPER}" font-size="10" fill-opacity="0.7">`
            + `${(NearCell * 1000).toFixed(0)} m</text>`,
            `<text x="${LegendX + LEVEL_COLOURS.length * 30 - 26}" y="${LegendY + 26}" fill="${PAPER}" font-size="10" `
            + `fill-opacity="0.7">${(NearCell * 1000 * Math.pow(2, LEVEL_COLOURS.length - 1)).toFixed(0)} m</text></g>`);
        Parts.push(`<text x="${LegendX + LEVEL_COLOURS.length * 30 + 18}" y="${LegendY + 12}" fill="${PAPER}" `
            + `font-size="10.5" fill-opacity="0.7">${Plan.Leaves.length.toLocaleString('en-GB')} tiles · `
            + `span ${Plan.Settings.AdaptiveKm} km · ${Plan.Settings.CameraHeightKm} km camera</text>`);
        Parts.push('</svg>');
        return Parts.join('');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    SURFACE SLICE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 A cross-section: the bed from the terrain SDF, the solved surface from Eta, the spectral surface behind it, and the
    //    blend band in between. This is the picture that shows a shallow-water solve is running and not a texture.
    function RenderSurfaceSlice(Ocean, Options)
    {
        Options = Options || {};
        const Width = Options.Width ?? 760;
        const Height = Options.Height ?? 260;
        const Patch = Ocean.Patch;
        if (!Patch) return `<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}"></svg>`;
        // 📝 Default to the wettest row, not the middle one: on a coastal patch the centre line is often dry ground, and a
        //    cross-section through a beach tells nobody anything about the solve.
        // 📝 Stay clear of the forced ring: the outer three rows are the wave maker, not the water it drives.
        const Ring = 6;
        let Wettest = Math.round(Patch.Cells / 2), Most = -1;
        for (let Candidate = Ring; Candidate < Patch.Cells - Ring; ++Candidate)
        {
            let Wet = 0;
            for (let Column = 0; Column < Patch.Cells; ++Column) Wet += Patch.Wet[Candidate * Patch.Cells + Column];
            if (Wet > Most) { Most = Wet; Wettest = Candidate; }
        }
        const Row = Math.max(0, Math.min(Patch.Cells - 1, Math.round(Options.Row ?? Wettest)));
        // 📝 Auto-fit the vertical scale to the row: a fixed scale either flattens a calm row or clips a driven one, and the
        //    point of the figure is to show the solve moving. The axis label carries the real range.
        const LonOfColumn = Column =>
            Patch.FocusLon + ((Column - Patch.Cells / 2 + 0.5) * Patch.CellKm)
            / (111.32 * Math.max(0.05, Math.cos(Patch.FocusLat * Math.PI / 180)));
        const LatOfRow = Patch.FocusLat + ((Patch.Cells / 2 - Row - 0.5) * Patch.CellKm) / 110.574;
        let Peak = 0;
        for (let Column = 0; Column < Patch.Cells; ++Column)
        {
            Peak = Math.max(Peak, Math.abs(Patch.Eta[Row * Patch.Cells + Column]));
            Peak = Math.max(Peak, Math.abs(Ocean.SpectralHeight(LonOfColumn(Column), LatOfRow, Ocean.Clock)));
        }
        const AutoFit = Peak > 0.02 ? Peak * 1.08 : 0.5;
        const Amplitude = Math.max(0.2, Options.Amplitude ?? AutoFit);
        const BedSpan = 96;
        const Mid = Height / 2 - 26;
        const X = Index => 44 + (Index / (Patch.Cells - 1)) * (Width - 76);
        const SurfaceY = Value => Mid - (Value / Amplitude) * (Height * 0.22);
        const BedY = Depth => Mid + 24 + (Depth / Math.max(1, Patch.Deepest)) * BedSpan;

        const Surface = [], Bed = [];
        for (let Column = 0; Column < Patch.Cells; ++Column)
        {
            const Index = Row * Patch.Cells + Column;
            Surface.push(`${X(Column).toFixed(1)},${SurfaceY(Patch.Eta[Index]).toFixed(1)}`);
            Bed.push(`${X(Column).toFixed(1)},${BedY(Patch.Depth[Index]).toFixed(1)}`);
        }
        const Spectral = [];
        for (let Column = 0; Column < Patch.Cells; ++Column)
        {
            Spectral.push(`${X(Column).toFixed(1)},`
                + `${SurfaceY(Ocean.SpectralHeight(LonOfColumn(Column), LatOfRow, Ocean.Clock)).toFixed(1)}`);
        }

        const Parts = [];
        Parts.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" `
            + `viewBox="0 0 ${Width} ${Height}" font-family="'Inter',system-ui,sans-serif">`);
        Parts.push(`<rect x="0" y="0" width="${Width}" height="${Height}" fill="#0b1218"/>`);
        Parts.push(`<line x1="44" y1="${Mid.toFixed(1)}" x2="${Width - 32}" y2="${Mid.toFixed(1)}" `
            + `stroke="${PAPER}" stroke-opacity="0.18" stroke-dasharray="4 5"/>`);
        for (const Level of [1, -1])
        {
            const Y = SurfaceY(Level * Amplitude).toFixed(1);
            Parts.push(`<line x1="44" y1="${Y}" x2="${Width - 32}" y2="${Y}" stroke="${PAPER}" stroke-opacity="0.09"/>`);
            Parts.push(`<text x="46" y="${(Number(Y) - 3).toFixed(1)}" fill="${PAPER}" font-size="9" fill-opacity="0.45">`
                + `${Level > 0 ? '+' : '−'}${Amplitude.toFixed(2)} m</text>`);
        }
        Parts.push(`<polygon points="${Bed.join(' ')} ${X(Patch.Cells - 1).toFixed(1)},${Height - 12} `
            + `${X(0).toFixed(1)},${Height - 12}" fill="#1b2a33" stroke="#2c4450"/>`);
        Parts.push(`<polyline points="${Spectral.join(' ')}" fill="none" stroke="#7aa7e0" stroke-opacity="0.7" `
            + `stroke-width="1.2" stroke-dasharray="3 3"/>`);
        // 📝 The water body follows the bed beneath it, so the column depth is the picture, not to mention the bathymetry.
        Parts.push(`<polygon points="${Surface.join(' ')} ${Bed.slice().reverse().join(' ')}" `
            + `fill="#2f7d92" fill-opacity="0.5"/>`);
        Parts.push(`<polyline points="${Surface.join(' ')}" fill="none" stroke="${COOL}" stroke-width="1.8"/>`);
        Parts.push(`<text x="14" y="20" fill="${PAPER}" font-size="11" fill-opacity="0.75">`
            + `row ${Row} · ${(Patch.CellKm * 1000).toFixed(0)} m cells · t ${Ocean.Clock.toFixed(1)} s</text>`);
        for (const Probe of Patch.Probes)
        {
            if (Probe.Y !== Row) continue;
            Parts.push(`<circle cx="${X(Probe.X).toFixed(1)}" cy="${SurfaceY(Probe.Height).toFixed(1)}" r="3.4" `
                + `fill="${ACCENT}"/>`);
        }
        Parts.push(`<text x="44" y="${(Height - 16)}" fill="${PAPER}" font-size="10" fill-opacity="0.6">`
            + `solved surface — solid · spectral field — dashed · sea bed — filled</text>`);
        Parts.push('</svg>');
        return Parts.join('');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    SMALL HELPERS
    //------------------------------------------------------------------------------------------------------------------------

    function RenderCityPads(Field, Options)
    {
        Options = Options || {};
        const Width = Options.Width ?? 360;
        const Height = Options.Height ?? 150;
        const Cities = Field.Cities.slice(0, 3);
        const Parts = [];
        Parts.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" `
            + `viewBox="0 0 ${Width} ${Height}" font-family="'Inter',system-ui,sans-serif">`);
        Parts.push(`<rect width="${Width}" height="${Height}" fill="#0b1218"/>`);
        const Columns = Math.max(1, Cities.length);
        const CellWidth = Width / Columns;
        Cities.forEach((City, Index) =>
        {
            const OriginX = Index * CellWidth + CellWidth / 2;
            const OriginY = Height / 2 - 8;
            const Reach = Math.min(CellWidth, Height) * 0.36;
            Parts.push(`<circle cx="${OriginX.toFixed(1)}" cy="${OriginY.toFixed(1)}" r="${Reach.toFixed(1)}" `
                + `fill="#25333d" stroke="${ACCENT}" stroke-width="1.2"/>`);
            Parts.push(`<circle cx="${OriginX.toFixed(1)}" cy="${OriginY.toFixed(1)}" r="${(Reach * 0.62).toFixed(1)}" `
                + `fill="#31424c" stroke="${PAPER}" stroke-opacity="0.3"/>`);
            Parts.push(`<circle cx="${OriginX.toFixed(1)}" cy="${OriginY.toFixed(1)}" r="2.4" fill="${ACCENT}"/>`);
            Parts.push(`<text x="${OriginX.toFixed(1)}" y="${(Height - 26).toFixed(1)}" text-anchor="middle" `
                + `fill="${PAPER}" font-size="11">${Escape(City.Name)}</text>`);
            Parts.push(`<text x="${OriginX.toFixed(1)}" y="${(Height - 12).toFixed(1)}" text-anchor="middle" `
                + `fill="${ACCENT}" font-size="10">${City.RadiusKm.toFixed(0)} km pad · ${City.Height.toFixed(0)} m</text>`);
        });
        Parts.push('</svg>');
        return Parts.join('');
    }

    return { RenderPlanetMap, RenderOceanPlan, RenderSurfaceSlice, RenderCityPads, BANDS, INK, PAPER, ACCENT, COOL };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = Scenes;
