// Scrapes ICY metadata (Shoutcast/Icecast "StreamTitle=...") by briefly
// connecting to a stream, reading past its first metadata block, and closing.
// Streams that don't advertise `icy-metaint` (HLS, AAC-in-MPEG-TS, most OGG
// mounts) return null and are negative-cached for longer so we don't hammer
// them. Positive results are cached briefly per station so /random doesn't
// re-scrape on every request.

const FETCH_TIMEOUT_MS = 3500;
const POSITIVE_TTL_MS = 20 * 1000;
const NEGATIVE_TTL_MS = 5 * 60 * 1000;
const MAX_READ_BYTES = 64 * 1024; // hard cap so a stream without metadata can't drain forever

const cache = new Map(); // station.uuid -> { fetchedAt, value }

function parseStreamTitle(block) {
  const m = block.match(/StreamTitle='([^']*)'/);
  if (!m) return { title: null, artist: null };
  const s = m[1].trim();
  if (!s) return { title: null, artist: null };
  // "Artist - Title" is the near-universal convention; split on the first
  // " - " and fall back to a title-only result when it isn't there.
  const dash = s.indexOf(' - ');
  if (dash > 0) {
    return { artist: s.slice(0, dash).trim(), title: s.slice(dash + 3).trim() };
  }
  return { artist: null, title: s };
}

async function scrape(streamUrl) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);

  try {
    const res = await fetch(streamUrl, {
      headers: {
        'Icy-MetaData': '1',
        'User-Agent': 'WorldRadioAPI/1.0 (metadata scrape)',
      },
      signal: controller.signal,
      redirect: 'follow',
    });
    if (!res.ok || !res.body) return null;

    const metaint = parseInt(res.headers.get('icy-metaint') || '0', 10);
    if (!metaint || metaint <= 0) return null;

    const reader = res.body.getReader();
    let buf = new Uint8Array(0);
    let totalRead = 0;
    let result = null;

    while (result === null && totalRead < MAX_READ_BYTES) {
      const { value, done } = await reader.read();
      if (done) break;

      totalRead += value.length;
      const next = new Uint8Array(buf.length + value.length);
      next.set(buf);
      next.set(value, buf.length);
      buf = next;

      // First `metaint` bytes are audio, then one length byte (in 16-byte
      // units), then `length * 16` metadata bytes, then audio resumes.
      if (buf.length < metaint + 1) continue;
      const metaLen = buf[metaint] * 16;
      if (metaLen === 0) {
        // Server sent an empty metadata block -- valid ICY, but no title yet.
        // Give up on this scrape rather than waiting for the next block
        // (potentially another 8-16 KB away).
        result = { title: null, artist: null };
        break;
      }
      const needed = metaint + 1 + metaLen;
      if (buf.length < needed) continue;

      const metaBytes = buf.slice(metaint + 1, needed);
      const text = new TextDecoder('utf-8', { fatal: false })
        .decode(metaBytes)
        .replace(/\0+$/, '');
      result = parseStreamTitle(text);
    }

    try { await reader.cancel(); } catch { /* ignore */ }
    return result;
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

export async function getNowPlaying(station) {
  if (!station || !station.stream_url) return null;
  const key = station.uuid;
  const now = Date.now();
  const entry = cache.get(key);
  if (entry) {
    const ttl = entry.value && (entry.value.title || entry.value.artist)
      ? POSITIVE_TTL_MS
      : NEGATIVE_TTL_MS;
    if (now - entry.fetchedAt < ttl) return entry.value;
  }
  const value = await scrape(station.stream_url);
  cache.set(key, { fetchedAt: now, value });
  return value;
}
