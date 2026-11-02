# Hearth launcher prototype

A warm, editorial take on a game/app launcher: one featured "stage", a scrollable
shelf, a live weekly panel, and a full launch-to-session flow. Static HTML, CSS,
and vanilla JavaScript — no build step, no runtime dependencies, no generated
imagery.

```text
Experimental/HearthLauncher/
├── index.html
├── styles.css              Design system, layout, motion, both palettes
├── launcher.js             Content, rendering, interactions, Web Audio engine
├── Code/
│   └── LauncherInteractionChecks.mjs
├── assets/
│   ├── rayman.jpg          Crops of the supplied game-store reference
│   ├── mario.jpg
│   ├── clash.jpg
│   ├── ledger.jpg          Crop of the supplied dashboard reference
│   └── fonts/              Self-hosted OFL faces and licence texts
├── package.json            Test-only dependency (jsdom)
└── package-lock.json
```

## Run it

From this folder:

```bash
python3 -m http.server 5001
```

Open `http://localhost:5001/`. `npm run serve` is the same command. Everything is
relative, so any static server works; no ports are hard-coded in the application.

## Design references

The three supplied references were used as material, not as templates:

- **Dark roadmap panel (Coming Soon).** Supplied the hairline discipline, tiny
  uppercase mono labels, dot-and-line timeline, and the warm near-black surface.
  Rebuilt natively as the full-width Updates band.
- **Game-store phone screens.** Supplied the three portrait key arts, which are
  cropped from the reference and framed as physical posters rather than stretched
  into wide banners. The two-carousel dots, star ratings, and JOGAR-style colour
  accents become the thumbnail strip, rating rows, and per-app accents.
- **Fishbowl dashboard.** Supplied the warm brown palette, segmented progress
  pills, serif/number contrast, and the "work" content. Its cropped dashboard is
  the Ledger app; the progress pills became the weekly breakdown.

No new images were generated. The four extra app covers (Nova Field, Bassline,
Kiln, Orbit Radio) are CSS drawings, so the shelf does not repeat the three photos.

## Interactions

- Featured stage auto-rotates every 9 s; hovering pauses it. Thumbnails, `←`/`→`,
  or clicking an app open the stage for that app.
- `Play` runs a launch sequence (progress + status) and hands over to a live
  session with a running clock, pause/resume, highlight, and end-session summary.
- Card hover lifts, art scales, accent line draws, play affordance fades in.
- Shelf filters (All/Games/Studio/Work), rail ↔ grid view, horizontal drag/scroll.
- `⌘K` / `Ctrl K` (or `/`) opens a fuzzy command palette with keyboard navigation.
- Detail sheet with synopsis, chips, metadata, save-to-favourites and play.
- Lamp toggle switches a full warm-paper palette; preference persists.
- Update likes and favourites persist in `localStorage` (safe no-ops in private mode).
- `Esc` closes the topmost layer; keys are ignored while typing in search.

## Audio

All sound is synthesised with the Web Audio API; there are no audio files.
- Hover ticks, click blips, support chimes, and launch whooshes.
- A four-oscillator ambient pad with a slow filter LFO runs during a session, and
  can be auditioned for eight seconds from the Room Tone panel.
- The footer waveform is driven by an `AnalyserNode`.
- Audio starts only after a gesture. The header speaker and `M` toggle mute; the
  choice persists. Launching an app enables sound once for first-time visitors.

## Verification

`npm ci && npm test` runs 24 jsdom interaction checks covering boot, rendering,
featured selection, filters, both shelf views, palette search, detail sheet,
launch, session timer, pause relabelling, session-ending, overlays, theme, sound,
room tone, likes, icon references, and runtime errors. All 24 pass.

The prototype was also rendered in headless Chromium at 1440 × 960, 1024 × 860,
820 × 900, and 390 × 844 to check layout, the light palette, detail sheet, launch,
session, and mobile stacking. Those screenshots are working evidence, not tracked
product assets. Console and page errors were empty.

## Known limits

- No real executable launches; the session is a designed simulation.
- Playtime history is presentational; only favourites, support, theme, and sound persist.
- Reference photo crops are sample material from the supplied brief. Review rights
  before redistributing them outside this prototype (tracker item 013).
- Fonts are SIL OFL 1.1: Fraunces, DM Sans, IBM Plex Mono; licence texts are in
  `assets/fonts/`. The UI does not request any third-party network resources.
