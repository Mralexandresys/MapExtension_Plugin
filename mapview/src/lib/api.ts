const PROTOCOL_RE = /^[a-z]+:\/\//i;
/** A plugin that accepts the connection but never answers must not stall polling. */
const REQUEST_TIMEOUT_MS = 5000;

export function normalizeEndpoint(value: string): string {
  const trimmed = value.trim();
  if (!trimmed) return '';
  const withProtocol = PROTOCOL_RE.test(trimmed)
    ? trimmed
    : `http://${trimmed}`;
  return withProtocol.replace(/\/+$/, '');
}

export async function fetchJson<T>(endpoint: string, path: string): Promise<T> {
  const base = normalizeEndpoint(endpoint);
  if (!base) {
    throw new Error('Endpoint vide');
  }

  const response = await fetch(`${base}${path}`, {
    cache: 'no-store',
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  return (await response.json()) as T;
}
