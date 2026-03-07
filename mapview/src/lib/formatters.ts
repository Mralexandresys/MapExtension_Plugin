export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export function normalizeText(value: unknown): string {
  return String(value ?? '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .trim();
}

export function formatWorld(value?: string, fallback = 'Unknown'): string {
  return value || fallback;
}

export function formatRelativeAge(
  timestamp: number,
  now: number,
  locale = 'en-US',
  prefix = 'Last update: ',
  missingLabel = 'Last update: --',
): string {
  if (!timestamp) return missingLabel;
  const deltaSeconds = Math.max(0, Math.round((now - timestamp) / 100) / 10);
  const formatted = new Intl.NumberFormat(locale, {
    minimumFractionDigits: 1,
    maximumFractionDigits: 1,
  }).format(deltaSeconds);
  return `${prefix}${formatted}s`;
}

export function formatNumber(
  value: number | string | null | undefined,
  locale = 'en-US',
): string {
  if (value == null || value === '') return '--';
  const numeric = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(numeric)) return String(value);
  return new Intl.NumberFormat(locale, {
    maximumFractionDigits: 1,
  }).format(numeric);
}
