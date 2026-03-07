const PROTOCOL_RE = /^[a-z]+:\/\//i;

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
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  return (await response.json()) as T;
}
