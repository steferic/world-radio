// Pulls the station corpus from radio-browser, normalizes it into the shape
// the ESP32 (and the browser prototype) consume, and holds it in memory as
// an indexed list. Indices are assigned by sorting the deduped set by UUID
// so they stay as stable as we can make them across refreshes: adding or
// removing a station shifts a few neighbors instead of reshuffling all of
// them, which matters because the ESP walks next_id/prev_id on the client.

const RADIO_BROWSER_HOSTS = [
  'de1.api.radio-browser.info',
  'at1.api.radio-browser.info',
  'nl1.api.radio-browser.info',
];

const SEARCH_PATH =
  '/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=500';

// The ESP32 firmware ships MP3 and AAC decoders only. We DON'T drop other
// codecs at ingest -- the browser demo happily plays OGG/FLAC, and other
// clients may not care. Instead, every station gets a `format` label ('mp3',
// 'aac', or null) and query endpoints accept ?fileType=mp3|aac to filter.
const MP3_CODECS = new Set(['MP3', 'MPEG']);
const AAC_CODECS = new Set(['AAC', 'AAC+', 'AACP', 'HE-AAC', 'HE-AACV2']);

// Playlist / manifest URLs -- the "stream" is a text file the MCU would have
// to parse. Flagged here so the mp3/aac filter can exclude them; nothing is
// dropped at ingest.
const PLAYLIST_EXTS = new Set([
  '.m3u', '.m3u8', '.pls', '.asx', '.xspf', '.ram', '.rm', '.wpl',
]);

export const FILE_TYPES = new Set(['mp3', 'aac']);

// Returns 'mp3' | 'aac' | null. null means "not something the MCU decoders
// can consume directly" -- either an unsupported codec, an HLS mount, or a
// URL that points at a playlist file rather than raw audio bytes.
function classifyFormat(raw) {
  if (raw.hls === 1 || raw.hls === '1' || raw.hls === true) return null;
  const stream = raw.url_resolved || raw.url || '';
  if (isPlaylistUrl(stream)) return null;
  const c = (raw.codec || '').trim().toUpperCase();
  if (MP3_CODECS.has(c)) return 'mp3';
  if (AAC_CODECS.has(c)) return 'aac';
  return null;
}

function isPlaylistUrl(url) {
  try {
    const u = new URL(url);
    const path = u.pathname.toLowerCase();
    const dot = path.lastIndexOf('.');
    if (dot < 0) return false;
    return PLAYLIST_EXTS.has(path.slice(dot));
  } catch {
    return true; // unparseable URL -- treat as unplayable
  }
}

const USER_AGENT = 'WorldRadioAPI/1.0 (+https://github.com/bkelldog/world-radio)';

const CACHE_TTL_MS = 10 * 60 * 1000;
const UPSTREAM_TIMEOUT_MS = 8000;
const MAX_STATIONS = 200;

let cache = {
  stations: [],
  byId: new Map(),
  byUuid: new Map(),
  fetchedAt: 0,
  lastError: null,
};

let inflight = null;

function normalize(raw) {
  const lon = parseFloat(raw.geo_long);
  const lat = parseFloat(raw.geo_lat);
  if (!Number.isFinite(lon) || !Number.isFinite(lat)) return null;
  const stream_url = raw.url_resolved || raw.url;
  if (!stream_url) return null;

  const region = (raw.state || '').trim();
  const country = raw.countrycode || '';
  const location = [region, country].filter(Boolean).join(', ');

  return {
    uuid: raw.stationuuid,
    name: (raw.name || 'Unknown').trim().slice(0, 60),
    location,
    country,
    region,
    lon,
    lat,
    genre: (raw.tags || '').split(',')[0].trim(),
    codec: raw.codec || '',
    // 'mp3' | 'aac' | null -- null means the MCU can't play it directly.
    // Used by the ?fileType= query filter; the field is always exposed so a
    // client can see up front what it's being handed.
    format: classifyFormat(raw),
    bitrate: raw.bitrate || 0,
    stream_url,
    homepage: raw.homepage || '',
  };
}

async function fetchWithFallback() {
  let lastErr;
  for (const host of RADIO_BROWSER_HOSTS) {
    try {
      const res = await fetch(`https://${host}${SEARCH_PATH}`, {
        headers: { 'User-Agent': USER_AGENT },
        signal: AbortSignal.timeout(UPSTREAM_TIMEOUT_MS),
      });
      if (!res.ok) throw new Error(`${host}: HTTP ${res.status}`);
      return await res.json();
    } catch (err) {
      lastErr = err;
    }
  }
  throw lastErr || new Error('all upstream hosts failed');
}

