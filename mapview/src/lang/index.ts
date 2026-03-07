import { en } from './en';
import { fr } from './fr';

const messages = {
  en,
  fr,
} as const;

export type Language = keyof typeof messages;
export type Messages = (typeof messages)[Language];

function normalizeLanguage(value: unknown): Language | null {
  if (typeof value !== 'string') return null;
  const normalized = value.trim().toLowerCase();
  if (normalized.startsWith('fr')) return 'fr';
  if (normalized.startsWith('en')) return 'en';
  return null;
}

function getStoredLanguage(storageKey: string): Language | null {
  if (typeof localStorage === 'undefined') return null;

  try {
    const raw = localStorage.getItem(storageKey);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as { lang?: unknown };
    return normalizeLanguage(parsed.lang);
  } catch {
    return null;
  }
}

export function resolveInitialLanguage(storageKey: string): Language {
  if (typeof window !== 'undefined') {
    const params = new URLSearchParams(window.location.search);
    const queryLanguage = normalizeLanguage(params.get('lang') ?? params.get('LANG'));
    if (queryLanguage) return queryLanguage;
  }

  const globalState = globalThis as { LANG?: unknown; MAPVIEW_LANG?: unknown };
  const globalLanguage = normalizeLanguage(globalState.MAPVIEW_LANG ?? globalState.LANG);
  if (globalLanguage) return globalLanguage;

  const storedLanguage = getStoredLanguage(storageKey);
  if (storedLanguage) return storedLanguage;

  if (typeof document !== 'undefined') {
    const documentLanguage = normalizeLanguage(document.documentElement.lang);
    if (documentLanguage) return documentLanguage;
  }

  if (typeof navigator !== 'undefined') {
    const browserLanguage = normalizeLanguage(navigator.language);
    if (browserLanguage) return browserLanguage;
  }

  return 'en';
}

export function getMessages(language: Language): Messages {
  return messages[language];
}

export function applyLanguage(language: Language): void {
  if (typeof document === 'undefined') return;

  const current = getMessages(language);
  document.documentElement.lang = language;
  document.title = current.documentTitle;
}
