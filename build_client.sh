#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOLUTION_PATH="$SCRIPT_DIR/MapExtension_Plugin.sln"
VSWWHERE_PATH="/mnt/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
DEFAULT_LOG_PATH="$SCRIPT_DIR/build_client.log"
DEFAULT_SDK_ROOT="$SCRIPT_DIR/../StarRupture-Plugin-SDK"

usage() {
  cat <<'EOF'
Usage: ./build_client.sh <debug|release> [--sdk-root <path>] [--log] [--log-file <path>] [--summary] [--build-tag <tag>] [--build-author <author>]

Builds the MapExtension_Plugin C++ project from the plugin repository using Windows MSBuild.
EOF
}

find_msbuild() {
  local msbuild_win_path=""

  if [[ -x "$VSWWHERE_PATH" ]]; then
    msbuild_win_path="$("$VSWWHERE_PATH" -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | tr -d '\r' | head -n 1)"
  fi

  if [[ -n "$msbuild_win_path" ]]; then
    wslpath -u "$msbuild_win_path"
    return 0
  fi

  local candidate=""
  for candidate in \
    "/mnt/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/18/Enterprise/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/18/BuildTools/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/MSBuild.exe" \
    "/mnt/c/Program Files/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe"
  do
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

if [[ $# -lt 1 ]]; then
  usage >&2
  exit 1
fi

BUILD_KIND=""
WRITE_LOG=false
SHOW_SUMMARY=false
LOG_PATH="$DEFAULT_LOG_PATH"
BUILD_TAG=""
BUILD_AUTHOR=""
SDK_ROOT="$DEFAULT_SDK_ROOT"

while [[ $# -gt 0 ]]; do
  ARG="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  case "$ARG" in
    debug|release)
      if [[ -n "$BUILD_KIND" ]]; then
        echo "Configuration deja definie: $BUILD_KIND" >&2
        exit 1
      fi
      BUILD_KIND="$ARG"
      shift
      ;;
    --sdk-root)
      if [[ $# -lt 2 ]]; then
        echo "Option --sdk-root sans chemin." >&2
        exit 1
      fi
      SDK_ROOT="$2"
      shift 2
      ;;
    --log)
      WRITE_LOG=true
      shift
      ;;
    --log-file)
      if [[ $# -lt 2 ]]; then
        echo "Option --log-file sans chemin." >&2
        exit 1
      fi
      WRITE_LOG=true
      LOG_PATH="$2"
      shift 2
      ;;
    --summary)
      WRITE_LOG=true
      SHOW_SUMMARY=true
      shift
      ;;
    --build-tag)
      if [[ $# -lt 2 ]]; then
        echo "Option --build-tag sans valeur." >&2
        exit 1
      fi
      BUILD_TAG="$2"
      shift 2
      ;;
    --build-author)
      if [[ $# -lt 2 ]]; then
        echo "Option --build-author sans valeur." >&2
        exit 1
      fi
      BUILD_AUTHOR="$2"
      shift 2
      ;;
    -h|--help|help)
      usage
      exit 0
      ;;
    *)
      echo "Argument invalide: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$BUILD_KIND" ]]; then
  echo "Configuration manquante: debug ou release." >&2
  usage >&2
  exit 1
fi

case "$BUILD_KIND" in
  debug)
    CONFIGURATION="Client Debug"
    ;;
  release)
    CONFIGURATION="Client Release"
    ;;
esac

if [[ ! -f "$SOLUTION_PATH" ]]; then
  echo "Solution introuvable: $SOLUTION_PATH" >&2
  exit 1
fi

if [[ ! -d "$SDK_ROOT" ]]; then
  echo "SDK introuvable: $SDK_ROOT" >&2
  exit 1
fi

if [[ ! -d "$SDK_ROOT/include" ]]; then
  echo "SDK incomplet: $SDK_ROOT/include introuvable" >&2
  exit 1
fi

if [[ ! -d "$SDK_ROOT/StarRupture SDK" ]]; then
  echo "SDK incomplet: $SDK_ROOT/StarRupture SDK introuvable" >&2
  exit 1
fi

if ! command -v wslpath >/dev/null 2>&1; then
  echo "wslpath est requis pour convertir les chemins du projet." >&2
  exit 1
fi

MSBUILD_PATH="$(find_msbuild)" || {
  echo "MSBuild.exe introuvable. Installe Visual Studio 2022 ou Build Tools avec MSBuild." >&2
  exit 1
}

SOLUTION_WIN_PATH="$(wslpath -w "$SOLUTION_PATH")"
PLUGIN_ROOT_WIN_PATH="$(wslpath -w "$SCRIPT_DIR")"
SDK_ROOT_WIN_PATH="$(wslpath -w "$SDK_ROOT")"

echo "Build de MapExtension_Plugin en ${CONFIGURATION}|x64"
echo "MSBuild : $MSBUILD_PATH"
echo "SDK : $SDK_ROOT"

BUILD_CMD=(
  "$MSBUILD_PATH"
  "$SOLUTION_WIN_PATH"
  /t:MapExtension_Plugin
  /m
  /p:Configuration="$CONFIGURATION"
  /p:Platform=x64
  /p:SolutionDir="$PLUGIN_ROOT_WIN_PATH\\"
  /p:RepoRootDir="$PLUGIN_ROOT_WIN_PATH\\"
  /p:PluginSdkRootDir="$SDK_ROOT_WIN_PATH\\"
)

if [[ -n "$BUILD_TAG" ]]; then
  BUILD_CMD+=(/p:ModLoaderBuildTag="$BUILD_TAG")
fi

if [[ -n "$BUILD_AUTHOR" ]]; then
  BUILD_CMD+=(/p:ModLoaderBuildAuthor="$BUILD_AUTHOR")
fi

if $WRITE_LOG; then
  echo "Log : $LOG_PATH"
  mkdir -p "$(dirname "$LOG_PATH")"
  LOG_WIN_PATH="$(wslpath -w "$LOG_PATH")"
  BUILD_CMD+=(/fileLogger "/fileLoggerParameters:LogFile=$LOG_WIN_PATH;Verbosity=normal")
  TMP_LOG="$(mktemp)"
  persist_log() {
    if [[ -z "${TMP_LOG:-}" || ! -f "$TMP_LOG" ]]; then
      return
    fi
    if [[ -s "$LOG_PATH" ]]; then
      rm -f "$TMP_LOG"
      return
    fi
    if ! mv "$TMP_LOG" "$LOG_PATH" 2>/dev/null; then
      cp "$TMP_LOG" "$LOG_PATH"
      rm -f "$TMP_LOG"
    fi
  }
  trap persist_log EXIT
  set +e
  "${BUILD_CMD[@]}" 2>&1 | tee "$TMP_LOG"
  BUILD_STATUS=${PIPESTATUS[0]}
  set -e

  persist_log
  trap - EXIT

  if $SHOW_SUMMARY; then
    echo
    if [[ -x "$SCRIPT_DIR/../summarize_build.sh" ]]; then
      "$SCRIPT_DIR/../summarize_build.sh" "$LOG_PATH"
    else
      echo "summarize_build.sh introuvable dans le repo parent; resume ignore." >&2
    fi
  fi

  exit "$BUILD_STATUS"
fi

"${BUILD_CMD[@]}"
