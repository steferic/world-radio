import { createServer } from './src/server.mjs';
import { ensureFresh } from './src/stations.mjs';

const PORT = process.env.PORT || 8787;

const server = createServer();
server.listen(PORT, () => {
  console.log(`[world-radio-api] listening on :${PORT}`);
  ensureFresh().catch((err) =>
    console.warn('[world-radio-api] initial refresh failed:', err.message),
  );
});
