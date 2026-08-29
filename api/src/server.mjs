import { createServer as createHttpServer } from 'node:http';
import {
  listStations,
  getStation,
  getStationByUuid,
  getRandomStation,
  getNearest,
  getCacheInfo,
  isReady,
} from './stations.mjs';
import { getNowPlaying } from './now_playing.mjs';

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

// :id can arrive as either the integer index (the primary handle for
// surfing) or the upstream stationuuid (useful for stable cross-refresh
// bookmarks). Integer wins when the token parses cleanly as one.
async function resolveStation(token) {
  const asInt = Number.parseInt(token, 10);
  if (Number.isInteger(asInt) && String(asInt) === token) {
    return getStation(asInt);
  }
  return getStationByUuid(token);
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
      if (url.pathname === '/api/health') {
        const ok = isReady();
        return sendJson(res, ok ? 200 : 503, { ok, cache: getCacheInfo() });
      }

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
          return sendJson(res, 200, await getNearest(lon, lat, limit), {
            'cache-control': 'public, max-age=60',
          });
        }
        const stations = await listStations();
        return sendJson(res, 200, { total: stations.length, stations }, {
          'cache-control': 'public, max-age=60',
        });
      }

      if (url.pathname === '/api/stations/random') {
        const station = await getRandomStation();
        if (!station) return sendJson(res, 503, { error: 'no stations cached yet' });
        return sendJson(res, 200, await withNowPlaying(station));
      }

      // /api/stations/:id/now-playing -- lightweight polling endpoint for a
      // client that already knows the station id and just wants the track.
      const npMatch = url.pathname.match(/^\/api\/stations\/([^/]+)\/now-playing$/);
      if (npMatch) {
        const station = await resolveStation(decodeURIComponent(npMatch[1]));
        if (!station) return notFound(res);
        const now_playing = await getNowPlaying(station);
        return sendJson(res, 200, { id: station.id, uuid: station.uuid, now_playing });
      }

      const stationMatch = url.pathname.match(/^\/api\/stations\/([^/]+)$/);
      if (stationMatch) {
        const station = await resolveStation(decodeURIComponent(stationMatch[1]));
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
