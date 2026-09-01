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

// limit is intentionally high (1500) because we filter aggressively below --
// after dropping HLS mounts, playlist URLs, and non-MP3/AAC codecs we still
// need to end up with ~MAX_STATIONS entries.
const SEARCH_PATH =
  '/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=1500';

// The ESP32 firmware ships MP3 and AAC decoders only. Anything else (OGG,
// FLAC, Opus, etc.) is dropped at ingest so /random can't hand the device a
// stream it physically can't play. Values here are compared case-insensitively
// against radio-browser's `codec` field, which uses a small controlled set.
const MP3_CODECS = new Set(['MP3', 'MPEG']);
const AAC_CODECS = new Set(['AAC', 'AAC+', 'AACP', 'HE-AAC', 'HE-AACV2']);

// Playlist / manifest file extensions. If the resolved URL points to one of
// these, the "stream" is actually a text file the MCU would have to parse to
// find the real audio -- reject it. Checked against the URL's pathname (query
// string stripped) so `.../stream.mp3?token=...` still passes.
const PLAYLIST_EXTS = new Set([
  '.m3u', '.m3u8', '.pls', '.asx', '.xspf', '.ram', '.rm', '.wpl',
]);

function classifyCodec(codec) {
  const c = (codec || '').trim().toUpperCase();
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
    return true; // unparseable URL -- reject to be safe
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

  // MCU can't parse HLS manifests -- radio-browser flags these with hls=1.
  if (raw.hls === 1 || raw.hls === '1' || raw.hls === true) return null;

  // Even without the hls flag, a URL that ends in .m3u8/.pls/etc. serves a
  // playlist file rather than raw audio bytes. Reject those too.
  if (isPlaylistUrl(stream_url)) return null;

  const format = classifyCodec(raw.codec);
  if (!format) return null;

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
    format, // 'mp3' or 'aac' -- lets the MCU pick a decoder without string parsing
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

// Attach next_id / prev_id, wrapping at the ends so the ESP can "surf"
// the list circularly without ever hitting a dead end.
function withNav(s) {
  if (!s) return null;
  const total = cache.stations.length;
  return {
    ...s,
    prev_id: (s.id - 1 + total) % total,
    next_id: (s.id + 1) % total,
  };
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

export async function listStations() {
  await ensureFresh();
  return cache.stations.map(slim);
}

export async function getStation(id) {
  await ensureFresh();
  return withNav(cache.byId.get(id) || null);
}

export async function getStationByUuid(uuid) {
  await ensureFresh();
  return withNav(cache.byUuid.get(uuid) || null);
}

export async function getRandomStation() {
  await ensureFresh();
  if (cache.stations.length === 0) return null;
  const i = Math.floor(Math.random() * cache.stations.length);
  return withNav(cache.stations[i]);
}

export async function getNearest(lon, lat, limit) {
  await ensureFresh();
  return cache.stations
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
