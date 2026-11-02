/* ==========================================================================
   Hearth launcher — prototype behaviour
   Vanilla ES2020. No dependencies, no audio files: every sound is synthesised.
   ========================================================================== */

(() => {
  'use strict';

  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  const store = {
    get(key, fallback) {
      try {
        const raw = localStorage.getItem(`hearth.${key}`);
        return raw === null ? fallback : JSON.parse(raw);
      } catch { return fallback; }
    },
    set(key, value) {
      try { localStorage.setItem(`hearth.${key}`, JSON.stringify(value)); } catch { /* private mode */ }
    },
  };

  /* Content --------------------------------------------------------------- */

  const APPS = [
    {
      id: 'rayman', index: '01', title: 'Rayman Adventures',
      titleMain: 'Rayman', titleAccent: 'Adventures',
      studio: 'Ubisoft Entertainment', genre: 'Platformer', category: 'game',
      art: 'assets/rayman.jpg', accent: '#f7b733', rating: '4.8', size: '1.2 GB',
      played: 'Yesterday', playtime: '42 hrs', likes: 128,
      chips: ['Platformer', 'Hand-drawn', '1–2 players'],
      blurb: 'Limber limbs, a bottomless appetite for adventure. Swing through painted worlds where every leaf is hand-drawn and every jump is a small act of faith.',
      note: 'The painted forest is open. Take the long way.',
    },
    {
      id: 'mario', index: '02', title: 'Super Mario Run',
      titleMain: 'Super Mario', titleAccent: 'Run',
      studio: 'Nintendo Co., Ltd.', genre: 'Auto-runner', category: 'game',
      art: 'assets/mario.jpg', accent: '#f2543d', rating: '4.6', size: '320 MB',
      played: 'Three days ago', playtime: '18 hrs', likes: 96,
      chips: ['Auto-runner', 'Single player', 'Nintendo'],
      blurb: 'One thumb, one kingdom. A perfectly tuned sprint through pipes and palaces, built for the thirty seconds you actually have.',
      note: 'World 1–4 is waiting at the flagpole.',
    },
    {
      id: 'clash', index: '03', title: 'Clash Royale',
      titleMain: 'Clash', titleAccent: 'Royale',
      studio: 'Supercell', genre: 'Strategy', category: 'game',
      art: 'assets/clash.jpg', accent: '#4f7dff', rating: '4.4', size: '180 MB',
      played: 'Last week', playtime: '61 hrs', likes: 212,
      chips: ['Real-time PvP', 'Deck builder', '3 min rounds'],
      blurb: 'A deck, a tower, a very smug king. Three-minute sieges where the whole plan can turn on one misplaced skeleton.',
      note: 'Matchmaking is quiet. Your tower is not.',
    },
    {
      id: 'ledger', index: '04', title: 'Ledger',
      titleMain: 'Ledger', titleAccent: '',
      studio: 'Hearth Labs', genre: 'Work · Finance', category: 'work',
      art: 'assets/ledger.jpg', accent: '#7c8cff', rating: '4.9', size: '64 MB',
      played: 'This morning', playtime: 'Weekly', likes: 34,
      chips: ['Work', 'Finance', 'Dashboard'],
      blurb: 'A calmer place for loud numbers. Positions, capital and correspondence arranged on one warm surface so the day starts with a glance, not a search.',
      note: 'Three items need your eyes. Nothing is on fire.',
    },
    {
      id: 'nova', index: '05', title: 'Nova Field',
      titleMain: 'Nova', titleAccent: 'Field',
      studio: 'Studio Ochre', genre: 'Exploration', category: 'game',
      cover: 'nova', accent: '#b9e36b', rating: '4.7', size: '780 MB',
      played: 'Two weeks ago', playtime: '9 hrs', likes: 58,
      chips: ['Exploration', 'Ambient', 'Controller'],
      blurb: 'A pale green planet with one rule: the horizon keeps moving. Walk until the light changes, then walk a little further.',
      note: 'The field is quiet. Keep the window open.',
    },
    {
      id: 'bassline', index: '06', title: 'Bassline',
      titleMain: 'Bassline', titleAccent: '',
      studio: 'Low End Theory', genre: 'Studio · Sequencer', category: 'studio',
      cover: 'bassline', accent: '#ff8a5c', rating: '4.5', size: '210 MB',
      played: 'Four days ago', playtime: '12 hrs', likes: 71,
      chips: ['Sequencer', 'Audio', 'MIDI'],
      blurb: 'Sixteen steps, endless low end. A step sequencer that sounds like a warm room and refuses to get in the way of the groove.',
      note: 'The last pattern is still holding its breath.',
    },
    {
      id: 'kiln', index: '07', title: 'Kiln',
      titleMain: 'Kiln', titleAccent: '',
      studio: 'Hearth Labs', genre: 'Studio · Texture', category: 'studio',
      cover: 'kiln', accent: '#e0a96a', rating: '4.8', size: '410 MB',
      played: 'Yesterday', playtime: '26 hrs', likes: 44,
      chips: ['Texture', 'Material', 'Offline'],
      blurb: 'Fire, grain and oxide. Bake procedural materials until they feel handled — then export them straight into a project.',
      note: 'The last firing cooled overnight. It looks good.',
    },
    {
      id: 'orbit', index: '08', title: 'Orbit Radio',
      titleMain: 'Orbit', titleAccent: 'Radio',
      studio: 'Transmission', genre: 'Work · Ambient', category: 'work',
      cover: 'orbit', accent: '#8ba1ff', rating: '4.3', size: '48 MB',
      played: 'Today', playtime: 'Continuous', likes: 19,
      chips: ['Ambient', 'Focus', 'Live'],
      blurb: 'Slow signals from the edge of the day. Four channels of generative ambient, tuned for spreadsheets and long thoughts.',
      note: 'Channel two is drifting somewhere near Saturn.',
    },
  ];

  const byId = (id) => APPS.find((app) => app.id === id);
  const FEATURED = ['rayman', 'mario', 'clash'];
  const KICKERS = ['featured tonight', 'staff pick', 'most played this week'];
  const ROTATE_MS = 9000;

  const WEEK = [
    { id: 'rayman', pct: 38, color: '#f7b733' },
    { id: 'clash', pct: 18, color: '#4f7dff' },
    { id: 'kiln', pct: 13, color: '#e0a96a' },
    { id: 'ledger', pct: 17, color: '#7c8cff' },
    { id: 'mario', pct: 14, color: '#f2543d' },
  ];

  const UPDATES = [
    {
      id: 'sync', state: 'progress', title: 'Shelf sync across devices',
      desc: 'Installed state, saves and last-played order follow you between machines.', likes: 42,
    },
    {
      id: 'ambient', state: 'progress', title: 'Ambient soundscapes per app',
      desc: 'Every shelf item gets its own generated room tone. This prototype already hums.', likes: 5,
    },
    {
      id: 'pad', state: 'progress', title: 'Controller-first navigation',
      desc: 'D-pad, bumpers and a dedicated home button for the couch.', likes: 2,
    },
    {
      id: 'pocket', state: 'planned', title: 'Second-screen companion',
      desc: 'Keep the shelf in your pocket while the game owns the display.', likes: 39,
    },
  ];

  /* Audio ----------------------------------------------------------------- */
  /* Everything below is built from oscillators and noise. No audio files. */

  class AudioEngine {
    constructor() {
      this.ctx = null;
      this.master = null;
      this.analyser = null;
      this.levels = null;
      this.on = false;
      this.ambient = null;
      this.buffer = null;
    }

    ensure() {
      if (this.ctx) return this.ctx;
      const Ctor = window.AudioContext || window.webkitAudioContext;
      if (!Ctor) return null;
      const ctx = new Ctor();
      this.ctx = ctx;
      this.master = ctx.createGain();
      this.master.gain.value = 0;
      this.analyser = ctx.createAnalyser();
      this.analyser.fftSize = 64;
      this.analyser.smoothingTimeConstant = 0.82;
      this.levels = new Uint8Array(this.analyser.frequencyBinCount);
      this.master.connect(this.analyser);
      this.analyser.connect(ctx.destination);
      return ctx;
    }

    enable(on) {
      const ctx = this.ensure();
      if (!ctx) return false;
      if (ctx.state === 'suspended') ctx.resume();
      this.on = on;
      const now = ctx.currentTime;
      this.master.gain.cancelScheduledValues(now);
      this.master.gain.setTargetAtTime(on ? 0.9 : 0, now, 0.08);
      return true;
    }

    tone({ freq = 880, to = null, dur = 0.09, type = 'sine', gain = 0.05, delay = 0, attack = 0.006 } = {}) {
      if (!this.on || !this.ctx) return;
      const ctx = this.ctx;
      const start = ctx.currentTime + delay;
      const osc = ctx.createOscillator();
      const amp = ctx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(freq, start);
      if (to) osc.frequency.exponentialRampToValueAtTime(Math.max(to, 1), start + dur);
      amp.gain.setValueAtTime(0.0001, start);
      amp.gain.exponentialRampToValueAtTime(gain, start + attack);
      amp.gain.exponentialRampToValueAtTime(0.0001, start + dur);
      osc.connect(amp).connect(this.master);
      osc.start(start);
      osc.stop(start + dur + 0.05);
    }

    noiseBuffer() {
      if (this.buffer) return this.buffer;
      const ctx = this.ctx;
      const length = Math.floor(ctx.sampleRate * 2);
      const buffer = ctx.createBuffer(1, length, ctx.sampleRate);
      const data = buffer.getChannelData(0);
      for (let index = 0; index < length; index += 1) data[index] = Math.random() * 2 - 1;
      this.buffer = buffer;
      return buffer;
    }

    hover() { this.tone({ freq: 1240, to: 1520, dur: 0.055, gain: 0.016 }); }
    tick() { this.tone({ freq: 560, to: 840, dur: 0.06, gain: 0.03, type: 'triangle' }); }
    click() { this.tone({ freq: 420, to: 240, dur: 0.12, gain: 0.045, type: 'triangle' }); }
    toggle() { this.tone({ freq: 320, to: 620, dur: 0.11, gain: 0.035, type: 'square' }); }

    chime() {
      this.tone({ freq: 659.3, dur: 0.5, gain: 0.03 });
      this.tone({ freq: 987.8, dur: 0.65, gain: 0.022, delay: 0.07 });
    }

    chord() {
      [261.6, 329.6, 392, 523.3].forEach((freq, index) => {
        this.tone({ freq, dur: 1.7, gain: 0.028, type: 'triangle', delay: index * 0.055 });
      });
    }

    whoosh(dur = 0.85) {
      if (!this.on || !this.ctx) return;
      const ctx = this.ctx;
      const now = ctx.currentTime;
      const source = ctx.createBufferSource();
      source.buffer = this.noiseBuffer();
      source.loop = true;
      const filter = ctx.createBiquadFilter();
      filter.type = 'bandpass';
      filter.Q.value = 0.9;
      filter.frequency.setValueAtTime(220, now);
      filter.frequency.exponentialRampToValueAtTime(2600, now + dur * 0.55);
      filter.frequency.exponentialRampToValueAtTime(500, now + dur);
      const amp = ctx.createGain();
      amp.gain.setValueAtTime(0.0001, now);
      amp.gain.exponentialRampToValueAtTime(0.09, now + dur * 0.3);
      amp.gain.exponentialRampToValueAtTime(0.0001, now + dur);
      source.connect(filter).connect(amp).connect(this.master);
      source.start(now);
      source.stop(now + dur + 0.05);
    }

    startAmbient() {
      if (!this.ctx || this.ambient) return;
      const ctx = this.ctx;
      const now = ctx.currentTime;
      const bus = ctx.createGain();
      bus.gain.value = 0;
      const filter = ctx.createBiquadFilter();
      filter.type = 'lowpass';
      filter.frequency.value = 560;
      filter.Q.value = 0.8;

      const lfo = ctx.createOscillator();
      lfo.frequency.value = 0.065;
      const lfoDepth = ctx.createGain();
      lfoDepth.gain.value = 220;
      lfo.connect(lfoDepth).connect(filter.frequency);
      lfo.start(now);

      const voices = [110, 164.81, 220, 329.63].map((freq, index) => {
        const osc = ctx.createOscillator();
        osc.type = index % 2 === 0 ? 'triangle' : 'sine';
        osc.frequency.value = freq;
        osc.detune.value = index % 2 === 0 ? -6 : 7;
        const amp = ctx.createGain();
        amp.gain.value = index === 3 ? 0.03 : 0.07;
        osc.connect(amp).connect(filter);
        osc.start(now);
        return osc;
      });

      const noise = ctx.createBufferSource();
      noise.buffer = this.noiseBuffer();
      noise.loop = true;
      const noiseFilter = ctx.createBiquadFilter();
      noiseFilter.type = 'bandpass';
      noiseFilter.frequency.value = 1500;
      noiseFilter.Q.value = 0.6;
      const noiseAmp = ctx.createGain();
      noiseAmp.gain.value = 0.012;
      noise.connect(noiseFilter).connect(noiseAmp).connect(filter);
      noise.start(now);

      filter.connect(bus).connect(this.master);
      bus.gain.setTargetAtTime(0.5, now, 1.4);
      this.ambient = { bus, voices, noise, lfo };
    }

    duck(down) {
      if (!this.ambient) return;
      this.ambient.bus.gain.setTargetAtTime(down ? 0.08 : 0.5, this.ctx.currentTime, 0.35);
    }

    stopAmbient() {
      if (!this.ambient || !this.ctx) return;
      const ambient = this.ambient;
      this.ambient = null;
      const now = this.ctx.currentTime;
      ambient.bus.gain.cancelScheduledValues(now);
      ambient.bus.gain.setTargetAtTime(0, now, 0.3);
      const stopAt = now + 1.2;
      [...ambient.voices, ambient.noise, ambient.lfo].forEach((node) => {
        try { node.stop(stopAt); } catch { /* already stopped */ }
      });
    }

    level() {
      if (!this.analyser) return 0;
      this.analyser.getByteFrequencyData(this.levels);
      let total = 0;
      for (let index = 0; index < this.levels.length; index += 1) total += this.levels[index];
      return total / this.levels.length / 255;
    }
  }

  const audio = new AudioEngine();

  /* Elements -------------------------------------------------------------- */

  const body = document.body;
  const html = document.documentElement;
  const stage = $('#stage');
  const stageInfo = $('.stage-info');
  const stageArtInner = $('#stageArtInner');
  const featIndex = $('#featIndex');
  const featKicker = $('#featKicker');
  const featTitle = $('#featTitle');
  const featChips = $('#featChips');
  const featBlurb = $('#featBlurb');
  const featCaption = $('#featCaption');
  const featThumbs = $('#featThumbs');
  const featLike = $('#featLike');
  const featLikeCount = $('#featLikeCount');
  const featPlay = $('#featPlay');
  const featDetails = $('#featDetails');
  const stageProgress = $('#stageProgress');
  const shelf = $('#shelf');
  const shelfEmpty = $('#shelfEmpty');
  const filters = $('#filters');
  const viewToggle = $('#viewToggle');
  const timeline = $('#timeline');
  const segbar = $('#segbar');
  const seglist = $('#seglist');
  const clock = $('#clock');
  const soundBtn = $('#soundBtn');
  const audioState = $('#audioState');
  const audioLabel = $('#audioLabel');
  const lampBtn = $('#lampBtn');
  const sheet = $('#sheet');
  const sheetArt = $('#sheetArt');
  const sheetKicker = $('#sheetKicker');
  const sheetTitle = $('#sheetTitle');
  const sheetBlurb = $('#sheetBlurb');
  const sheetStudio = $('#sheetStudio');
  const sheetPlayed = $('#sheetPlayed');
  const sheetSize = $('#sheetSize');
  const sheetRating = $('#sheetRating');
  const sheetPlay = $('#sheetPlay');
  const launch = $('#launch');
  const launchBackdrop = $('#launchBackdrop');
  const launchLabel = $('#launchLabel');
  const launchTitle = $('#launchTitle');
  const launchBar = $('#launchBar');
  const launchStatus = $('#launchStatus');
  const launchMark = $('#launchMark');
  const launchStudio = $('#launchStudio');
  const sheetChips = $('#sheetChips');
  const sessionLiveText = $('#sessionLiveText');
  const launchPhase = $('#launchPhase');
  const launchCancel = $('#launchCancel');
  const session = $('#session');
  const sessionTitle = $('#sessionTitle');
  const sessionArt = $('#sessionArt');
  const sessionTimer = $('#sessionTimer');
  const sessionNote = $('#sessionNote');
  const sessionPause = $('#sessionPause');
  const sessionEnd = $('#sessionEnd');
  const sessionHeart = $('#sessionHeart');
  const sessionSound = $('#sessionSound');
  const palette = $('#palette');
  const paletteInput = $('#paletteInput');
  const paletteResults = $('#paletteResults');
  const toastEl = $('#toast');
  const tonePanel = $('.panel-tone');
  const tonePlay = $('#tonePlay');
  const toneState = $('#toneState');
  const toneCta = $('#toneCta');
  const cursorGlow = $('#cursorGlow');
  const statusSession = $('#statusSession');
  const boot = $('#boot');

  /* State ----------------------------------------------------------------- */

  let featuredIndex = 0;
  let rotateTimer = null;
  let hoverPause = false;
  let currentOverlay = null;
  let sheetApp = null;
  let launchToken = 0;
  let sessionTimerId = null;
  let sessionSeconds = 0;
  let sessionPaused = false;
  let sessionApp = null;
  let activeFilter = 'all';
  let paletteActive = 0;
  let paletteMatches = [];
  let lastHoverAt = 0;
  let toastTimer = null;
  let booted = false;
  let toneOn = false;
  let toneTimer = null;

  const likedApps = new Set(store.get('likedApps', []));
  const likedUpdates = new Set(store.get('likedUpdates', []));

  /* Small helpers --------------------------------------------------------- */

  const fmt = (seconds) => {
    const hrs = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${hrs > 0 ? `${String(hrs).padStart(2, '0')}:` : ''}${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
  };

  const pad2 = (value) => String(value).padStart(2, '0');

  const icon = (name, cls = 'i') => `<svg class="${cls}" aria-hidden="true"><use href="#${name}"/></svg>`;
  const STAR = '<svg class="star-i" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3.4l2.7 5.5 6 .9-4.3 4.2 1 6-5.4-2.9-5.4 2.9 1-6L3.3 9.8l6-.9z"/></svg>';

  // Keyboard glyphs that exist everywhere.
  const isApple = /Mac|iPhone|iPad|iPod/.test(navigator.platform || navigator.userAgent);
  $$('[data-mod]').forEach((element) => { element.textContent = isApple ? '⌘' : 'Ctrl'; });

  // Dark ink on light accents, warm white on saturated ones.
  function inkFor(hex) {
    const value = hex.replace('#', '');
    const [r, g, b] = [0, 2, 4].map((offset) => parseInt(value.slice(offset, offset + 2), 16) / 255);
    const lin = (channel) => (channel <= 0.03928 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4);
    const luminance = 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
    return luminance > 0.42 ? '#1a1109' : '#fff8ef';
  }

  function paintAccent(element, accent) {
    element.style.setProperty('--accent', accent);
    element.style.setProperty('--on-accent', inkFor(accent));
  }

  const artMarkup = (app, alt = true) => (
    app.art
      ? `<img src="${app.art}" alt="${alt ? app.title : ''}" ${alt ? '' : 'aria-hidden="true"'} loading="lazy">`
      : `<div class="cover cover--${app.cover}" role="presentation"></div>`
  );

  function toast(message) {
    toastEl.textContent = message;
    toastEl.classList.add('is-on');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove('is-on'), 2600);
  }

  function pop(el) {
    if (!el || reduceMotion) return;
    el.classList.remove('is-pop');
    void el.offsetWidth;
    el.classList.add('is-pop');
  }

  /* Audio wiring ---------------------------------------------------------- */

  let wantsSound = store.get('sound', false);
  let soundChosen = store.get('soundChosen', false);

  function paintAudio() {
    const on = audio.on;
    soundBtn.setAttribute('aria-pressed', String(on));
    soundBtn.setAttribute('aria-label', on ? 'Mute audio' : 'Enable audio');
    audioLabel.textContent = on ? 'audio on' : 'audio off';
    audioState.setAttribute('aria-label', on ? 'Mute audio' : 'Enable audio');
    const sessionIcon = $('#sessionSoundIcon');
    if (sessionIcon) {
      const use = sessionIcon.querySelector('use');
      if (use) use.setAttribute('href', on ? '#i-volume' : '#i-volume-off');
    }
    sessionSound.setAttribute('aria-pressed', String(on));
    paintTone();
  }

  function paintTone() {
    const live = toneOn || Boolean(sessionApp && !session.hidden);
    tonePanel.classList.toggle('is-live', live);
    toneState.textContent = live ? 'live' : 'idle';
    toneCta.textContent = live ? 'tap to silence' : 'preview 8s';
  }

  function setSound(on, { silent = false, remember = true } = {}) {
    const ok = audio.enable(on);
    if (!ok) {
      toast('Audio is not available in this browser');
      return;
    }
    wantsSound = on;
    if (remember) {
      store.set('sound', on);
      store.set('soundChosen', true);
      soundChosen = true;
    }
    paintAudio();
    if (on) {
      audio.click();
      if (!silent) toast('Sound on · everything you hear is synthesised');
    } else if (!silent) {
      toast('Sound off');
    }
    if (!on) {
      toneOn = false;
      clearTimeout(toneTimer);
      audio.stopAmbient();
    } else if (sessionApp && !session.hidden) {
      audio.startAmbient();
    }
    paintTone();
  }

  soundBtn.addEventListener('click', () => setSound(!audio.on));
  audioState.addEventListener('click', () => setSound(!audio.on));

  function silenceTone() {
    toneOn = false;
    clearTimeout(toneTimer);
    if (!sessionApp) audio.stopAmbient();
    paintTone();
  }

  tonePlay.addEventListener('click', () => {
    if (toneOn) {
      silenceTone();
      audio.click();
      toast('Room tone off');
      return;
    }
    if (!audio.on) setSound(true, { silent: true });
    audio.startAmbient();
    audio.tick();
    toneOn = true;
    paintTone();
    toast('Room tone · an eight second preview');
    clearTimeout(toneTimer);
    toneTimer = setTimeout(() => {
      if (sessionApp) return;
      silenceTone();
    }, 8000);
  });

  function hoverSound() {
    if (!audio.on) return;
    const now = performance.now();
    if (now - lastHoverAt < 55) return;
    lastHoverAt = now;
    audio.hover();
  }

  // The browser needs a gesture before audio may start.
  function armFirstGesture() {
    const start = () => {
      if (wantsSound && !audio.on) setSound(true, { silent: true });
      window.removeEventListener('pointerdown', start);
      window.removeEventListener('keydown', start);
    };
    window.addEventListener('pointerdown', start, { once: true });
    window.addEventListener('keydown', start, { once: true });
  }

  // Web audio levels animate the footer waveform.
  const waveBars = $$('.wave i');
  function animateWave(now) {
    const active = audio.on;
    const level = active ? audio.level() : 0;
    waveBars.forEach((bar, index) => {
      const idle = 3 + Math.abs(Math.sin(now / 620 + index * 0.9)) * 1.5;
      const height = active ? Math.max(4, Math.min(13, 4 + level * 170 + Math.abs(Math.sin(now / 300 + index)) * 6)) : idle;
      bar.style.height = `${height}px`;
    });
    requestAnimationFrame(animateWave);
  }
  requestAnimationFrame(animateWave);

  /* Theme ------------------------------------------------------------------ */

  function setTheme(theme, announce = false) {
    html.dataset.theme = theme;
    const lamp = theme === 'lamp';
    lampBtn.setAttribute('aria-pressed', String(lamp));
    lampBtn.innerHTML = icon(lamp ? 'i-sun' : 'i-moon');
    store.set('theme', theme);
    if (announce) toast(lamp ? 'Lamp lit — warm paper mode' : 'Lamp dimmed');
  }

  lampBtn.addEventListener('click', () => {
    audio.toggle();
    setTheme(html.dataset.theme === 'lamp' ? 'night' : 'lamp', true);
  });

  setTheme(store.get('theme', 'night'));

  /* Clock ------------------------------------------------------------------ */

  function paintClock() {
    const now = new Date();
    clock.textContent = `${pad2(now.getHours())}:${pad2(now.getMinutes())}`;
  }
  paintClock();
  setInterval(paintClock, 20000);

  /* Featured --------------------------------------------------------------- */

  function buildSlides() {
    stageArtInner.innerHTML = FEATURED.map((id) => {
      const app = byId(id);
      return `
        <div class="slide" data-id="${app.id}">
          <div class="slide-glow" style="--accent:${app.accent}"></div>
          <img src="${app.art}" alt="${app.title} key art">
          <div class="slide-shade"></div>
        </div>`;
    }).join('');

    featThumbs.innerHTML = FEATURED.map((id, index) => {
      const app = byId(id);
      return `
        <button class="thumb" data-index="${index}" role="tab" aria-selected="false"
                aria-label="${app.title}">
          <img src="${app.art}" alt="">
        </button>`;
    }).join('');
  }

  function paintFeatured(index, { silent = false } = {}) {
    featuredIndex = (index + FEATURED.length) % FEATURED.length;
    const app = byId(FEATURED[featuredIndex]);

    paintAccent(stage, app.accent);
    stageInfo.dataset.index = app.index;
    featIndex.textContent = app.index;
    featKicker.textContent = KICKERS[featuredIndex] || KICKERS[0];
    featTitle.innerHTML = `${app.titleMain}${app.titleAccent ? ` <em>${app.titleAccent}</em>` : ''}`;
    featChips.innerHTML = [
      app.genre,
      `rated ${app.rating}`,
      app.size,
      `${app.playtime} played`,
    ].map((text) => `<span class="chip">${text}</span>`).join('');
    featBlurb.textContent = app.blurb;
    featCaption.innerHTML = `<span>key art ${app.index}</span><span>540 × 891</span>`;

    const liked = likedApps.has(app.id);
    featLike.setAttribute('aria-pressed', String(liked));
    featLikeCount.textContent = String(app.likes + (liked ? 1 : 0));

    $$('.slide', stageArtInner).forEach((slide) => {
      slide.classList.toggle('is-active', slide.dataset.id === app.id);
    });
    $$('.thumb', featThumbs).forEach((thumb, thumbIndex) => {
      const active = thumbIndex === featuredIndex;
      thumb.classList.toggle('is-active', active);
      thumb.setAttribute('aria-selected', String(active));
      thumb.tabIndex = active ? 0 : -1;
    });

    if (!reduceMotion) {
      [featTitle, featChips, featBlurb].forEach((element, order) => {
        element.animate(
          [{ opacity: 0, transform: 'translateY(10px)' }, { opacity: 1, transform: 'none' }],
          { duration: 520, delay: order * 60, easing: 'cubic-bezier(.2,.7,.2,1)', fill: 'both' },
        );
      });
    }

    restartProgress();
    if (!silent) {
      audio.tick();
      startRotation();
    }
  }

  function restartProgress() {
    if (!stageProgress) return;
    stageProgress.style.animation = 'none';
    void stageProgress.offsetWidth;
    stageProgress.style.animation = '';
  }

  function startRotation() {
    stopRotation();
    if (reduceMotion) return;
    rotateTimer = setInterval(() => {
      if (hoverPause || currentOverlay || document.hidden) return;
      paintFeatured(featuredIndex + 1, { silent: true });
    }, ROTATE_MS);
  }
  function stopRotation() {
    if (rotateTimer) clearInterval(rotateTimer);
    rotateTimer = null;
  }

  featThumbs.addEventListener('click', (event) => {
    const thumb = event.target.closest('.thumb');
    if (!thumb) return;
    paintFeatured(Number(thumb.dataset.index));
  });

  featThumbs.addEventListener('mouseover', (event) => {
    if (event.target.closest('.thumb')) hoverSound();
  });

  stage.addEventListener('pointerenter', () => { hoverPause = true; });
  stage.addEventListener('pointerleave', () => {
    hoverPause = false;
    stageArtInner.style.setProperty('--px', '0px');
    stageArtInner.style.setProperty('--py', '0px');
  });

  stage.addEventListener('pointermove', (event) => {
    if (reduceMotion || event.pointerType === 'touch') return;
    const rect = stage.getBoundingClientRect();
    const x = (event.clientX - rect.left) / rect.width - 0.5;
    const y = (event.clientY - rect.top) / rect.height - 0.5;
    stageArtInner.style.setProperty('--px', `${(-x * 14).toFixed(1)}px`);
    stageArtInner.style.setProperty('--py', `${(-y * 12).toFixed(1)}px`);
  });

  featLike.addEventListener('click', () => {
    const app = byId(FEATURED[featuredIndex]);
    const liked = likedApps.has(app.id);
    if (liked) likedApps.delete(app.id); else likedApps.add(app.id);
    store.set('likedApps', [...likedApps]);
    featLike.setAttribute('aria-pressed', String(!liked));
    featLikeCount.textContent = String(app.likes + (liked ? 0 : 1));
    pop(featLike);
    if (liked) audio.click(); else audio.chime();
    toast(liked ? 'Removed from favourites' : `Saved ${app.title}`);
  });

  featPlay.addEventListener('click', () => play(portalApp(FEATURED[featuredIndex])));
  featDetails.addEventListener('click', () => openSheet(byId(FEATURED[featuredIndex])));

  // Kept for clarity when handing data around.
  function portalApp(id) { return byId(id); }

  /* Shelf ------------------------------------------------------------------ */

  function cardMarkup(app) {
    return `
      <article class="card" role="button" tabindex="0" data-app="${app.id}"
               style="--accent:${app.accent}" aria-label="Open ${app.title}">
        <div class="card-art">
          ${artMarkup(app, false)}
          <span class="card-index mono">${app.index}</span>
          <span class="card-play">${icon('i-play')}</span>
          <span class="card-accent" style="--accent:${app.accent}"></span>
        </div>
        <div class="card-body">
          <h3>${app.title}</h3>
          <p class="card-sub">${app.studio} · ${app.genre}</p>
          <div class="card-foot">
            <span>${STAR} ${app.rating}</span>
            <span>${app.playtime}</span>
          </div>
        </div>
      </article>`;
  }

  function renderShelf() {
    const items = APPS.filter((app) => activeFilter === 'all' || app.category === activeFilter);
    shelf.style.setProperty('--accent', byId(FEATURED[featuredIndex]).accent);
    shelf.innerHTML = items.map((app, index) => {
      const markup = cardMarkup(app);
      return markup.replace('<article class="card"', `<article class="card" data-order="${index}"`)
        .replace('style="--accent', `style="animation-delay:${index * 45}ms;--accent`);
    }).join('');
    shelfEmpty.hidden = items.length > 0;
  }

  function setFilter(next, { scroll = false } = {}) {
    activeFilter = next;
    $$('.filter', filters).forEach((button) => {
      const active = button.dataset.filter === next;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-selected', String(active));
    });
    renderShelf();
    audio.click();
    if (scroll) {
      $('#library').scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
    }
  }

  filters.addEventListener('click', (event) => {
    const button = event.target.closest('.filter');
    if (!button) return;
    setFilter(button.dataset.filter);
  });

  viewToggle.addEventListener('click', (event) => {
    const button = event.target.closest('button');
    if (!button) return;
    const view = button.dataset.view;
    shelf.dataset.view = view;
    $$('button', viewToggle).forEach((item) => {
      const active = item === button;
      item.classList.toggle('is-active', active);
      item.setAttribute('aria-pressed', String(active));
    });
    audio.tick();
  });

  shelf.addEventListener('pointerover', (event) => {
    if (event.target.closest('.card')) hoverSound();
  });

  shelf.addEventListener('click', (event) => {
    const card = event.target.closest('.card');
    if (card) openSheet(byId(card.dataset.app));
  });

  shelf.addEventListener('keydown', (event) => {
    const card = event.target.closest('.card');
    if (!card) return;
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      openSheet(byId(card.dataset.app));
    }
  });

  /* Weekly stats ----------------------------------------------------------- */

  function renderStats() {
    segbar.innerHTML = WEEK.map((entry) => {
      const app = byId(entry.id);
      return `<span class="seg" style="--w:${entry.pct};--c:${entry.color}" title="${app.title} · ${entry.pct}%">
        <span class="seg-fill"></span>
      </span>`;
    }).join('');

    seglist.innerHTML = WEEK.map((entry) => {
      const app = byId(entry.id);
      return `<li><span class="swatch" style="--c:${entry.color}"></span>
        <span class="name">${app.title}</span>
        <span class="pct">${entry.pct}%</span></li>`;
    }).join('');
  }

  /* Updates ---------------------------------------------------------------- */

  function renderUpdates() {
    timeline.innerHTML = UPDATES.map((item) => {
      const liked = likedUpdates.has(item.id);
      return `
        <li data-state="${item.state}">
          <p class="t-title">${item.title}</p>
          <p class="t-desc">${item.desc}</p>
          <div class="t-meta">
            <span class="badge ${item.state === 'planned' ? 'planned' : ''}">${item.state === 'planned' ? 'planned' : 'in progress'}</span>
            <button class="t-like" data-update="${item.id}" aria-pressed="${liked}"
                    aria-label="Support ${item.title}">
              ${icon('i-heart')}<span>${item.likes + (liked ? 1 : 0)}</span>
            </button>
          </div>
        </li>`;
    }).join('');
  }

  timeline.addEventListener('click', (event) => {
    const button = event.target.closest('.t-like');
    if (!button) return;
    const item = UPDATES.find((entry) => entry.id === button.dataset.update);
    if (!item) return;
    const liked = likedUpdates.has(item.id);
    if (liked) likedUpdates.delete(item.id); else likedUpdates.add(item.id);
    store.set('likedUpdates', [...likedUpdates]);
    button.setAttribute('aria-pressed', String(!liked));
    button.querySelector('span').textContent = String(item.likes + (liked ? 0 : 1));
    pop(button);
    if (liked) audio.click(); else audio.chime();
    toast(liked ? 'Support withdrawn' : 'Support counted — thank you');
  });

  $$('[data-toast]').forEach((element) => {
    element.addEventListener('click', () => {
      audio.click();
      toast(element.dataset.toast);
    });
  });

  /* Layers ----------------------------------------------------------------- */

  const layerFor = { sheet, launch, palette };

  function syncScrollLock() {
    const open = Object.values(layerFor).some((layer) => !layer.hidden);
    body.style.overflow = open ? 'hidden' : '';
  }

  function openLayer(name) {
    const layer = layerFor[name];
    if (!layer) return;
    clearTimeout(layer._hideTimer);
    layer.hidden = false;
    requestAnimationFrame(() => layer.classList.add('is-open'));
    currentOverlay = name;
    syncScrollLock();
  }

  function closeLayer(name, { instant = false } = {}) {
    const layer = layerFor[name];
    if (!layer || layer.hidden) return;
    layer.classList.remove('is-open');
    clearTimeout(layer._hideTimer);
    if (instant) {
      layer.hidden = true;
      syncScrollLock();
      if (currentOverlay === name) currentOverlay = null;
      return;
    }
    const wait = reduceMotion ? 0 : 380;
    layer._hideTimer = setTimeout(() => {
      layer.hidden = true;
      syncScrollLock();
      if (currentOverlay === name) currentOverlay = null;
    }, wait);
  }

  const layerIsOpen = (element) => Boolean(element && !element.hidden && element.classList.contains('is-open'));

  function closeAllLayers() {
    ['sheet', 'palette'].forEach(closeLayer);
  }

  /* Detail sheet ----------------------------------------------------------- */

  function openSheet(app) {
    if (!app) return;
    audio.click();
    sheetApp = app;
    sheetArt.innerHTML = app.art
      ? `<div class="sheet-art-bg" style="background-image:url('${app.art}')" aria-hidden="true"></div>
         <img src="${app.art}" alt="${app.title} key art">`
      : artMarkup(app, false);
    sheetArt.style.setProperty('--accent', app.accent);
    sheetKicker.textContent = `${app.index} · ${app.genre}`;
    sheetTitle.textContent = app.title;
    sheetBlurb.textContent = app.blurb;
    sheetChips.innerHTML = app.chips.map((text) => `<span class="chip">${text}</span>`).join('');
    sheetStudio.textContent = app.studio;
    sheetPlayed.textContent = app.played;
    sheetSize.textContent = app.size;
    sheetRating.textContent = `${app.rating} / 5`;
    paintAccent(sheet, app.accent);
    sheetArt.style.setProperty('--accent', app.accent);
    openLayer('sheet');
    setTimeout(() => $('.sheet-card').focus({ preventScroll: true }), 60);
  }

  $$('[data-close="sheet"]').forEach((element) => {
    element.addEventListener('click', () => closeLayer('sheet'));
  });

  sheetPlay.addEventListener('click', () => {
    if (sheetApp) play(sheetApp);
  });

  /* Launch and session ----------------------------------------------------- */

  const LAUNCH_STEPS = ['checking local files', 'warming the room', 'restoring last save', 'setting the table'];

  function play(app) {
    if (!app) return;
    closeAllLayers();
    launchToken += 1;
    const token = launchToken;
    clearTimeout(launch._hideTimer);
    sessionApp = app;
    sessionSeconds = 0;
    sessionPaused = false;

    // The play gesture is a good moment to switch the room's sound on,
    // unless the visitor has already made an explicit choice.
    if (!audio.on && !soundChosen) {
      setSound(true, { silent: true });
      toast('Sound on · synthesised room tone');
    }
    if (audio.on) {
      audio.click();
      setTimeout(() => audio.whoosh(0.9), 60);
    }

    paintAccent(launch, app.accent);
    launchBackdrop.style.backgroundImage = app.art
      ? `url("${app.art}")`
      : `radial-gradient(circle at 52% 38%, ${app.accent}, #0a0807 72%)`;
    launchTitle.textContent = app.title;
    launchLabel.textContent = 'preparing the room';
    launchStudio.textContent = `${app.studio} · ${app.genre}`;
    launchMark.style.backgroundImage = app.art
      ? `url("${app.art}")`
      : `radial-gradient(circle at 50% 40%, ${app.accent}, #14100d 78%)`;
    launchMark.style.setProperty('--accent', app.accent);
    launchStatus.textContent = LAUNCH_STEPS[0];
    launchBar.style.transition = 'none';
    launchBar.style.width = '0%';
    session.hidden = true;
    session.classList.remove('is-on', 'is-paused');
    launchPhase.style.display = '';
    launch.classList.remove('is-running');
    launch.hidden = false;
    syncScrollLock();
    currentOverlay = 'launch';
    requestAnimationFrame(() => {
      launch.classList.add('is-open');
      requestAnimationFrame(() => {
        launchBar.style.transition = 'width 2.35s cubic-bezier(.35,0,.2,1)';
        launchBar.style.width = '100%';
      });
    });

    let step = 0;
    const stepTimer = setInterval(() => {
      if (token !== launchToken) { clearInterval(stepTimer); return; }
      step = (step + 1) % LAUNCH_STEPS.length;
      launchStatus.textContent = LAUNCH_STEPS[step];
      if (audio.on) audio.tick();
    }, 560);

    setTimeout(() => {
      clearInterval(stepTimer);
      if (token !== launchToken) return;
      startSession(app);
    }, 2450);
  }

  function startSession(app) {
    launch.classList.add('is-running');
    launchPhase.style.display = 'none';
    session.hidden = false;
    sessionTitle.textContent = app.title;
    sessionArt.innerHTML = artMarkup(app, false);
    sessionNote.textContent = app.note || 'Take your time. Hearth is listening.';
    sessionTimer.textContent = '00:00';
    statusSession.textContent = 'live · 00:00';
    statusSession.classList.add('is-live');

    requestAnimationFrame(() => {
      session.classList.add('is-on');
    });

    sessionSeconds = 0;
    sessionPaused = false;
    toneOn = false;
    clearTimeout(toneTimer);
    paintTone();
    sessionPause.innerHTML = icon('i-pause');
    sessionPause.setAttribute('aria-label', 'Pause session');
    sessionLiveText.textContent = 'live';
    if (audio.on) {
      audio.startAmbient();
      audio.chord();
    }
    clearInterval(sessionTimerId);
    sessionTimerId = setInterval(() => {
      if (sessionPaused) return;
      sessionSeconds += 1;
      const value = fmt(sessionSeconds);
      sessionTimer.textContent = value;
      statusSession.textContent = `live · ${value}`;
    }, 1000);
  }

  function togglePause() {
    if (!sessionApp || session.hidden) return;
    sessionPaused = !sessionPaused;
    session.classList.toggle('is-paused', sessionPaused);
    sessionLiveText.textContent = sessionPaused ? 'paused' : 'live';
    sessionPause.innerHTML = icon(sessionPaused ? 'i-play' : 'i-pause');
    sessionPause.setAttribute('aria-label', sessionPaused ? 'Resume session' : 'Pause session');
    audio.duck(sessionPaused);
    if (!sessionPaused) audio.tick();
    statusSession.textContent = sessionPaused ? 'paused' : `live · ${fmt(sessionSeconds)}`;
    statusSession.classList.toggle('is-live', !sessionPaused);
  }

  function endSession({ cancelled = false } = {}) {
    if (!sessionApp) return;
    const app = sessionApp;
    const elapsed = sessionSeconds;
    clearInterval(sessionTimerId);
    sessionTimerId = null;
    audio.stopAmbient();
    audio.click();
    session.classList.remove('is-on', 'is-paused');
    launch.classList.remove('is-open', 'is-running');
    launchToken += 1;
    clearTimeout(launch._hideTimer);
    launch._hideTimer = setTimeout(() => {
      launch.hidden = true;
      session.hidden = true;
      sessionApp = null;
      currentOverlay = null;
      syncScrollLock();
    }, reduceMotion ? 0 : 500);
    statusSession.classList.remove('is-live');
    statusSession.textContent = 'idle';
    toneOn = false;
    paintTone();
    if (!cancelled) {
      toast(elapsed > 0 ? `Session saved — ${fmt(elapsed)} with ${app.title}` : `Closed ${app.title}`);
      if (elapsed > 0) {
        const hours = Number($('#statHours').textContent) || 0;
        const mins = Number($('#statMins').textContent) || 0;
        const total = hours * 60 + mins + Math.max(1, Math.round(elapsed / 60));
        $('#statHours').textContent = String(Math.floor(total / 60));
        $('#statMins').textContent = String(total % 60);
        setTimeout(() => toast('This week has grown by a minute.'), 3400);
      }
    }
  }

  function cancelLaunch() {
    if (sessionApp && !session.hidden) {
      endSession({ cancelled: true });
      return;
    }
    launchToken += 1;
    launch.classList.remove('is-open', 'is-running');
    audio.stopAmbient();
    clearTimeout(launch._hideTimer);
    launch._hideTimer = setTimeout(() => {
      launch.hidden = true;
      session.hidden = true;
      sessionApp = null;
      currentOverlay = null;
      syncScrollLock();
    }, reduceMotion ? 0 : 380);
    statusSession.textContent = 'idle';
    statusSession.classList.remove('is-live');
    toast('Launch cancelled');
  }

  launchCancel.addEventListener('click', cancelLaunch);
  sessionPause.addEventListener('click', togglePause);
  sessionEnd.addEventListener('click', () => endSession());
  sessionHeart.addEventListener('click', () => {
    pop(sessionHeart);
    audio.chime();
    sessionHeart.style.color = 'var(--ember)';
    sessionHeart.querySelector('svg').style.fill = 'currentColor';
    toast('Highlight saved to this session');
  });
  sessionSound.addEventListener('click', () => setSound(!audio.on));

  /* Command palette -------------------------------------------------------- */

  function scoreApp(app, query) {
    if (!query) return 1;
    const haystack = `${app.title} ${app.studio} ${app.genre} ${app.category}`.toLowerCase();
    const terms = query.toLowerCase().split(/\s+/).filter(Boolean);
    let score = 0;
    for (const term of terms) {
      const at = haystack.indexOf(term);
      if (at === -1) return 0;
      score += app.title.toLowerCase().startsWith(term) ? 6 : (at < 14 ? 3 : 1);
    }
    return score;
  }

  function renderPalette(query = '') {
    paletteMatches = APPS
      .map((app) => ({ app, score: scoreApp(app, query) }))
      .filter((entry) => entry.score > 0)
      .sort((a, b) => b.score - a.score)
      .map((entry) => entry.app)
      .slice(0, 7);

    paletteActive = Math.min(paletteActive, Math.max(paletteMatches.length - 1, 0));

    if (!paletteMatches.length) {
      paletteResults.innerHTML = `<li class="palette-empty"><span class="mono">no matches</span>Nothing on the shelf answers to “${query.replace(/[<>&]/g, '')}”.</li>`;
      return;
    }

    paletteResults.innerHTML = paletteMatches.map((app, index) => `
      <li class="result ${index === paletteActive ? 'is-active' : ''}" role="option"
          aria-selected="${index === paletteActive}" data-app="${app.id}">
        <span class="result-thumb">${artMarkup(app, false)}</span>
        <span class="result-body">
          <span class="result-name">${app.title}</span>
          <span class="result-meta">${app.studio} · ${app.genre}</span>
        </span>
        <span class="result-go">open ${icon('i-arrow')}</span>
      </li>`).join('');

    $$('.result', paletteResults).forEach((row) => {
      row.addEventListener('pointerenter', () => {
        paletteActive = Number(paletteMatches.findIndex((app) => app.id === row.dataset.app));
        $$('.result', paletteResults).forEach((item, index) => {
          item.classList.toggle('is-active', index === paletteActive);
          item.setAttribute('aria-selected', String(index === paletteActive));
        });
        hoverSound();
      });
      row.addEventListener('click', () => {
        closeLayer('palette', { instant: true });
        openSheet(byId(row.dataset.app));
      });
    });
  }

  function openPalette() {
    if (!launch.hidden) return;
    paletteActive = 0;
    paletteInput.value = '';
    renderPalette('');
    openLayer('palette');
    setTimeout(() => paletteInput.focus({ preventScroll: true }), 80);
    audio.tick();
  }

  $('#searchTrigger').addEventListener('click', openPalette);
  paletteInput.addEventListener('input', () => {
    paletteActive = 0;
    renderPalette(paletteInput.value.trim());
  });

  paletteInput.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();
      if (!paletteMatches.length) return;
      paletteActive = (paletteActive + (event.key === 'ArrowDown' ? 1 : -1) + paletteMatches.length) % paletteMatches.length;
      renderPalette(paletteInput.value.trim());
      const active = $('.result.is-active', paletteResults);
      active?.scrollIntoView({ block: 'nearest' });
      audio.tick();
    }
    if (event.key === 'Enter') {
      const app = paletteMatches[paletteActive];
      if (!app) return;
      closeLayer('palette', { instant: true });
      openSheet(app);
    }
  });

  $$('[data-close="palette"]').forEach((element) => {
    element.addEventListener('click', () => closeLayer('palette'));
  });

  /* Navigation ------------------------------------------------------------- */

  $$('.nav-item').forEach((button) => {
    button.addEventListener('click', () => {
      const target = button.dataset.nav;
      $$('.nav-item').forEach((item) => item.classList.toggle('is-active', item === button));
      if (target === 'updates') {
        $('#updatesPanel').scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'center' });
        audio.tick();
        return;
      }
      const filter = target === 'studio' ? 'studio' : target === 'work' ? 'work' : 'all';
      setFilter(filter, { scroll: true });
    });
  });

  /* Cursor glow ------------------------------------------------------------ */

  let glowFrame = null;
  window.addEventListener('pointermove', (event) => {
    if (reduceMotion || event.pointerType === 'touch') return;
    if (glowFrame) return;
    glowFrame = requestAnimationFrame(() => {
      glowFrame = null;
      cursorGlow.style.opacity = '1';
      cursorGlow.style.left = `${event.clientX}px`;
      cursorGlow.style.top = `${event.clientY}px`;
    });
  });
  window.addEventListener('pointerleave', () => { cursorGlow.style.opacity = '0'; });

  /* Global keys ------------------------------------------------------------ */

  document.addEventListener('keydown', (event) => {
    const typing = /^(INPUT|TEXTAREA)$/.test(event.target.tagName) || event.target.isContentEditable;
    const key = event.key.toLowerCase();

    if ((event.metaKey || event.ctrlKey) && key === 'k') {
      event.preventDefault();
      if (palette.hidden) openPalette(); else closeLayer('palette');
      return;
    }

    if (event.key === 'Escape') {
      if (layerIsOpen(palette)) { closeLayer('palette'); return; }
      if (layerIsOpen(sheet)) { closeLayer('sheet'); return; }
      if (!launch.hidden) { cancelLaunch(); return; }
      return;
    }

    if (typing) return;
    if (!palette.hidden || !sheet.hidden || !launch.hidden) return;

    if (key === 'l') { setTheme(html.dataset.theme === 'lamp' ? 'night' : 'lamp', true); audio.toggle(); }
    if (key === 'm') { setSound(!audio.on); }
    if (event.key === 'ArrowRight') { event.preventDefault(); paintFeatured(featuredIndex + 1); }
    if (event.key === 'ArrowLeft') { event.preventDefault(); paintFeatured(featuredIndex - 1); }
    if (event.key === 'Enter' && (document.activeElement === body || document.activeElement === document.documentElement)) {
      play(byId(FEATURED[featuredIndex]));
    }
    if (key === '/') {
      event.preventDefault();
      openPalette();
    }
  });

  /* Boot ------------------------------------------------------------------- */

  function finishBoot() {
    if (booted) return;
    booted = true;
    boot.classList.add('is-done');
    body.classList.add('is-ready');
    startRotation();
    if (!store.get('visited', false)) {
      store.set('visited', true);
      setTimeout(() => toast(wantsSound ? 'Sound is ready when you are' : 'Press M for room tone'), 700);
    }
  }

  $('#bootSkip').addEventListener('click', finishBoot);
  boot.addEventListener('click', (event) => {
    if (event.target === boot) finishBoot();
  });
  setTimeout(finishBoot, 1550);

  /* Init ------------------------------------------------------------------- */

  buildSlides();
  paintFeatured(0, { silent: true });
  renderShelf();
  renderStats();
  renderUpdates();
  paintAudio();
  armFirstGesture();
  startRotation();

  document.addEventListener('visibilitychange', () => {
    if (document.hidden) stopRotation();
    else startRotation();
  });
})();
