import { createServer as createHttpServer } from 'node:http';
import {
  listStations,
  getStation,
  getStationByUuid,
  getRandomStation,
  getNearest,
  getCacheInfo,
  isReady,
  FILE_TYPES,
} from './stations.mjs';
import { getNowPlaying } from './now_playing.mjs';
import { logEvent } from './events.mjs';

// Hard cap on request body size for /api/events. Real events are ~200 bytes;
// 4 KB leaves plenty of headroom while making it trivially cheap to reject a
// misbehaving client (or a malicious one) trying to OOM us with a giant POST.
const EVENT_BODY_MAX_BYTES = 4096;

// Buffers the request body up to EVENT_BODY_MAX_BYTES and parses it as JSON.
// Resolves with { body } on success, { error, status } on failure. Never
// rejects -- the handler can just check .error.
function readJsonBody(req) {
  return new Promise((resolve) => {
    let total = 0;
    const chunks = [];
    req.on('data', (chunk) => {
      total += chunk.length;
      if (total > EVENT_BODY_MAX_BYTES) {
        // Stop reading further -- pause() and destroy the socket so a rogue
        // client can't keep streaming megabytes at us before we return 413.
        req.destroy();
        resolve({ error: 'body too large', status: 413 });
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      const raw = Buffer.concat(chunks).toString('utf8');
      if (raw.length === 0) {
        resolve({ body: {} });
        return;
      }
      try {
        resolve({ body: JSON.parse(raw) });
      } catch {
        resolve({ error: 'body was not valid JSON', status: 400 });
      }
    });
    req.on('error', () => resolve({ error: 'read error', status: 400 }));
  });
}

function sendJson(res, status, body, extraHeaders = {}) {
  const data = JSON.stringify(body);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(data),
    'access-control-allow-origin': '*',
    ...extraHeaders,
  });
  res.end(data);
}

const notFound = (res) => sendJson(res, 404, { error: 'not found' });
const badReq = (res, message) => sendJson(res, 400, { error: message });

// Parses ?fileType=mp3 (or aac). Returns { fileType } on success -- fileType
// is null if the param was omitted, so callers can pass it straight through.
// Returns { error } for anything else the client sent (e.g. ?fileType=ogg).
function parseFileType(url) {
  const raw = url.searchParams.get('fileType');
  if (raw === null) return { fileType: null };
  const v = raw.trim().toLowerCase();
  if (!FILE_TYPES.has(v)) {
    return { error: `fileType must be one of: ${[...FILE_TYPES].join(', ')}` };
  }
  return { fileType: v };
}

// :id can arrive as either the integer index (the primary handle for
// surfing) or the upstream stationuuid (useful for stable cross-refresh
// bookmarks). Integer wins when the token parses cleanly as one.
async function resolveStation(token, opts) {
  const asInt = Number.parseInt(token, 10);
  if (Number.isInteger(asInt) && String(asInt) === token) {
    return getStation(asInt, opts);
  }
  return getStationByUuid(token, opts);
}

async function withNowPlaying(station) {
  if (!station) return null;
  const now_playing = await getNowPlaying(station);
  return { ...station, now_playing };
}

export function createServer() {
  return createHttpServer(async (req, res) => {
    const url = new URL(req.url, `http://${req.headers.host}`);

    try {
      if (url.pathname === '/api/events' && req.method === 'POST') {
        const { body, error, status } = await readJsonBody(req);
        if (error) return sendJson(res, status || 400, { error });
        // req.socket.remoteAddress is behind Render/Fly proxies -- prefer the
        // first X-Forwarded-For hop if present, since that's the actual device.
        const fwd = req.headers['x-forwarded-for'];
        const sourceIp = (typeof fwd === 'string' && fwd.split(',')[0].trim())
          || req.socket.remoteAddress
          || null;
        const result = logEvent({
          deviceId: req.headers['x-device-id'],
          sourceIp,
          body,
        });
        if (!result.ok) return sendJson(res, 400, { error: result.error });
        // 200 (not 204) so sendJson can keep sending its usual body -- the
        // helper always writes content-length, which is technically illegal on
        // a 204 response. The device just ignores the body either way.
        return sendJson(res, 200, { ok: true });
      }

      if (url.pathname === '/api/health') {
        const ok = isReady();
        return sendJson(res, ok ? 200 : 503, { ok, cache: getCacheInfo() });
      }

      // ?fileType=mp3|aac is accepted on any /api/stations* endpoint; parsed
      // here once and validated even for endpoints that don't use it (a bad
      // value should be an error the caller sees, not silently dropped).
      const ft = parseFileType(url);
      if (ft.error) return badReq(res, ft.error);
      const fileType = ft.fileType;

      // Indexed list. now_playing is deliberately omitted here -- scraping
      // 200 streams per list request would take minutes and hammer the
      // upstream servers. Fetch per-station via /random or /:id instead.
      if (url.pathname === '/api/stations') {
        const near = url.searchParams.get('near');
        if (near) {
          const [lon, lat] = near.split(',').map(Number);
          if (!Number.isFinite(lon) || !Number.isFinite(lat)) {
            return badReq(res, "'near' must be 'lon,lat'");
          }
          const limitRaw = Number(url.searchParams.get('limit')) || 20;
          const limit = Math.min(Math.max(1, limitRaw), 100);
          return sendJson(res, 200, await getNearest(lon, lat, limit, { fileType }), {
            'cache-control': 'public, max-age=60',
          });
        }
        const stations = await listStations({ fileType });
        return sendJson(res, 200, { total: stations.length, stations }, {
          'cache-control': 'public, max-age=60',
        });
      }

      if (url.pathname === '/api/stations/random') {
        const station = await getRandomStation({ fileType });
        if (!station) {
          const msg = fileType
            ? `no ${fileType} stations available`
            : 'no stations cached yet';
          return sendJson(res, 503, { error: msg });
        }
        return sendJson(res, 200, await withNowPlaying(station));
      }

      // /api/stations/:id/now-playing -- lightweight polling endpoint for a
      // client that already knows the station id and just wants the track.
      const npMatch = url.pathname.match(/^\/api\/stations\/([^/]+)\/now-playing$/);
      if (npMatch) {
        const station = await resolveStation(decodeURIComponent(npMatch[1]), { fileType });
        if (!station) return notFound(res);
        const now_playing = await getNowPlaying(station);
        return sendJson(res, 200, { id: station.id, uuid: station.uuid, now_playing });
      }

      const stationMatch = url.pathname.match(/^\/api\/stations\/([^/]+)$/);
      if (stationMatch) {
        const station = await resolveStation(decodeURIComponent(stationMatch[1]), { fileType });
        if (!station) return notFound(res);
        return sendJson(res, 200, await withNowPlaying(station));
      }

      return notFound(res);
    } catch (err) {
      console.error('[world-radio-api] error:', err);
      return sendJson(res, 502, { error: 'upstream fetch failed' });
    }
  });
}
