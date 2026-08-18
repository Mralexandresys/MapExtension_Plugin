export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
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


export function formatClockSeconds(totalSeconds: number | null | undefined): string {
  if (totalSeconds == null || !Number.isFinite(totalSeconds)) return '--:--';
  const safe = Math.max(0, Math.round(totalSeconds));
  const hours = Math.floor(safe / 3600);
  const minutes = Math.floor((safe % 3600) / 60);
  const seconds = safe % 60;

  if (hours > 0) {
    return [hours, minutes, seconds]
      .map((value) => String(value).padStart(2, '0'))
      .join(':');
  }

  return [minutes, seconds]
    .map((value) => String(value).padStart(2, '0'))
    .join(':');
}
