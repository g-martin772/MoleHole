#!/usr/bin/env bash
# Installs the build into a temp prefix with isolated XDG dirs and runs the installed binary headlessly.
# Usage: tools/install_smoke.sh [build-dir]   (env: SMOKE_TIMEOUT seconds per run, default 300; KEEP=1 keeps the temp dir)
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
TIMEOUT="${SMOKE_TIMEOUT:-300}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/molehole-install-smoke.XXXXXX")"
PREFIX="$TMP/prefix"
HOME_DIR="$TMP/xdg/home"
mkdir -p "$HOME_DIR" "$TMP/xdg"/{config,data,state,cache}
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; echo "log dir: $TMP" >&2; trap - EXIT; exit 1; }

MOLEHOLE_SKIP_DESKTOP=1 cmake --install "$BUILD" --prefix "$PREFIX" > "$TMP/install.log" 2>&1 || fail "cmake --install ($TMP/install.log)"

BIN="$PREFIX/bin/molehole"
SHARE="$PREFIX/share/molehole"
for path in "$BIN" "$PREFIX/lib/molehole/MoleHole" "$SHARE/config.json" "$SHARE/shaders/pbr" "$SHARE/templates/dev.yaml" \
            "$SHARE/scripts" "$SHARE/combos" "$SHARE/font" "$SHARE/assets/models"; do
    [ -e "$path" ] || fail "missing from install: ${path#$PREFIX/}"
done

run_installed() {
    (cd "$HOME_DIR" && env -i PATH="$PATH" HOME="$HOME_DIR" XDG_CONFIG_HOME="$TMP/xdg/config" XDG_DATA_HOME="$TMP/xdg/data" \
        XDG_STATE_HOME="$TMP/xdg/state" XDG_CACHE_HOME="$TMP/xdg/cache" XDG_DOCUMENTS_DIR="$HOME_DIR/Documents" \
        WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" DISPLAY="${DISPLAY:-}" \
        timeout "$TIMEOUT" "$BIN" "$@")
}

SCENE="$SHARE/templates/dev.yaml"
out="$(run_installed --validate-scene "$SCENE" 2>&1)"; code=$?
[ $code -eq 0 ] || fail "--validate-scene exit $code: $out"
grep -q "0 errors" <<< "$out" || fail "--validate-scene reported errors: $out"

run_installed --export-image "$TMP/out.png" --width 480 --height 270 --scene "$SCENE" > "$TMP/export.log" 2>&1; code=$?
[ $code -eq 0 ] || fail "--export-image exit $code ($TMP/export.log)"
[ -s "$TMP/out.png" ] || fail "export produced no image"
grep -q '\[error\]' "$TMP/export.log" && fail "[error] lines in export log ($TMP/export.log)"
grep -q "Loaded glTF '$SHARE/assets/" "$TMP/export.log" || fail "template mesh was not resolved against the installed assets"
grep -q "$ROOT" "$TMP/export.log" && fail "installed run touched the source tree ($ROOT)"
[ -s "$TMP/xdg/state/molehole/logs/app.log" ] || fail "no log file under XDG_STATE_HOME"
[ -d "$HOME_DIR/Documents/MoleHole/scenes" ] || fail "user data dirs were not created"

echo "OK: installed layout validates and exports ($TMP/out.png)"
[ "${KEEP:-0}" = 1 ] && echo "kept: $TMP"
exit 0