async function refresh() {
  const raw = await fetchWithFallback();

  const seen = new Set();
  const stations = [];
  for (const r of raw) {
    const s = normalize(r);
    if (!s) continue;
    // Dedup by rounded coordinate so nearby markers don't collide on the globe.
    const key = Math.round(s.lon) + '_' + Math.round(s.lat);
    if (seen.has(key)) continue;
    seen.add(key);
    stations.push(s);
    if (stations.length >= MAX_STATIONS) break;
  }

  // Stable-ish ordering: sort by UUID before assigning integer IDs so a
  // partial refresh doesn't reshuffle every ESP client's cached next/prev.
  stations.sort((a, b) => (a.uuid < b.uuid ? -1 : a.uuid > b.uuid ? 1 : 0));
  stations.forEach((s, i) => { s.id = i; });

  cache = {
    stations,
    byId: new Map(stations.map((s) => [s.id, s])),
    byUuid: new Map(stations.map((s) => [s.uuid, s])),
    fetchedAt: Date.now(),
    lastError: null,
  };
}

// Serve-stale-on-error, single-flight refresh: concurrent stale-cache
// requests share the same in-flight fetch instead of hammering upstream.
export async function ensureFresh() {
  if (Date.now() - cache.fetchedAt <= CACHE_TTL_MS) return;
  if (!inflight) {
    inflight = refresh()
      .catch((err) => {
        cache.lastError = err.message;
        if (cache.stations.length === 0) throw err;
      })
      .finally(() => { inflight = null; });
  }
  await inflight;
}

// Attach next_id / prev_id from a specific subset (defaults to the full
// cache). When a fileType filter is active, surfing stays within the matching
// subset so next-next-next won't wander onto a station the device can't play.
function withNav(s, subset) {
  if (!s) return null;
  const list = subset || cache.stations;
  const total = list.length;
  if (total === 0) return null;
  // If the requested station is outside the current subset (e.g. asked for a
  // specific id with ?fileType=mp3 but that id is an OGG stream) return null
  // rather than making up neighbors -- the caller will 404.
  const idx = list.findIndex((x) => x.id === s.id);
  if (idx < 0) return null;
  return {
    ...s,
    prev_id: list[(idx - 1 + total) % total].id,
    next_id: list[(idx + 1) % total].id,
  };
}

function subsetFor(fileType) {
  if (!fileType) return cache.stations;
  return cache.stations.filter((s) => s.format === fileType);
}

// Slim shape for list responses: no stream URL (would encourage clients to
// pick their own randomly instead of going through /random), no nav (nav
// only makes sense per-station), no homepage.
function slim(s) {
  return {
    id: s.id,
    uuid: s.uuid,
    name: s.name,
    location: s.location,
    country: s.country,
    region: s.region,
    lon: s.lon,
    lat: s.lat,
    genre: s.genre,
    codec: s.codec,
    format: s.format,
    bitrate: s.bitrate,
  };
}

export async function listStations({ fileType } = {}) {
  await ensureFresh();
  return subsetFor(fileType).map(slim);
}

export async function getStation(id, { fileType } = {}) {
  await ensureFresh();
  const s = cache.byId.get(id);
  if (!s) return null;
  return withNav(s, subsetFor(fileType));
}

export async function getStationByUuid(uuid, { fileType } = {}) {
  await ensureFresh();
  const s = cache.byUuid.get(uuid);
  if (!s) return null;
  return withNav(s, subsetFor(fileType));
}

export async function getRandomStation({ fileType } = {}) {
  await ensureFresh();
  const list = subsetFor(fileType);
  if (list.length === 0) return null;
  const i = Math.floor(Math.random() * list.length);
  return withNav(list[i], list);
}

export async function getNearest(lon, lat, limit, { fileType } = {}) {
  await ensureFresh();
  return subsetFor(fileType)
    .map((s) => ({ s, d2: (s.lon - lon) ** 2 + (s.lat - lat) ** 2 }))
    .sort((a, b) => a.d2 - b.d2)
    .slice(0, limit)
    .map((x) => slim(x.s));
}

export function isReady() {
  return cache.stations.length > 0;
}

export function getCacheInfo() {
  return {
    count: cache.stations.length,
    fetchedAt: cache.fetchedAt ? new Date(cache.fetchedAt).toISOString() : null,
    ageMs: cache.fetchedAt ? Date.now() - cache.fetchedAt : null,
    lastError: cache.lastError,
  };
}
