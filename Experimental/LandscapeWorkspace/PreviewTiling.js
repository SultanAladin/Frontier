/*==========================================================================================================================================
                                                             PREVIEWTILING.JS
==========================================================================================================================================*/
// 🧩 Pixel-bounded preview extents and gap-free incremental raster rectangles, independent of GPU vendor identification.

export const InitialPreviewPixels = 360000;
export const MaximumPreviewPixels = 1500000;
export const MaximumTerrainTilePixels = 256;
export const MaximumOceanTilePixels = 4096;

export function PreviewExtent(
    ClientWidth,
    ClientHeight,
    PixelRatio,
    RequestedWidth,
    PixelBudget = MaximumPreviewPixels)
{
    if (![ClientWidth, ClientHeight, PixelRatio, RequestedWidth, PixelBudget].every(Number.isFinite) ||
        Math.min(ClientWidth, ClientHeight, PixelRatio, RequestedWidth, PixelBudget) <= 0) return null;
    const Requested = Math.min(1440, Math.max(64, RequestedWidth));
    let Ratio = Math.min(PixelRatio, 1.5, Requested / ClientWidth);
    Ratio = Math.min(Ratio, Math.sqrt(Math.min(MaximumPreviewPixels, PixelBudget) / (ClientWidth * ClientHeight)), 2048 / ClientHeight);
    const Width = Math.max(1, Math.round(ClientWidth * Ratio));
    const Height = Math.max(1, Math.min(Math.round(ClientHeight * Ratio), Math.floor(Math.min(MaximumPreviewPixels, PixelBudget) / Width)));
    return [Width, Height];
}

// 📝 Resolution rises only after completed GPU images. Slow images set a persistent ceiling rather than oscillating with distance.
export function ConstructPreviewDensity()
{
    let Pixels = InitialPreviewPixels, Ceiling = MaximumPreviewPixels, LastMilliseconds = 0;
    return {
        Observe(DrawnPixels, Milliseconds)
        {
            if (!Number.isFinite(DrawnPixels) || DrawnPixels <= 0 || !Number.isFinite(Milliseconds) || Milliseconds <= 0) return;
            LastMilliseconds = Milliseconds;
            if (Milliseconds > 1200)
            {
                Ceiling = Math.min(Ceiling, Math.max(65536, Math.floor(DrawnPixels * .75)));
                Pixels = Math.min(Pixels, Ceiling);
            }
            else if (Milliseconds > 700)
            {
                Ceiling = Math.min(Ceiling, Math.max(65536, DrawnPixels));
                Pixels = Math.min(Pixels, Ceiling);
            }
            else Pixels = Math.min(Ceiling, Math.round(Pixels * 1.2));
        },
        Pixels(Interacting = false) { return Interacting && LastMilliseconds > 80 ? Math.min(Pixels, InitialPreviewPixels) : Pixels; },
        Ceiling() { return Ceiling; }
    };
}

export function PreviewTile(
    Width,
    Height,
    Cursor,
    PixelBudget,
    ColumnWidth = 64)
{
    if (![Width, Height, PixelBudget, ColumnWidth].every(Number.isSafeInteger) ||
        Math.min(Width, Height, PixelBudget, ColumnWidth) < 1) throw Error('Invalid preview tile extent or budget');
    const X = Cursor?.X ?? 0, Y = Cursor?.Y ?? 0;
    if (![X, Y].every(Number.isSafeInteger) || X < 0 || X >= Width || Y < 0 || Y > Height) throw Error('Invalid preview cursor');
    if (Y === Height) return null;
    const SpanY = X ? Cursor.RowHeight : Math.min(Height - Y, 64, Math.max(1, Math.floor(PixelBudget / Math.min(ColumnWidth, Width))));
    if (!Number.isSafeInteger(SpanY) || SpanY < 1 || SpanY > Height - Y || SpanY > PixelBudget) throw Error('Invalid preview row extent');
    // 📝 A budget reduction midway through a row narrows the rectangle, never exceeds the new pixel cap or leaves gaps.
    const SpanX = Math.min(ColumnWidth, Width - X, Math.floor(PixelBudget / SpanY));
    const NextX = X + SpanX;
    return {
        X, Y, Width: SpanX, Height: SpanY,
        Next: NextX >= Width ? { X: 0, Y: Y + SpanY, RowHeight: 0 } : { X: NextX, Y, RowHeight: SpanY }
    };
}

export function AdaptivePreviewWidth(Width, Milliseconds)
{
    if (!Number.isFinite(Width) || Width <= 0 || !Number.isFinite(Milliseconds) || Milliseconds <= 0) return 256;
    return Math.max(96, Math.min(480, Math.round(Width * Math.sqrt(250 / Milliseconds) / 32) * 32));
}
