// Demo endpoint backend. Reads a hand-curated JSON file of country -> [stream
// URLs] and exposes it via /api/demo. No radio-browser fetching, no format
// classifier, no metadata storage: the file is the source of truth, and the
// MCU reads codec/name/bitrate from each stream's own ICY headers at play
// time.
//
// File shape (see api/data/demo_stations.json):
//   {
//     "countries": {
//       "<slug>": { "name": "<display name>", "streams": ["<url>", ...] },
//       ...
//     }
//   }
//
// Countries with an empty streams array are dropped at load so a fully
// pre-populated template stays readable while the API only shows what's
// actually filled in. The file is loaded once at boot and never refreshed;
// edit the file, restart the API.

import { readFileSync, existsSync, statSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DATA_PATH = resolve(__dirname, '../data/demo_stations.json');

const cache = {
  countries: [],   // [{ slug, name, streams: [{ stream_id, stream_url }] }]
  bySlug: new Map(),
  flat: [],        // [{ country: slug, stream_id }] in surf order
  loadedAt: 0,
  fileMtimeMs: 0,
  lastError: null,
};

// URL-safe slugifier -- matches what a human editor would naturally type
// (lowercase, ASCII, hyphens). Accents are stripped so a hand-authored key
// like "cote-d-ivoire" lines up with the display name "Côte d'Ivoire".
function toSlug(name) {
  return String(name)
    .toLowerCase()
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

function load() {
  if (!existsSync(DATA_PATH)) {
    throw new Error(
      `${DATA_PATH} not found. Create it with the shape:\n` +
      `  { "countries": { "<slug>": { "name": "<name>", "streams": ["<url>", ...] } } }`,
    );
  }
  const raw = JSON.parse(readFileSync(DATA_PATH, 'utf8'));
  if (!raw || typeof raw !== 'object' || !raw.countries || typeof raw.countries !== 'object') {
    throw new Error(`${DATA_PATH}: expected { "countries": { ... } } at top level`);
  }

  const countries = [];
  for (const [rawKey, val] of Object.entries(raw.countries)) {
    if (!val || !Array.isArray(val.streams)) continue;
    const streams = val.streams
      .filter((u) => typeof u === 'string' && u.trim())
      .map((u, i) => ({ stream_id: i, stream_url: u.trim() }));
    if (streams.length === 0) continue; // hide empty-country template entries
    countries.push({
      slug: toSlug(rawKey),
      name: (val.name || rawKey).trim(),
      streams,
    });
  }
  // Sort by display name so alphabetical surfing is by what the user sees,
  // not by the underlying slug (e.g. "Côte d'Ivoire" vs "cote-d-ivoire").
  countries.sort((a, b) => a.name.localeCompare(b.name));

  const flat = [];
  for (const c of countries) {
    for (const s of c.streams) flat.push({ country: c.slug, stream_id: s.stream_id });
  }

  cache.countries = countries;
  cache.bySlug = new Map(countries.map((c) => [c.slug, c]));
  cache.flat = flat;
  cache.loadedAt = Date.now();
  cache.fileMtimeMs = statSync(DATA_PATH).mtimeMs;
  cache.lastError = null;
}

// Kept for interface compatibility with the main stations cache. The demo
// data doesn't refresh; it's a one-shot load on first request (or on boot
// via index.mjs). Errors are surfaced only if the cache is empty -- once
// loaded successfully, a subsequent broken file just leaves us serving the
// last good version.
export async function ensureFresh() {
  if (cache.countries.length > 0) return;
  try { load(); }
  catch (err) { cache.lastError = err.message; throw err; }
}

// Convenience for the reviewer / other tooling: force a reload without
// bouncing the whole server, e.g. after the reviewer mutates the file.
export function reload() {
  try { load(); return { ok: true }; }
  catch (err) { cache.lastError = err.message; return { ok: false, error: err.message }; }
}

export function isReady() {
  return cache.countries.length > 0;
}

export function getDemoCacheInfo() {
  return {
    countries: cache.countries.length,
    streams: cache.flat.length,
    loadedAt: cache.loadedAt ? new Date(cache.loadedAt).toISOString() : null,
    fileMtime: cache.fileMtimeMs ? new Date(cache.fileMtimeMs).toISOString() : null,
    lastError: cache.lastError,
  };
}

// Position of a station in the flat alphabetical list, and the neighbors on
// either side (wrapping at the ends). Powers the rotary-encoder "next" walk
// which is allowed to cross country boundaries.
function computeNav(slug, streamId) {
  const idx = cache.flat.findIndex(
    (x) => x.country === slug && x.stream_id === streamId,
  );
  if (idx < 0) return null;
  const total = cache.flat.length;
  const nextRef = cache.flat[(idx + 1) % total];
  const prevRef = cache.flat[(idx - 1 + total) % total];
  const toRef = (r) => ({
    country: r.country,
    stream_id: r.stream_id,
    url: `/api/demo/${r.country}/${r.stream_id}`,
  });
  return { next: toRef(nextRef), prev: toRef(prevRef), position: idx + 1, of: total };
}

export async function listCountries() {
  await ensureFresh();
  return cache.countries.map((c) => ({
    slug: c.slug,
    name: c.name,
    count: c.streams.length,
  }));
}

export async function getCountry(slug) {
  await ensureFresh();
  const c = cache.bySlug.get(slug);
  if (!c) return null;
  return {
    slug: c.slug,
    name: c.name,
    streams: c.streams.map((s) => ({ stream_id: s.stream_id, stream_url: s.stream_url })),
  };
}

export async function getDemoStation(slug, streamId) {
  await ensureFresh();
  const c = cache.bySlug.get(slug);
  if (!c) return null;
  const s = c.streams[streamId];
  if (!s) return null;
  const nav = computeNav(slug, streamId);
  return {
    country: { slug: c.slug, name: c.name },
    stream_id: s.stream_id,
    stream_url: s.stream_url,
    ...nav,
  };
}
