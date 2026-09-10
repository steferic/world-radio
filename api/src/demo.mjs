// Curated "demo" station set for the hardware demo. Independent of the main
// station cache (which is geo-info-only and dedup'd by rounded coordinate for
// the globe view) -- the demo wants broad country coverage, not geographic
// spread. Pulls a large chunk of popular MP3/AAC stations from radio-browser,
// groups them by country, keeps the top N per country by click count, and
// sorts countries alphabetically. Rotary-encoder surfing walks the flat
// alphabetical list, so the last German station's next_id is Ghana #0.

import { classifyFormat } from './stations.mjs';

const RADIO_BROWSER_HOSTS = [
  'de1.api.radio-browser.info',
  'at1.api.radio-browser.info',
  'nl1.api.radio-browser.info',
];

// hidebroken=true skips stations whose last uptime check failed -- our proxy
// for "confirmed stable". Ordering by clickcount gives us popularity.
// limit=5000 is generous so grouping-by-country still yields decent coverage
// after we filter to MP3/AAC and cap per country.
const DEMO_SEARCH_PATH =
  '/json/stations/search?hidebroken=true&order=clickcount&reverse=true&limit=5000';

const USER_AGENT = 'WorldRadioAPI/1.0 (+https://github.com/bkelldog/world-radio)';

// Demo list is more expensive to build (larger upstream fetch) and changes
// slowly, so hold it longer than the main cache.
const CACHE_TTL_MS = 30 * 60 * 1000;
const UPSTREAM_TIMEOUT_MS = 10000;

// Curation knobs. Tuned for "handful per country, decent quality" -- easy to
// nudge if the demo needs more/fewer stations.
const STATIONS_PER_COUNTRY = 3;
const MIN_BITRATE = 48;

let cache = {
  countries: [],       // ordered alphabetically by name
  bySlug: new Map(),   // slug -> country object with .stations
  flat: [],            // flat list of { country, stream_id } for cross-country nav
  fetchedAt: 0,
  lastError: null,
};

let inflight = null;

// URL-safe country identifier. Strips diacritics (Côte d'Ivoire -> cote-d-ivoire)
// so the MCU can build request paths without worrying about UTF-8 encoding.
function toSlug(name) {
  return name
    .toLowerCase()
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

function normalize(raw) {
  const stream_url = raw.url_resolved || raw.url;
  if (!stream_url) return null;
  const format = classifyFormat(raw);
  if (!format) return null;
  const country = (raw.country || '').trim();
  if (!country) return null;
  if ((raw.bitrate || 0) < MIN_BITRATE) return null;

  return {
    uuid: raw.stationuuid,
    name: (raw.name || 'Unknown').trim().slice(0, 60),
    country,
    country_code: raw.countrycode || '',
    genre: (raw.tags || '').split(',')[0].trim(),
    codec: raw.codec || '',
    format,
    bitrate: raw.bitrate || 0,
    clickcount: raw.clickcount || 0,
    stream_url,
    homepage: raw.homepage || '',
  };
}

async function fetchWithFallback() {
  let lastErr;
  for (const host of RADIO_BROWSER_HOSTS) {
    try {
      const res = await fetch(`https://${host}${DEMO_SEARCH_PATH}`, {
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

  // Bucket by country name. Upstream is already sorted by clickcount desc,
  // so each bucket's head is the country's most popular qualifying station.
  const buckets = new Map();
  for (const r of raw) {
    const s = normalize(r);
    if (!s) continue;
    if (!buckets.has(s.country)) buckets.set(s.country, []);
    buckets.get(s.country).push(s);
  }

  // Take top N per country, drop countries with zero survivors, then sort
  // countries alphabetically by name (localeCompare handles accents sensibly).
  const countries = [];
  for (const [name, stations] of buckets) {
    const top = stations.slice(0, STATIONS_PER_COUNTRY);
    if (top.length === 0) continue;
    top.forEach((s, i) => { s.stream_id = i; });
    countries.push({
      slug: toSlug(name),
      name,
      country_code: top[0].country_code,
      stations: top,
    });
  }
  countries.sort((a, b) => a.name.localeCompare(b.name));

  // Flat list so cross-country next/prev is an O(1) index lookup at read time.
  const flat = [];
  for (const c of countries) {
    for (const s of c.stations) flat.push({ country: c.slug, stream_id: s.stream_id });
  }

  cache = {
    countries,
    bySlug: new Map(countries.map((c) => [c.slug, c])),
    flat,
    fetchedAt: Date.now(),
    lastError: null,
  };
}

export async function ensureFresh() {
  if (Date.now() - cache.fetchedAt <= CACHE_TTL_MS) return;
  if (!inflight) {
    inflight = refresh()
      .catch((err) => {
        cache.lastError = err.message;
        if (cache.countries.length === 0) throw err;
      })
      .finally(() => { inflight = null; });
  }
  await inflight;
}

// Slim shape for country list responses: no stream_url (encourages clients to
// go through /:country/:id which computes nav), no homepage, no clickcount.
function slim(s) {
  return {
    stream_id: s.stream_id,
    uuid: s.uuid,
    name: s.name,
    genre: s.genre,
    codec: s.codec,
    format: s.format,
    bitrate: s.bitrate,
  };
}

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
    country_code: c.country_code,
    count: c.stations.length,
  }));
}

export async function getCountry(slug) {
  await ensureFresh();
  const c = cache.bySlug.get(slug);
  if (!c) return null;
  return {
    slug: c.slug,
    name: c.name,
    country_code: c.country_code,
    stations: c.stations.map(slim),
  };
}

export async function getDemoStation(slug, streamId) {
  await ensureFresh();
  const c = cache.bySlug.get(slug);
  if (!c) return null;
  const s = c.stations[streamId];
  if (!s) return null;
  const nav = computeNav(slug, streamId);
  // Strip the station's raw `country`/`country_code` -- we replace them with a
  // richer nested object (slug + display name + code) so the MCU doesn't have
  // to slugify anything itself.
  const { country: _c, country_code: _cc, ...rest } = s;
  return {
    country: { slug: c.slug, name: c.name, country_code: c.country_code },
    ...rest,
    ...nav,
  };
}

export function isReady() {
  return cache.countries.length > 0;
}

export function getDemoCacheInfo() {
  return {
    countries: cache.countries.length,
    stations: cache.flat.length,
    fetchedAt: cache.fetchedAt ? new Date(cache.fetchedAt).toISOString() : null,
    ageMs: cache.fetchedAt ? Date.now() - cache.fetchedAt : null,
    lastError: cache.lastError,
  };
}
