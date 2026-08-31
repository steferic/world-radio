// Structured event sink. Currently just writes one JSON object per line to
// stdout so Render's log viewer captures it.

const MAX_TYPE_LEN = 64;
const MAX_SOURCE_LEN = 32;
const KNOWN_TYPES = new Set(['device.boot', 'station.change']);

function emit(record) {
  // One line per event -> log aggregators can parse without a multiline codec.
  process.stdout.write(JSON.stringify(record) + '\n');
}

// Validates an event coming off the wire and forwards it to the sink.
// Returns { ok: true } on accept, or { ok: false, error } for a 400.
// Never throws -- callers can treat this as infallible.
export function logEvent({ deviceId, sourceIp, body }) {
  if (!deviceId || typeof deviceId !== 'string' || deviceId.length > 128) {
    return { ok: false, error: 'missing or invalid X-Device-Id header' };
  }
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    return { ok: false, error: 'body must be a JSON object' };
  }
  const { type, ...rest } = body;
  if (typeof type !== 'string' || type.length === 0 || type.length > MAX_TYPE_LEN) {
    return { ok: false, error: "field 'type' is required (string)" };
  }
  // Not a hard failure -- we still log unknown types so a device shipping a
  // new event type doesn't get its data silently dropped just because the API
  // wasn't updated first. The `known: false` flag lets you filter/alert later.
  const known = KNOWN_TYPES.has(type);

  emit({
    ts: new Date().toISOString(),
    device_id: deviceId,
    source_ip: sourceIp || null,
    type,
    known,
    payload: rest,
  });
  return { ok: true };
}
