/*==========================================================================================================================================
                                                           PREVIEWTILING.TEST.JS
==========================================================================================================================================*/
// 🧩 Hard raster budgets, bounded high-DPI storage, gap-free changing-size rectangles and hardware-independent preview sizing.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { PreviewExtent, PreviewTile, AdaptivePreviewWidth, ConstructPreviewDensity, InitialPreviewPixels, MaximumTerrainTilePixels, MaximumOceanTilePixels, MaximumPreviewPixels } from './PreviewTiling.js';

test('preview extents bound high-DPI and portrait storage independently of advertised GPU vendor', () =>
{
    for (const [Width, Height] of [[1000, 750], [3840, 2160], [300, 10000], [10000, 300]])
    for (const Ratio of [1, 2, 4])
    for (const Requested of [96, 256, 480, 900, 1440, 999999])
    {
        const [PixelsX, PixelsY] = PreviewExtent(Width, Height, Ratio, Requested);
        Assert.ok(PixelsX <= 1441 && PixelsY <= 2048);
        Assert.ok(PixelsX * PixelsY <= MaximumPreviewPixels);
        Assert.ok(PixelsX > 0 && PixelsY > 0);
    }
    for (const Invalid of [0, -1, Infinity, NaN]) Assert.equal(PreviewExtent(Invalid, 700, 2, 256), null);
});

test('retained diagnostic tiles obey their arithmetic caps (not used by the live surface renderer)', () =>
{
    Assert.equal(MaximumTerrainTilePixels, 256);
    Assert.equal(MaximumOceanTilePixels, 4096);
    for (const Budget of [64, 128, MaximumTerrainTilePixels])
    {
        const Tile = PreviewTile(1440, 1200, { X: 0, Y: 0 }, Budget);
        Assert.ok(Tile.Width * Tile.Height <= Budget);
        Assert.ok(Tile.Width * Tile.Height < 1440 * 1200 / 1000);
    }
});

test('incremental preview rectangles cover every pixel exactly once, including partial right and bottom edges', () =>
{
    for (const [Width, Height] of [[1, 1], [63, 97], [96, 79], [257, 211], [900, 744]])
    {
        const Coverage = new Uint8Array(Width * Height);
        let Cursor = { X: 0, Y: 0 }, Count = 0;
        while (Cursor.Y < Height)
        {
            const Budget = [256, 128, 64][Count % 3];
            const Tile = PreviewTile(Width, Height, Cursor, Budget);
            Assert.ok(Tile.Width * Tile.Height <= Budget);
            for (let Y = Tile.Y; Y < Tile.Y + Tile.Height; Y++)
            for (let X = Tile.X; X < Tile.X + Tile.Width; X++) Assert.equal(Coverage[Y * Width + X]++, 0);
            Cursor = Tile.Next;
            Assert.ok(++Count <= Width * Height);
        }
        Assert.ok(Coverage.every(Count => Count === 1));
        Assert.equal(PreviewTile(Width, Height, Cursor, 64), null);
    }
});

test('reducing the budget inside a tall row narrows the tile rather than submitting an oversized GPU job', () =>
{
    const First = PreviewTile(257, 211, { X: 0, Y: 0 }, 4096);
    Assert.equal(First.Height, 64);
    const Smaller = PreviewTile(257, 211, First.Next, 64);
    Assert.equal(Smaller.Width, 1);
    Assert.equal(Smaller.Height, 64);
    Assert.equal(Smaller.X, First.Width);
    Assert.equal(Smaller.Y, 0);
});

test('invalid raster cursors and budgets fail rather than looping or issuing negative scissor extents', () =>
{
    for (const Cursor of [{ X: -1, Y: 0 }, { X: 96, Y: 0 }, { X: 0, Y: -1 }, { X: .5, Y: 0 }, { X: 1, Y: 0, RowHeight: 0 }])
        Assert.throws(() => PreviewTile(96, 80, Cursor, 64), /preview/);
    for (const Budget of [0, NaN, Infinity, .5]) Assert.throws(() => PreviewTile(96, 80, { X: 0, Y: 0 }, Budget), /budget/);
});

test('adaptive preview sizing responds to measured cost, not a software-versus-hardware renderer string', () =>
{
    Assert.ok(AdaptivePreviewWidth(256, 2000) < AdaptivePreviewWidth(256, 100));
    Assert.equal(AdaptivePreviewWidth(1440, .001), 480);
    Assert.equal(AdaptivePreviewWidth(64, 99999), 96);
    for (const Invalid of [0, -1, NaN, Infinity]) Assert.equal(AdaptivePreviewWidth(Invalid, 100), 256);
});

test('completed GPU images refine the initial pixel cap while slow images set a stable ceiling', () =>
{
    const Density = ConstructPreviewDensity();
    Assert.equal(Density.Pixels(), InitialPreviewPixels);
    for (let Index = 0; Index < 20; Index++) Density.Observe(Density.Pixels(), 12);
    Assert.equal(Density.Pixels(), MaximumPreviewPixels);
    Density.Observe(1000000, 900);
    Assert.equal(Density.Ceiling(), 1000000);
    Assert.equal(Density.Pixels(true), InitialPreviewPixels);
    for (let Index = 0; Index < 10; Index++) Density.Observe(Density.Pixels(), 20);
    Assert.equal(Density.Pixels(), 1000000, 'do not breathe between quality levels after finding a slow limit');
    Density.Observe(1000000, 2000);
    Assert.equal(Density.Pixels(), 750000);
    Assert.equal(Density.Ceiling(), 750000);
    for (const Invalid of [0, -1, Infinity, NaN])
    {
        Density.Observe(Invalid, 20);
        Density.Observe(800000, Invalid);
        Assert.equal(Density.Pixels(), 750000);
        Assert.equal(PreviewExtent(1000, 800, 1, 1000, Invalid), null);
    }
});

test('explicit initial budgets stay bounded while completed images can reach native viewport resolution', () =>
{
    const Draft = PreviewExtent(1000, 800, 1, 1000, InitialPreviewPixels);
    const Native = PreviewExtent(1000, 800, 1, 1000, MaximumPreviewPixels);
    Assert.ok(Draft[0] < 700);
    Assert.ok(Draft[0] * Draft[1] <= InitialPreviewPixels);
    Assert.deepEqual(Native, [1000, 800]);
});
