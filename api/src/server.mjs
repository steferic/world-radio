import { createServer as createHttpServer } from 'node:http';
import { getStations, getStation, getNearestStations, getCacheInfo, getRandomStation } from './stations.mjs';

function sendJson(res, status, body) {
  const data = JSON.stringify(body);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(data),
    'access-control-allow-origin': '*',
  });
  res.end(data);
}

function notFound(res) {
  sendJson(res, 404, { error: 'not found' });
}

export function createServer() {
  return createHttpServer(async (req, res) => {
    const url = new URL(req.url, `http://${req.headers.host}`);

    try {

      // GET basic health check
      if (url.pathname === '/api/health') {
        return sendJson(res, 200, { ok: true, cache: getCacheInfo() });
      }

      // GET list of stations
      if (url.pathname === '/api/stations') {
        const near = url.searchParams.get('near');
        if (near) {
          const [lon, lat] = near.split(',').map(Number);
          const limit = Number(url.searchParams.get('limit')) || 20;
          if (!Number.isFinite(lon) || !Number.isFinite(lat)) {
            return sendJson(res, 400, { error: 'Variable \'near\' must be \'lon,lat\'' });
          }
          return sendJson(res, 200, await getNearestStations(lon, lat, limit));
        }
        return sendJson(res, 200, await getStations());
      }

      // GET random station. This must come before GET single station.
      if (url.pathname === '/api/stations/random') {
        const station = await getRandomStation();
        if (!station) return sendJson(res, 503, { error: 'no stations cached yet' });
        return sendJson(res, 200, station);
      }

      // GET details on single station
      const stationMatch = url.pathname.match(/^\/api\/stations\/([^/]+)$/);
      if (stationMatch) {
        const station = await getStation(decodeURIComponent(stationMatch[1]));
        if (!station) return notFound(res);
        return sendJson(res, 200, station);
      }

      return notFound(res);
    } catch (err) {
      console.error('[world-radio-api] error:', err);
      return sendJson(res, 502, { error: 'upstream fetch failed', detail: err.message });
    }
  });
}
