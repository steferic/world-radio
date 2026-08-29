import { createServer } from './src/server.mjs';
import { ensureFresh } from './src/stations.mjs';

const PORT = process.env.PORT || 8787;

const server = createServer();
server.listen(PORT, () => {
  console.log(`[world-radio-api] listening on :${PORT}`);
  // Warm the cache in the background so the first client doesn't pay for a
  // round-trip to radio-browser's German mirror.
  ensureFresh().catch((err) =>
    console.warn('[world-radio-api] initial refresh failed:', err.message),
  );
});
