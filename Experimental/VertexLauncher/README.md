# Frontier launcher prototype

A simple game launcher, store and download/updater UI. This revision follows the three supplied General UI references:
charcoal surfaces, large rounded cards, a floating left dock, blue accent, Inter typography and artwork-led pages.
It deliberately removes the previous dashboard panels, statistics, friends list and permanent configuration drawer.

## Run

```bash
cd Experimental/VertexLauncher
python3 -m http.server 5173 --bind 0.0.0.0
```

Open the served `index.html`. No build step or runtime network dependency is required.

## Pages

| Page      | Behaviour                                                                 |
|-----------|---------------------------------------------------------------------------|
| Store     | Featured carousel, Discover/Browse/Wishlist pills, genre filters, search.   |
| Library   | Owned games, installed/update filters, grid/list toggle, launch action.     |
| Downloads | Speed-trace graph and time-left cards; active/paused/queued/completed transfer cards; Storage and Preferences rail. |
| Settings  | Automatic-update preference and download-speed limit.                       |

`npm run serve` starts the preview on port 5173 with `Cache-Control: no-store`. Stylesheet, launcher script and ES-module
imports carry a revision query so an embedded preview cannot mix an updated document with stale cached CSS/JS.

Store and Library cards use internal grids: cover, title, icon metadata and a three-icon action row
(install/play/update, wishlist, details). The Store toolbar includes a compact-grid toggle.

The floating dock keeps navigation, settings and a circular active-download progress ring visible outside the Downloads
page. The Downloads rail exposes a games/system/free storage breakdown and preferences for automatic updates and
bandwidth limits; both stay in sync with the settings dialog. Library, wishlist and settings persist in
`localStorage`. Transfers are deterministic local simulations; no game files are downloaded and no executable is launched.

## Verify

```bash
npm ci
npm test
npm run verify
```

`npm run verify` expects the static server above on port 5173. It uses headless Chromium and writes captures and a JSON
report to `Exhibits/LauncherStorefront/`. On Linux hosts without system Chromium libraries, the validation script uses the
serverless Chromium package; set `LD_LIBRARY_PATH=/tmp/al2023/lib:/tmp` if the package's bundled libraries are not picked up.

## Assets

Game artwork is stored locally under `assets/art/` for prototype use. Source pages are listed in `assets/CREDITS.md`.
Inter is self-hosted under the SIL Open Font License; see `assets/fonts/OFL-Inter.txt`.
