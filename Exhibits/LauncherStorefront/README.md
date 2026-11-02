# Launcher storefront validation

Actual rendered evidence for `Experimental/VertexLauncher` on branch `arena/d888bed1-frontier`.

## Reproduce

```bash
cd Experimental/VertexLauncher
python3 -m http.server 5173 --bind 0.0.0.0
# In a second terminal:
npm ci
npm test
LD_LIBRARY_PATH=/tmp/al2023/lib:/tmp npm run verify
```

The `LD_LIBRARY_PATH` prefix is only needed on Linux hosts where the serverless Chromium bundled libraries are not already
on the loader path. The validation script itself is `Experimental/VertexLauncher/Code/LauncherValidation.mjs`.

## Captures

| File             | Viewport  | Content                                  |
|------------------|-----------|------------------------------------------|
| Store.png        | 1440×960  | Default store, hero and five-card row.    |
| Library.png      | 1440×960  | Owned games with installed/update badges. |
| DownloadsActive.png | 1440×960 | Active transfer with speed graph, time left and storage cards. |
| Downloads.png    | 1440×960  | Paused active card and queued update.     |
| MobileStore.png  | 390×844   | Mobile store and bottom navigation.       |
| PreviewDownloads.png | 780×600 | Narrow split-preview downloads viewport. |

## Results

`Results/ChromiumValidation.json` records 64 rendered interaction checks across desktop, split-preview and mobile
viewports, including cache-header, metric-consistency, blue-accent, floating-dock, storage-breakdown and preferences
checks. Unit coverage is in `Experimental/VertexLauncher/Code/TransferQueue.test.mjs` and `TransferMetrics.test.mjs`
(12 checks total).

## Limitations

This is browser prototype evidence, not a native Windows application capture. Store purchases, game launches and file
transfers are local simulations. No payment, real download or executable launch is performed.
