#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_SUMMARY="$SCRIPT_DIR/summarize_build_log.py"

usage() {
  cat <<'EOF'
Usage: ./summarize_build.sh <client|server|logfile> [--all-warnings]

Examples:
  ./summarize_build.sh client
  ./summarize_build.sh server
  ./summarize_build.sh build_client.log
  ./summarize_build.sh build_server.log --all-warnings
EOF
}

if [[ $# -lt 1 ]]; then
  usage >&2
  exit 1
fi

TARGET="$1"
shift

case "$TARGET" in
  client)
    LOG_PATH="$SCRIPT_DIR/build_client.log"
    ;;
  server)
    LOG_PATH="$SCRIPT_DIR/build_server.log"
    ;;
  -h|--help|help)
    usage
    exit 0
    ;;
  *)
    LOG_PATH="$TARGET"
    ;;
esac

if [[ ! -x "$PY_SUMMARY" ]]; then
  if [[ ! -f "$LOG_PATH" ]]; then
    echo "Log introuvable: $LOG_PATH" >&2
    exit 1
  fi

  ALL_WARNINGS=false
  for arg in "$@"; do
    if [[ "$arg" == "--all-warnings" ]]; then
      ALL_WARNINGS=true
    fi
  done

  echo "Build log: $LOG_PATH"

  if grep -aEq "^[[:space:]]*0 (Erreur|Error)\(s\)" "$LOG_PATH" && ! grep -aEq "La generation a echoue|La génération a échoué|Build FAILED|failed" "$LOG_PATH"; then
    echo "Status: success"
  elif grep -aEq "La generation a echoue|La génération a échoué|Build FAILED|failed" "$LOG_PATH"; then
    echo "Status: failed"
  else
    echo "Status: unknown"
  fi

  grep -aE "^[[:space:]]*[0-9]+ (Avertissement|Warning|Erreur|Error)\(s\)" "$LOG_PATH" || true

  ERROR_LINES="$(grep -aEn "(^|[^[:alpha:]])(error C[0-9]+|fatal error|: error )" "$LOG_PATH" || true)"
  if [[ -n "$ERROR_LINES" ]]; then
    echo
    echo "Errors:"
    printf '%s\n' "$ERROR_LINES"
  fi

  WARNING_LINES="$(grep -aEn "(^|[^[:alpha:]])(warning C[0-9]+|: warning )" "$LOG_PATH" || true)"
  if [[ -n "$WARNING_LINES" ]]; then
    echo
    if $ALL_WARNINGS; then
      echo "Warnings:"
      printf '%s\n' "$WARNING_LINES"
    else
      echo "Warnings (first 20, pass --all-warnings for all):"
      printf '%s\n' "$WARNING_LINES" | head -n 20
    fi
  fi

  exit 0
fi

exec "$PY_SUMMARY" "$LOG_PATH" "$@"
