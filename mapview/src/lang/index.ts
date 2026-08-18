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
  try {
    const raw = localStorage.getItem(storageKey);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as { lang?: unknown };
    return normalizeLanguage(parsed.lang);
  } catch {
    return null;
  }
}

/** `?lang=fr` overrides the saved choice, which itself overrides the browser. */
export function resolveInitialLanguage(storageKey: string): Language {
  const params = new URLSearchParams(window.location.search);
  return (
    normalizeLanguage(params.get('lang')) ??
    getStoredLanguage(storageKey) ??
    normalizeLanguage(navigator.language) ??
    'en'
  );
}

export function getMessages(language: Language): Messages {
  return messages[language];
}

export function applyLanguage(language: Language): void {
  const current = getMessages(language);
  document.documentElement.lang = language;
  document.title = current.documentTitle;
}
