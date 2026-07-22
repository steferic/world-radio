// app.js — Browser glue around the portable renderer.
//
// Everything browser-specific lives here: the canvas, the 1-bit display
// pipeline, animation, input, audio, and the e-ink BEHAVIOR SIMULATION
// (partial-vs-full refresh + ghosting). None of this goes on the chip — on the
// ESP32 this is replaced by the e-ink driver, a rotary encoder, and I2S audio.
// projection.js stays the same on both sides.
//
// Classic script. Depends on globals loaded before it in index.html:
//   window.Projection, window.STATIONS, window.COASTLINE, window.LAND

(function () {
  'use strict';

  const P = window.Projection;
  const CURATED = window.STATIONS || [];
  const DATA = { coast: window.COASTLINE || [], land: window.LAND || [] };

  const PANELS = [
    { id: '4.2', label: '4.2" — 400×300', w: 400, h: 300, scale: 2 },
    { id: '1.54', label: '1.54" — 200×200 (square)', w: 200, h: 200, scale: 4 },
    { id: '2.9', label: '2.9" — 296×128', w: 296, h: 128, scale: 3 },
    { id: '7.5', label: '7.5" — 800×480', w: 800, h: 480, scale: 1 },
  ];

  const STYLES = [
    { id: 'filled', label: 'Filled land (e-ink friendly)' },
    { id: 'gray', label: 'Gray land (dithered)' },
    { id: 'wire', label: 'Wireframe (cleanest)' },
    { id: 'shaded', label: 'Shaded sphere (ghosts!)' },
  ];

  // Ghosting model tuning (out of 255). A partial refresh leaves a residual mark
  // where a pixel went black->white; it saturates after a couple of refreshes
  // and only a FULL refresh clears it.
  const GHOST_ADD = 70;
  const GHOST_MAX = 115;
  const FULL_EVERY = 6; // auto full-refresh cadence in e-ink mode

  const state = {
    stations: CURATED.slice(),
    selected: 0,
    panel: PANELS[0],
    style: 'filled',
    curLon: CURATED[0] ? CURATED[0].lon : 0,
    curLat: CURATED[0] ? CURATED[0].lat : 20,
    targetLon: CURATED[0] ? CURATED[0].lon : 0,
    targetLat: CURATED[0] ? CURATED[0].lat : 20,
    graticule: true,
    einkSim: false,
    animating: false,
    playing: false,
    // ghosting state (allocated per panel)
    residual: null,
    prevTarget: null,
    lastTarget: null,
    selScreen: null,
    refreshCount: 0,
    forceFull: false,
  };

  let view, fb, natCanvas, natCtx, screenCanvas, screenCtx, audio;

  // -------------------------------------------------------------------------
  // Panel config
  // -------------------------------------------------------------------------

  function configurePanel(panel) {
    state.panel = panel;
    view = new P.GlobeView(panel.w, panel.h);
    fb = P.makeFramebuffer(panel.w, panel.h);
    natCanvas.width = panel.w;
    natCanvas.height = panel.h;
    screenCanvas.width = panel.w * panel.scale;
    screenCanvas.height = panel.h * panel.scale;
    screenCtx.imageSmoothingEnabled = false;
    const n = panel.w * panel.h;
    state.residual = new Float32Array(n);
    state.prevTarget = new Uint8Array(n).fill(255);
    state.lastTarget = new Uint8Array(n).fill(255);
  }

  // -------------------------------------------------------------------------
  // Chrome (header, status bar, on-globe label) — drawn antialiased, then
  // thresholded with the rest of the frame so even the text is 1-bit.
  // -------------------------------------------------------------------------

  function truncate(text, w, fontPx) {
    const maxChars = Math.floor((w - 12) / (fontPx * 0.62));
    return text.length > maxChars ? text.slice(0, maxChars - 1) + '…' : text;
  }

  function paintChrome() {
    const w = state.panel.w;
    const h = state.panel.h;
    const st = state.stations[state.selected] || { name: '—', city: '', country: '', genre: '' };
    const small = Math.max(9, Math.round(h * 0.05));

    natCtx.textBaseline = 'alphabetic';

    // On-globe label for the selected station (white plate + black text so it
    // reads over solid land or ocean).
    if (state.selScreen) {
      const lblFont = Math.max(8, small - 1);
      natCtx.font = 'bold ' + lblFont + 'px ui-monospace, monospace';
      const label = st.name;
      const tw = natCtx.measureText(label).width;
      let lx = (state.selScreen.x | 0) + 9;
      let ly = (state.selScreen.y | 0) - 9;
      if (lx + tw + 6 > w) lx = (state.selScreen.x | 0) - 9 - tw - 6;
      if (lx < 2) lx = 2;
      if (ly < small + lblFont) ly = (state.selScreen.y | 0) + lblFont + 10;
      natCtx.fillStyle = '#fff';
      natCtx.fillRect(lx - 3, ly - lblFont, tw + 6, lblFont + 5);
      natCtx.fillStyle = '#000';
      natCtx.fillRect(lx - 3, ly - lblFont, tw + 6, 1);
      natCtx.fillRect(lx - 3, ly + 4, tw + 6, 1);
      natCtx.textAlign = 'left';
      natCtx.fillText(label, lx, ly);
    }

    natCtx.fillStyle = '#000';
    natCtx.font = 'bold ' + small + 'px ui-monospace, monospace';
    natCtx.textAlign = 'left';
    natCtx.fillText('WORLD  RADIO', 6, small + 2);
    natCtx.textAlign = 'right';
    natCtx.fillText((state.selected + 1) + '/' + state.stations.length, w - 6, small + 2);
    natCtx.textAlign = 'left';

    const barY = h - small * 2 - 8;
    natCtx.fillStyle = '#fff';
    natCtx.fillRect(0, barY + 1, w, h - barY); // clean plate behind status text
    natCtx.fillStyle = '#000';
    natCtx.fillRect(0, barY, w, 1);
    natCtx.font = 'bold ' + small + 'px ui-monospace, monospace';
    natCtx.fillText(truncate(st.name, w, small), 6, barY + small + 2);
    natCtx.font = (small - 1) + 'px ui-monospace, monospace';
    const sub = st.city + ' · ' + st.country + (st.genre ? '  ·  ' + st.genre : '');
    natCtx.fillText(truncate(sub, w, small - 1), 6, barY + small * 2 + 4);
    natCtx.textAlign = 'right';
    natCtx.fillText(state.playing ? '▶ LIVE' : '■ IDLE', w - 6, barY + small + 2);
    natCtx.textAlign = 'left';
  }

  // -------------------------------------------------------------------------
  // Display pipeline
  // -------------------------------------------------------------------------

  // Render fb + chrome and return the clean 1-bit target (Uint8Array, 0|255).
  function computeTarget() {
    const w = state.panel.w;
    const h = state.panel.h;
    const img = natCtx.createImageData(w, h);
    for (let i = 0; i < fb.px.length; i++) {
      const v = fb.px[i] ? 0 : 255;
      img.data[i * 4] = v; img.data[i * 4 + 1] = v; img.data[i * 4 + 2] = v; img.data[i * 4 + 3] = 255;
    }
    natCtx.putImageData(img, 0, 0);
    paintChrome();
    const after = natCtx.getImageData(0, 0, w, h);
    const d = after.data;
    const target = new Uint8Array(w * h);
    for (let i = 0, p = 0; i < d.length; i += 4, p++) {
      const lum = 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
      target[p] = lum < 128 ? 0 : 255;
    }
    return target;
  }

  // Draw target (+ accumulated ghost residual) to the visible canvas.
  function paintDisplay(target) {
    const w = state.panel.w;
    const h = state.panel.h;
    const res = state.residual;
    const out = natCtx.createImageData(w, h);
    const d = out.data;
    for (let p = 0, i = 0; p < target.length; p++, i += 4) {
      let v;
      if (target[p] === 0) v = 0;
      else { v = 255 - res[p]; if (v < 0) v = 0; }
      d[i] = v; d[i + 1] = v; d[i + 2] = v; d[i + 3] = 255;
    }
    natCtx.putImageData(out, 0, 0);
    screenCtx.imageSmoothingEnabled = false;
    screenCtx.drawImage(natCanvas, 0, 0, screenCanvas.width, screenCanvas.height);
  }

  // Render one frame. fast=true forces cheap wireframe (used during motion).
  function renderFrame(fast) {
    const center = P.makeCenter(state.curLon, state.curLat);
    const style = fast ? 'wire' : state.style;
    P.renderGlobe(fb, view, center, DATA, { style: style, graticule: state.graticule });
    state.selScreen = null;
    for (let i = 0; i < state.stations.length; i++) {
      const p = P.drawMarker(fb, view, center, state.stations[i], i === state.selected);
      if (i === state.selected) state.selScreen = p;
    }
    const target = computeTarget();
    state.lastTarget = target;
    paintDisplay(target);
    return target;
  }

  // -------------------------------------------------------------------------
  // E-ink behavior simulation (partial vs full refresh + ghosting)
  // -------------------------------------------------------------------------

  function updateGhost(full) {
    const res = state.residual;
    if (full) { res.fill(0); return; }
    const prev = state.prevTarget;
    const cur = state.lastTarget;
    for (let i = 0; i < res.length; i++) {
      if (cur[i] === 0) res[i] = 0;                 // freshly inked: clean
      else if (prev[i] === 0) {                     // was black, now white: ghost
        res[i] += GHOST_ADD;
        if (res[i] > GHOST_MAX) res[i] = GHOST_MAX;
      } // else white->white: keep older ghost
    }
  }

  function flash(done) {
    const sw = screenCanvas.width;
    const sh = screenCanvas.height;
    const steps = ['#000', '#fff', '#000', '#fff'];
    let n = 0;
    (function tick() {
      if (n < steps.length) {
        screenCtx.fillStyle = steps[n++];
        screenCtx.fillRect(0, 0, sw, sh);
        setTimeout(tick, 85);
      } else if (done) done();
    })();
  }

  // One discrete e-ink refresh: render final style, update ghosting, flash on a
  // full refresh, then settle.
  function einkRefresh() {
    renderFrame(false);
    state.refreshCount++;
    const full = state.forceFull || (state.refreshCount % FULL_EVERY === 0);
    state.forceFull = false;
    updateGhost(full);
    state.prevTarget = state.lastTarget.slice();
    paintDisplay(state.lastTarget);
    if (full) {
      flash(function () { paintDisplay(state.lastTarget); });
      setStatus('FULL refresh — panel cleared, no ghosting (≈2s on real glass).');
    } else {
      const k = ((state.refreshCount - 1) % FULL_EVERY) + 1;
      setStatus('Partial refresh ' + k + '/' + FULL_EVERY + ' — fast (~0.3s) but ghosting builds. Style: ' + state.style + '.');
    }
  }

  // -------------------------------------------------------------------------
  // Navigation & animation
  // -------------------------------------------------------------------------

  function selectStation(index, opts) {
    opts = opts || {};
    const n = state.stations.length;
    if (!n) return;
    state.selected = ((index % n) + n) % n;
    const st = state.stations[state.selected];
    state.targetLon = st.lon;
    state.targetLat = st.lat;
    updateList();
    if (state.playing) playCurrent();

    if (state.einkSim) {
      state.curLon = st.lon;
      state.curLat = st.lat;
      einkRefresh();
    } else if (opts.snap) {
      state.curLon = st.lon;
      state.curLat = st.lat;
      renderFrame(false);
    } else if (!state.animating) {
      loop();
    }
  }

  function shortestLonDelta(from, to) {
    let d = (to - from) % 360;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
  }

  // Smooth dev preview (non-e-ink): spin in cheap wireframe, settle in full style.
  function loop() {
    state.animating = true;
    const ease = 0.18;
    const dLon = shortestLonDelta(state.curLon, state.targetLon);
    const dLat = state.targetLat - state.curLat;
    if (Math.abs(dLon) < 0.06 && Math.abs(dLat) < 0.06) {
      state.curLon = state.targetLon;
      state.curLat = state.targetLat;
      state.animating = false;
      renderFrame(false);
      return;
    }
    state.curLon += dLon * ease;
    state.curLat += dLat * ease;
    renderFrame(true);
    requestAnimationFrame(loop);
  }

  // -------------------------------------------------------------------------
  // Audio
  // -------------------------------------------------------------------------

  function playCurrent() {
    const st = state.stations[state.selected];
    if (!st || !st.url) {
      state.playing = false;
      setStatus('No stream URL for ' + (st ? st.name : '—') + ' — try "Load live stations".');
      repaintChromeOnly();
      return;
    }
    audio.src = st.url;
    audio.play().then(function () {
      state.playing = true;
      setStatus('Playing ' + st.name + ' (' + st.city + ')');
      repaintChromeOnly();
    }).catch(function (err) {
      state.playing = false;
      setStatus('Could not play ' + st.name + ': ' + err.message);
      repaintChromeOnly();
    });
  }

  // Cheap repaint when only the play/idle indicator changed.
  function repaintChromeOnly() {
    const target = computeTarget();
    state.lastTarget = target;
    paintDisplay(target);
  }

  function togglePlay() {
    if (state.playing) {
      audio.pause();
      state.playing = false;
      setStatus('Stopped.');
      repaintChromeOnly();
    } else {
      playCurrent();
    }
  }

  // -------------------------------------------------------------------------
  // Live stations
  // -------------------------------------------------------------------------

  function loadLiveStations() {
    setStatus('Loading live stations from radio-browser.info…');
    const base = 'https://de1.api.radio-browser.info';
    const url = base + '/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=400';
    fetch(url).then(function (res) { return res.json(); }).then(function (json) {
      const seen = {};
      const live = [];
      for (let i = 0; i < json.length; i++) {
        const s = json[i];
        const lon = parseFloat(s.geo_long);
        const lat = parseFloat(s.geo_lat);
        if (!isFinite(lon) || !isFinite(lat)) continue;
        const key = Math.round(lon) + '_' + Math.round(lat);
        if (seen[key]) continue;
        seen[key] = 1;
        live.push({
          name: (s.name || 'Unknown').trim().slice(0, 40),
          city: s.state || s.country || '',
          country: s.countrycode || '',
          lon: lon, lat: lat,
          genre: (s.tags || '').split(',')[0] || '',
          url: s.url_resolved || s.url || '',
        });
        if (live.length >= 150) break;
      }
      if (!live.length) throw new Error('no geolocated stations returned');
      state.stations = live;
      state.selected = 0;
      buildList();
      selectStation(0, { snap: true });
      setStatus('Loaded ' + live.length + ' live stations.');
    }).catch(function (err) {
      setStatus('Live load failed (' + err.message + ') — keeping curated list.');
    });
  }

  // -------------------------------------------------------------------------
  // Input & UI
  // -------------------------------------------------------------------------

  function onCanvasClick(ev) {
    const rect = screenCanvas.getBoundingClientRect();
    const nx = (ev.clientX - rect.left) / rect.width * state.panel.w;
    const ny = (ev.clientY - rect.top) / rect.height * state.panel.h;
    const center = P.makeCenter(state.curLon, state.curLat);
    let best = -1;
    let bestD = 18 * 18;
    for (let i = 0; i < state.stations.length; i++) {
      const p = view.project(state.stations[i].lon, state.stations[i].lat, center);
      if (!p.front) continue;
      const dx = p.x - nx;
      const dy = p.y - ny;
      const dd = dx * dx + dy * dy;
      if (dd < bestD) { bestD = dd; best = i; }
    }
    if (best >= 0) selectStation(best);
  }

  function setStatus(msg) {
    const el = document.getElementById('status');
    if (el) el.textContent = msg;
  }

  function updateList() {
    const sel = document.querySelector('#list [data-i="' + state.selected + '"]');
    const actives = document.querySelectorAll('#list .row.active');
    for (let i = 0; i < actives.length; i++) actives[i].classList.remove('active');
    if (sel) { sel.classList.add('active'); sel.scrollIntoView({ block: 'nearest' }); }
  }

  function buildList() {
    const list = document.getElementById('list');
    list.innerHTML = '';
    state.stations.forEach(function (s, i) {
      const row = document.createElement('button');
      row.className = 'row' + (i === state.selected ? ' active' : '');
      row.dataset.i = i;
      row.innerHTML = '<span class="nm"></span><span class="ct"></span>';
      row.querySelector('.nm').textContent = s.name;
      row.querySelector('.ct').textContent = s.city + ' · ' + s.country;
      row.addEventListener('click', function () { selectStation(i); });
      list.appendChild(row);
    });
  }

  function redrawCurrent() {
    if (state.einkSim) einkRefresh();
    else renderFrame(false);
  }

  function wireControls() {
    document.getElementById('prev').addEventListener('click', function () { selectStation(state.selected - 1); });
    document.getElementById('next').addEventListener('click', function () { selectStation(state.selected + 1); });
    document.getElementById('play').addEventListener('click', togglePlay);
    document.getElementById('live').addEventListener('click', loadLiveStations);
    document.getElementById('fullrefresh').addEventListener('click', function () {
      state.forceFull = true;
      if (state.einkSim) einkRefresh();
      else { state.residual.fill(0); renderFrame(false); setStatus('Full refresh.'); }
    });

    const panelSel = document.getElementById('panel');
    PANELS.forEach(function (p, i) {
      const o = document.createElement('option');
      o.value = i; o.textContent = p.label; panelSel.appendChild(o);
    });
    panelSel.addEventListener('change', function (e) {
      configurePanel(PANELS[+e.target.value]);
      renderFrame(false);
    });

    const styleSel = document.getElementById('style');
    STYLES.forEach(function (s) {
      const o = document.createElement('option');
      o.value = s.id; o.textContent = s.label;
      if (s.id === state.style) o.selected = true;
      styleSel.appendChild(o);
    });
    styleSel.addEventListener('change', function (e) {
      state.style = e.target.value;
      if (state.einkSim) { state.residual.fill(0); }
      renderFrame(false);
      state.prevTarget = state.lastTarget.slice();
      setStatus('Style: ' + state.style + '.');
    });

    const grat = document.getElementById('graticule');
    if (grat) grat.addEventListener('change', function (e) { state.graticule = e.target.checked; renderFrame(false); });

    const eink = document.getElementById('eink');
    if (eink) eink.addEventListener('change', function (e) {
      state.einkSim = e.target.checked;
      state.residual.fill(0);
      state.refreshCount = 0;
      renderFrame(false);
      state.prevTarget = state.lastTarget.slice();
      setStatus(state.einkSim
        ? 'E-ink mode ON: snap navigation, partial refreshes ghost, full refresh every ' + FULL_EVERY + ' moves.'
        : 'E-ink mode OFF: smooth preview.');
    });

    screenCanvas.addEventListener('click', onCanvasClick);

    window.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowRight' || e.key === ']') { selectStation(state.selected + 1); e.preventDefault(); }
      else if (e.key === 'ArrowLeft' || e.key === '[') { selectStation(state.selected - 1); e.preventDefault(); }
      else if (e.key === 'ArrowUp') { selectStation(state.selected - 1); e.preventDefault(); }
      else if (e.key === 'ArrowDown') { selectStation(state.selected + 1); e.preventDefault(); }
      else if (e.key === ' ') { togglePlay(); e.preventDefault(); }
      else if (e.key === 'f' || e.key === 'F') { document.getElementById('fullrefresh').click(); e.preventDefault(); }
    });
  }

  // -------------------------------------------------------------------------
  // Boot
  // -------------------------------------------------------------------------

  function boot() {
    if (!P) { setStatus('projection.js failed to load.'); return; }
    natCanvas = document.createElement('canvas');
    natCtx = natCanvas.getContext('2d', { willReadFrequently: true });
    screenCanvas = document.getElementById('screen');
    screenCtx = screenCanvas.getContext('2d');
    audio = document.getElementById('audio');
    audio.addEventListener('error', function () {
      if (state.playing) { state.playing = false; setStatus('Stream error.'); repaintChromeOnly(); }
    });

    configurePanel(PANELS[0]);
    wireControls();
    buildList();
    setStatus('Ready — ' + DATA.coast.length + ' coastlines, ' + DATA.land.length + ' landmasses, '
      + state.stations.length + ' stations. ←/→ navigate · F full-refresh.');
    selectStation(0, { snap: true });
    window.__WR_BOOTED = true;
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
