// This file fetches radio station data from the open source, open license
// radio.browser project, then normalizes and caches the station data.

const SOURCE_URL = 'https://de1.api.radio-browser.info/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=400';

const USER_AGENT = 'WorldRadioAPI/1.0 (+https://github.com/your-org/world-radio)';

const CACHE_TTL_MS = 10 * 60 * 1000; // 10 minutes
const MAX_STATIONS = 150;

let cache = {
  stations: [],
  byId: new Map(),
  fetchedAt: 0,
  lastError: null,
};

function normalize(raw) {
  const lon = parseFloat(raw.geo_long);
  const lat = parseFloat(raw.geo_lat);
  if (!Number.isFinite(lon) || !Number.isFinite(lat)) return null;
  return {
    id: raw.stationuuid,
    name: (raw.name || 'Unknown').trim().slice(0, 40),
    city: raw.state || raw.country || '',
    country: raw.countrycode || '',
    lon,
    lat,
    genre: (raw.tags || '').split(',')[0] || '',
    url: raw.url_resolved || raw.url || '',
    bitrate: raw.bitrate || 0,
    codec: raw.codec || '',
    ok: !!raw.lastcheckok, // Latest stream health check performed by radio-browser
  };
}

async function refresh() {
  // Fetch new station list from upstream radio-browser server.
  const res = await fetch(SOURCE_URL, { headers: { 'User-Agent': USER_AGENT } });
  if (!res.ok) throw new Error(`upstream ${res.status}`);
  const json = await res.json();

  const seen = new Set();
  const stations = [];
  for (const raw of json) {
    const s = normalize(raw);
    if (!s) continue;
    // Deduplicate by rounded coordinate so the globe doesn't get a dozen markers
    // stacked on the same pixel.
    const key = Math.round(s.lon) + '_' + Math.round(s.lat);
    if (seen.has(key)) continue;
    seen.add(key);
    stations.push(s);
    if (stations.length >= MAX_STATIONS) break;
  }

  cache = {
    stations,
    byId: new Map(stations.map((s) => [s.id, s])),
    fetchedAt: Date.now(),
    lastError: null,
  };
}

async function ensureFresh() {
  const stale = Date.now() - cache.fetchedAt > CACHE_TTL_MS;
  if (!stale) return;
  try {
    await refresh();
  } catch (err) {
    cache.lastError = err.message;
    // Keep serving the old cache (if any) rather than a hard failure.
    if (cache.stations.length === 0) throw err;
  }
}

export async function getStations() {
  await ensureFresh();
  return cache.stations;
}

export async function getStation(id) {
  await ensureFresh();
  return cache.byId.get(id) || null;
}

// Radius filter in decimal degrees, returns nearest N results.
export async function getNearestStations(lon, lat, limit = 20) {
  await ensureFresh();
  return cache.stations
    .map((s) => ({ s, d2: (s.lon - lon) ** 2 + (s.lat - lat) ** 2 }))
    .sort((a, b) => a.d2 - b.d2)
    .slice(0, limit)
    .map((x) => x.s);
}

export async function getRandomStation() {
  await ensureFresh();
  if (cache.stations.length === 0) return null;
  const i = Math.floor(Math.random() * cache.stations.length);
  return cache.stations[i];
}

export function getCacheInfo() {
  return {
    count: cache.stations.length,
    fetchedAt: cache.fetchedAt ? new Date(cache.fetchedAt).toISOString() : null,
    ageMs: cache.fetchedAt ? Date.now() - cache.fetchedAt : null,
    lastError: cache.lastError,
  };
}
