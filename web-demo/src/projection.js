// projection.js — Portable 1-bit globe renderer.
//
// This file is deliberately DOM-free and dependency-free. Every function here
// maps almost line-for-line to C you can run on the ESP32-S3 driving a B&W
// e-ink panel. Inputs are only numbers and typed arrays. KEEP IT THAT WAY — the
// moment you reach for the canvas, d3, or the DOM in here, you've written code
// that can't go on the chip. The browser-specific glue lives in app.js.
//
// Loaded as a CLASSIC script (not an ES module) so it works on any static
// server and even over file://. The public API is exposed as `window.Projection`.
//
// Framebuffer model: one byte per pixel, 0 = white, 1 = black. On the ESP32 this
// becomes a packed 1bpp buffer (width*height/8 bytes). See firmware-notes/.

(function () {
  'use strict';

  const BLACK = 1;
  const WHITE = 0;
  const DEG = Math.PI / 180;

  // 4x4 Bayer ordered-dither thresholds, normalized to (0,1). Used to fake gray
  // tone (sphere shading) on a panel that only has black and white.
  const BAYER4 = [
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5,
  ].map((v) => (v + 0.5) / 16);

  // -------------------------------------------------------------------------
  // Framebuffer primitives
  // -------------------------------------------------------------------------

  function makeFramebuffer(width, height) {
    return { width, height, px: new Uint8Array(width * height) };
  }

  function clear(fb, color) {
    fb.px.fill(color === undefined ? WHITE : color);
  }

  function setPixel(fb, x, y, color) {
    x |= 0;
    y |= 0;
    if (x < 0 || y < 0 || x >= fb.width || y >= fb.height) return;
    fb.px[y * fb.width + x] = color;
  }

  // Bresenham line.
  function drawLine(fb, x0, y0, x1, y1, color) {
    if (color === undefined) color = BLACK;
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
    const dx = Math.abs(x1 - x0);
    const dy = -Math.abs(y1 - y0);
    const sx = x0 < x1 ? 1 : -1;
    const sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    for (;;) {
      setPixel(fb, x0, y0, color);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  // Dotted line — fakes a lighter "gray" stroke on a 1-bit panel by skipping
  // pixels. Used for the graticule so it reads as secondary to the coastlines.
  function drawDottedLine(fb, x0, y0, x1, y1, color, period) {
    if (color === undefined) color = BLACK;
    if (period === undefined) period = 3;
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
    const dx = Math.abs(x1 - x0);
    const dy = -Math.abs(y1 - y0);
    const sx = x0 < x1 ? 1 : -1;
    const sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    let i = 0;
    for (;;) {
      if (i++ % period === 0) setPixel(fb, x0, y0, color);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  function fillCircle(fb, cx, cy, r, color) {
    if (color === undefined) color = BLACK;
    const r2 = r * r;
    for (let y = -r; y <= r; y++) {
      for (let x = -r; x <= r; x++) {
        if (x * x + y * y <= r2) setPixel(fb, cx + x, cy + y, color);
      }
    }
  }

  // Midpoint circle outline.
  function strokeCircle(fb, cx, cy, r, color) {
    if (color === undefined) color = BLACK;
    let x = r;
    let y = 0;
    let err = 1 - r;
    while (x >= y) {
      const pts = [
        [x, y], [y, x], [-y, x], [-x, y],
        [-x, -y], [-y, -x], [y, -x], [x, -y],
      ];
      for (let i = 0; i < pts.length; i++) setPixel(fb, cx + pts[i][0], cy + pts[i][1], color);
      y++;
      if (err < 0) err += 2 * y + 1;
      else { x--; err += 2 * (y - x) + 1; }
    }
  }

  // -------------------------------------------------------------------------
  // Orthographic globe projection
  // -------------------------------------------------------------------------

  // Precompute the trig for a viewing center so the per-point math is cheap.
  function makeCenter(lonDeg, latDeg) {
    const lon = lonDeg * DEG;
    const lat = latDeg * DEG;
    return { lonDeg, latDeg, lon, lat, sinLat: Math.sin(lat), cosLat: Math.cos(lat) };
  }

  function GlobeView(width, height, marginFrac) {
    this.resize(width, height, marginFrac);
  }

  GlobeView.prototype.resize = function (width, height, marginFrac) {
    if (marginFrac === undefined) marginFrac = 0.07;
    this.width = width;
    this.height = height;
    this.cx = (width - 1) / 2;
    this.cy = (height - 1) / 2;
    this.R = (Math.min(width, height) / 2) * (1 - marginFrac);
  };

  // Orthographic azimuthal projection ("globe as seen from space").
  // Returns { x, y, front } — front=true means the point is on the visible
  // near hemisphere (the far side is clipped). THE math that ports to C.
  GlobeView.prototype.project = function (lonDeg, latDeg, c) {
    const lon = lonDeg * DEG;
    const lat = latDeg * DEG;
    const dlon = lon - c.lon;
    const cosLatP = Math.cos(lat);
    const sinLatP = Math.sin(lat);
    const cosc = c.sinLat * sinLatP + c.cosLat * cosLatP * Math.cos(dlon);
    const x = cosLatP * Math.sin(dlon);
    const y = c.cosLat * sinLatP - c.sinLat * cosLatP * Math.cos(dlon);
    return {
      x: this.cx + this.R * x,
      y: this.cy - this.R * y, // screen y grows downward
      front: cosc >= 0,
    };
  };

  // -------------------------------------------------------------------------
  // Compositing
  // -------------------------------------------------------------------------

  function shadeSphere(fb, view, light) {
    const cx = view.cx, cy = view.cy, R = view.R;
    const lx = light[0], ly = light[1], lz = light[2];
    const yMin = Math.max(0, Math.ceil(cy - R));
    const yMax = Math.min(fb.height - 1, Math.floor(cy + R));
    const xMin = Math.max(0, Math.ceil(cx - R));
    const xMax = Math.min(fb.width - 1, Math.floor(cx + R));
    for (let y = yMin; y <= yMax; y++) {
      for (let x = xMin; x <= xMax; x++) {
        const nx = (x - cx) / R;
        const ny = (y - cy) / R;
        const r2 = nx * nx + ny * ny;
        if (r2 > 1) continue;
        const nz = Math.sqrt(1 - r2);
        // Flip ny so "up" on screen lights from the top.
        let lam = nx * lx + -ny * ly + nz * lz;
        if (lam < 0) lam = 0;
        const brightness = 0.66 + 0.34 * lam; // keep it light
        const t = BAYER4[(y & 3) * 4 + (x & 3)];
        if (brightness < t) setPixel(fb, x, y, BLACK);
      }
    }
  }

  function drawPolyline(fb, view, center, coords, dotted) {
    let prev = null;
    for (let i = 0; i < coords.length; i++) {
      const p = view.project(coords[i][0], coords[i][1], center);
      if (prev && prev.front && p.front) {
        if (dotted) drawDottedLine(fb, prev.x, prev.y, p.x, p.y, BLACK);
        else drawLine(fb, prev.x, prev.y, p.x, p.y, BLACK);
      }
      prev = p;
    }
  }

  // Inverse orthographic: screen pixel -> (lon,lat) on the visible hemisphere,
  // or null if the pixel is outside the disc. Lets us fill land by testing each
  // disc pixel against the land polygons — and it clips at the limb for free.
  function unproject(view, center, px, py) {
    const x = (px - view.cx) / view.R;
    const y = -(py - view.cy) / view.R; // north up
    const rr = x * x + y * y;
    if (rr > 1) return null;
    const nz = Math.sqrt(1 - rr);
    const lat = Math.asin(nz * center.sinLat + y * center.cosLat) / DEG;
    let lon = (center.lon + Math.atan2(x, nz * center.cosLat - y * center.sinLat)) / DEG;
    lon = ((lon + 180) % 360 + 360) % 360 - 180;
    return { lon: lon, lat: lat };
  }

  // Even-odd ray cast across all of a landmass's rings (handles holes), with a
  // fast bbox reject up front.
  function pointInFeature(feat, lon, lat) {
    const b = feat.bbox;
    if (lon < b[0] || lon > b[2] || lat < b[1] || lat > b[3]) return false;
    let inside = false;
    const rings = feat.rings;
    for (let r = 0; r < rings.length; r++) {
      const ring = rings[r];
      for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
        const xi = ring[i][0], yi = ring[i][1];
        const xj = ring[j][0], yj = ring[j][1];
        if (((yi > lat) !== (yj > lat)) &&
            (lon < (xj - xi) * (lat - yi) / (yj - yi) + xi)) inside = !inside;
      }
    }
    return inside;
  }

  // Fill landmasses on the visible hemisphere. dither=false → solid black land;
  // dither=true → 50% dithered land (gray look, but ocean stays clean white so
  // it ghosts far less than full-disc sphere shading).
  function fillLand(fb, view, center, land, dither) {
    const cx = view.cx, cy = view.cy, R = view.R;
    const yMin = Math.max(0, Math.ceil(cy - R));
    const yMax = Math.min(fb.height - 1, Math.floor(cy + R));
    const xMin = Math.max(0, Math.ceil(cx - R));
    const xMax = Math.min(fb.width - 1, Math.floor(cx + R));
    for (let py = yMin; py <= yMax; py++) {
      for (let px = xMin; px <= xMax; px++) {
        const x = (px - cx) / R;
        const y = -(py - cy) / R;
        const rr = x * x + y * y;
        if (rr > 1) continue;
        const nz = Math.sqrt(1 - rr);
        const lat = Math.asin(nz * center.sinLat + y * center.cosLat) / DEG;
        let lon = (center.lon + Math.atan2(x, nz * center.cosLat - y * center.sinLat)) / DEG;
        lon = ((lon + 180) % 360 + 360) % 360 - 180;
        let isLand = false;
        for (let f = 0; f < land.length; f++) {
          if (pointInFeature(land[f], lon, lat)) { isLand = true; break; }
        }
        if (!isLand) continue;
        if (dither) {
          if (0.5 < BAYER4[(py & 3) * 4 + (px & 3)]) setPixel(fb, px, py, BLACK);
        } else {
          setPixel(fb, px, py, BLACK);
        }
      }
    }
  }

  // Built-in graticule (lat/long grid). Generated, not stored — same on the chip.
  function eachGraticule(stepDeg, fn) {
    for (let lon = -180; lon < 180; lon += stepDeg) {
      const line = [];
      for (let lat = -80; lat <= 80; lat += 4) line.push([lon, lat]);
      fn(line);
    }
    for (let lat = -60; lat <= 60; lat += stepDeg) {
      const line = [];
      for (let lon = -180; lon <= 180; lon += 4) line.push([lon, lat]);
      fn(line);
    }
  }

  // Draw one station marker. Returns its projected screen point (or null if it's
  // on the far hemisphere). The white halo guarantees the dot reads against
  // coastlines and shading.
  function drawMarker(fb, view, center, station, selected) {
    const p = view.project(station.lon, station.lat, center);
    if (!p.front) return null;
    const x = p.x | 0;
    const y = p.y | 0;
    if (selected) {
      fillCircle(fb, x, y, 5, WHITE);
      fillCircle(fb, x, y, 2, BLACK);
      strokeCircle(fb, x, y, 6, BLACK);
      strokeCircle(fb, x, y, 7, BLACK);
    } else {
      fillCircle(fb, x, y, 3, WHITE);
      fillCircle(fb, x, y, 2, BLACK);
    }
    return p;
  }

  // Render the whole globe into the framebuffer. `data` is { coast, land }:
  //   coast = array of [[lon,lat],...] coastline polylines
  //   land  = array of landmasses (see land.js) for the fill styles
  // opts.style selects the look (and, crucially, how e-ink-friendly it is):
  //   'wire'   coastline outlines only        — fewest black pixels, least ghosting
  //   'filled' solid black continents         — bold, e-ink-friendly (ocean clean)
  //   'gray'   50% dithered continents        — middle ground, ocean stays clean
  //   'shaded' full-disc lit sphere dither    — prettiest, but ghosts on real e-ink
  // Mutates `fb`.
  function renderGlobe(fb, view, center, data, opts) {
    opts = opts || {};
    const style = opts.style || 'gray';
    const graticule = opts.graticule !== false;
    const light = opts.light || [-0.5, -0.55, 0.67];
    const coast = data && data.coast;
    const land = data && data.land;

    clear(fb, WHITE);

    if (style === 'shaded') shadeSphere(fb, view, light);
    else if (style === 'filled' && land) fillLand(fb, view, center, land, false);
    else if (style === 'gray' && land) fillLand(fb, view, center, land, true);

    if (graticule) eachGraticule(30, function (line) { drawPolyline(fb, view, center, line, true); });

    // Coastline outlines read on every style except solid fill (where they'd be
    // black-on-black and invisible).
    if (coast && style !== 'filled') {
      for (let i = 0; i < coast.length; i++) drawPolyline(fb, view, center, coast[i], false);
    }

    strokeCircle(fb, Math.round(view.cx), Math.round(view.cy), Math.round(view.R), BLACK);
  }

  window.Projection = {
    BLACK, WHITE,
    makeFramebuffer, clear, setPixel, drawLine, drawDottedLine, fillCircle, strokeCircle,
    makeCenter, GlobeView, renderGlobe, drawMarker, fillLand, unproject,
  };
})();
