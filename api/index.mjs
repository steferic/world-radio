// API entry point.
import { createServer } from './src/server.mjs';

const PORT = process.env.PORT || 8787;

const server = createServer();
server.listen(PORT, () => {
  console.log(`[world-radio-api] listening on :${PORT}`);
});
