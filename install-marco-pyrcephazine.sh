#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build-local"
PREFIX="/usr/local"
INSTALL_MATE=0

usage() {
  cat <<'EOF'
Usage: ./install-marco-pyrcephazine.sh [OPTIONS]

Build Marco Pyrcephazine by default. Use --install-mate to install it as the
Marco used by MATE sessions on this machine.

Options:
  --build-only          Build only. This is the default.
  --install-mate        Build, install, compile schemas, and refresh ldconfig.
  --build-dir DIR       Build directory. Default: build-local
  --prefix PREFIX       Install prefix. Default: /usr/local
  -h, --help            Show this help.

Examples:
  ./install-marco-pyrcephazine.sh
  ./install-marco-pyrcephazine.sh --install-mate
  ./install-marco-pyrcephazine.sh --install-mate --prefix /usr/local
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '==> %s\n' "$*"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

version_ge() {
  [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" = "$2" ]
}

check_cmd_version() {
  cmd="$1"
  required="$2"
  actual="$($cmd --version | head -n 1 | grep -Eo '[0-9]+([.][0-9]+)+' | head -n 1)"

  [ -n "$actual" ] || die "could not determine $cmd version"
  version_ge "$actual" "$required" ||
    die "$cmd $required or newer is required; found $actual"
}

check_pkg() {
  pkg="$1"
  required="${2:-}"

  if [ -n "$required" ]; then
    pkg-config --atleast-version "$required" "$pkg" ||
      die "$pkg $required or newer is required"
  else
    pkg-config --exists "$pkg" ||
      die "$pkg is required"
  fi
}

check_optional_pkg() {
  pkg="$1"
  required="${2:-}"

  if [ -n "$required" ]; then
    if ! pkg-config --atleast-version "$required" "$pkg"; then
      printf 'warning: optional dependency missing or too old: %s >= %s\n' "$pkg" "$required" >&2
    fi
  elif ! pkg-config --exists "$pkg"; then
    printf 'warning: optional dependency missing: %s\n' "$pkg" >&2
  fi
}

sudo_cmd() {
  if [ "${EUID:-$(id -u)}" -eq 0 ]; then
    "$@"
  else
    sudo "$@"
  fi
}

configure_build() {
  if [ -d "$BUILD_DIR" ]; then
    meson setup "$BUILD_DIR" --prefix "$PREFIX" --reconfigure
  else
    meson setup "$BUILD_DIR" --prefix "$PREFIX"
  fi
}

write_ld_config() {
  lib_dirs="$(find "$PREFIX/lib" -type f -name 'libmarco-private.so*' -printf '%h\n' 2>/dev/null | sort -u)"

  [ -n "$lib_dirs" ] || die "installed libmarco-private was not found under $PREFIX/lib"

  tmp_conf="$(mktemp)"
  printf '%s\n' "$lib_dirs" > "$tmp_conf"
  sudo_cmd install -m 0644 "$tmp_conf" /etc/ld.so.conf.d/marco-pyrcephazine.conf
  rm -f "$tmp_conf"
  sudo_cmd ldconfig
}

verify_installed_marco() {
  marco_bin="$PREFIX/bin/marco"
  schema_file="$PREFIX/share/glib-2.0/schemas/org.mate.marco.gschema.xml"

  [ -x "$marco_bin" ] || die "installed marco binary not found at $marco_bin"
  [ -f "$schema_file" ] || die "installed schema not found at $schema_file"
  grep -q 'change-size-resolutions' "$schema_file" ||
    die "installed schema does not contain change-size-resolutions"

  linked_lib="$(ldd "$marco_bin" | awk '/libmarco-private\.so/ { print $3; exit }')"
  [ -n "$linked_lib" ] || die "could not determine which libmarco-private $marco_bin loads"
  [ -f "$linked_lib" ] || die "linked libmarco-private not found: $linked_lib"

  if ! strings "$linked_lib" | grep -q 'Change _size'; then
    die "$marco_bin is loading $linked_lib, which does not contain the Change size menu"
  fi

  log "$marco_bin loads $linked_lib"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-only)
      INSTALL_MATE=0
      ;;
    --install-mate)
      INSTALL_MATE=1
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || die "--build-dir requires a value"
      BUILD_DIR="$2"
      shift
      ;;
    --prefix)
      [ "$#" -ge 2 ] || die "--prefix requires a value"
      PREFIX="$2"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

cd "$(dirname "$0")"

[ -f meson.build ] || die "run this script from the repository root"
[ -f src/core/core.c ] || die "src/core/core.c is missing; resolve any conflict files before building"
[ -f src/core/prefs.c ] || die "src/core/prefs.c is missing; resolve any conflict files before building"

need_cmd meson
need_cmd ninja
need_cmd pkg-config
need_cmd glib-compile-schemas
need_cmd sort
need_cmd strings

if [ "$INSTALL_MATE" -eq 1 ]; then
  need_cmd sudo
  need_cmd ldd
  need_cmd awk
  need_cmd find
  need_cmd install
  need_cmd mktemp
fi

check_cmd_version meson 0.47.0

check_pkg glib-2.0 2.58.0
check_pkg gio-2.0 2.25.10
check_pkg gtk+-3.0 3.22.0
check_pkg pango 1.2.0
check_pkg libcanberra-gtk3
check_pkg xres 1.2.0
check_pkg x11

check_optional_pkg libstartup-notification-1.0 0.7
check_optional_pkg xcomposite 0.3
check_optional_pkg xrender 0.0
check_optional_pkg xcursor
check_optional_pkg xrandr
check_optional_pkg xpresent
check_optional_pkg xinerama
check_optional_pkg sm

log "configuring $BUILD_DIR with prefix $PREFIX"
configure_build

log "building"
ninja -C "$BUILD_DIR"

log "validating build-tree schemas"
glib-compile-schemas --strict --dry-run src

if [ "$INSTALL_MATE" -eq 0 ]; then
  cat <<EOF

Build complete.

Test without installing:
  GSETTINGS_SCHEMA_DIR=$PWD/$BUILD_DIR/src DISPLAY=\${DISPLAY:-:0} $PWD/$BUILD_DIR/src/marco --replace

For a safer nested X test:
  Xephyr :2 -screen 1280x800 -ac &
  GSETTINGS_SCHEMA_DIR=$PWD/$BUILD_DIR/src DISPLAY=:2 $PWD/$BUILD_DIR/src/marco --sm-disable --replace
  DISPLAY=:2 zenity --info --text="Right-click my titlebar"

Install into MATE:
  ./install-marco-pyrcephazine.sh --install-mate
EOF
  exit 0
fi

log "installing to $PREFIX"
sudo_cmd meson install -C "$BUILD_DIR"

log "compiling installed GSettings schemas"
sudo_cmd glib-compile-schemas "$PREFIX/share/glib-2.0/schemas"

log "refreshing dynamic linker configuration"
write_ld_config

log "verifying installed Marco"
verify_installed_marco

cat <<EOF

Install complete.

Confirm PATH order:
  which -a marco

Start the installed Marco now:
  $PREFIX/bin/marco --replace

Configure Change size entries:
  gsettings set org.mate.Marco.general change-size-resolutions "['800x600', '1024x768', '1280x720', '1920x1080']"
EOF
