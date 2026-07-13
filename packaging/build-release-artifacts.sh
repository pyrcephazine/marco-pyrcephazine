#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RELEASE_TAG="${1:-}"
OUTPUT_DIR="${2:-$ROOT_DIR/dist}"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

usage() {
  printf 'Usage: %s RELEASE_TAG [OUTPUT_DIR]\n' "${0##*/}"
}

[ -n "$RELEASE_TAG" ] || {
  usage >&2
  exit 2
}

case "$RELEASE_TAG" in
  v[0-9]*-pyrcephazine.[0-9]*) ;;
  *) die "release tag must look like v1.26.2-pyrcephazine.1" ;;
esac

for cmd in dpkg dpkg-architecture dpkg-buildpackage dpkg-deb dpkg-parsechangelog find git grep mktemp readelf sha256sum strings tar xz; do
  need_cmd "$cmd"
done

DEBIAN_VERSION="$(dpkg-parsechangelog -l "$ROOT_DIR/debian/changelog" -S Version)"
EXPECTED_RELEASE="${DEBIAN_VERSION/+/-}"
[ "${RELEASE_TAG#v}" = "$EXPECTED_RELEASE" ] ||
  die "tag $RELEASE_TAG does not match Debian version $DEBIAN_VERSION"

COMMIT="$(git -C "$ROOT_DIR" rev-parse --verify "$RELEASE_TAG^{commit}")" ||
  die "tag not found: $RELEASE_TAG"
ARCH="$(dpkg-architecture -qDEB_HOST_ARCH)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
ARCHIVE_BASENAME="marco-pyrcephazine-${RELEASE_TAG#v}"

if [ -d "$OUTPUT_DIR" ] && [ -n "$(find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  die "output directory is not empty: $OUTPUT_DIR"
fi
mkdir -p "$OUTPUT_DIR"

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

printf '==> Archiving %s (%s)\n' "$RELEASE_TAG" "$COMMIT"
git -C "$ROOT_DIR" archive \
  --format=tar \
  --prefix="$ARCHIVE_BASENAME/" \
  --output="$TEMP_DIR/$ARCHIVE_BASENAME.tar" \
  "$COMMIT"
xz --threads=0 -9e "$TEMP_DIR/$ARCHIVE_BASENAME.tar"
tar -xJf "$TEMP_DIR/$ARCHIVE_BASENAME.tar.xz" -C "$TEMP_DIR"

BUILD_OPTIONS="${DEB_BUILD_OPTIONS:-}"
case " $BUILD_OPTIONS " in
  *" noddebs "*) ;;
  *) BUILD_OPTIONS="${BUILD_OPTIONS:+$BUILD_OPTIONS }noddebs" ;;
esac

printf '==> Building Debian packages for %s\n' "$ARCH"
(
  cd "$TEMP_DIR/$ARCHIVE_BASENAME"
  export DEB_BUILD_OPTIONS="$BUILD_OPTIONS"
  dpkg-buildpackage --build=binary --no-sign
)

if command -v lintian >/dev/null 2>&1; then
  printf '==> Running Lintian\n'
  lintian --fail-on error "$TEMP_DIR"/*.changes
fi

cp "$TEMP_DIR"/*.deb "$OUTPUT_DIR/"
cp "$TEMP_DIR/$ARCHIVE_BASENAME.tar.xz" "$OUTPUT_DIR/"

EXPECTED_PACKAGES=(
  "libmarco-dev_${DEBIAN_VERSION}_${ARCH}.deb"
  "libmarco-private2_${DEBIAN_VERSION}_${ARCH}.deb"
  "marco-common_${DEBIAN_VERSION}_all.deb"
  "marco_${DEBIAN_VERSION}_${ARCH}.deb"
)

mapfile -t BUILT_PACKAGES < <(find "$OUTPUT_DIR" -maxdepth 1 -type f -name '*.deb' -printf '%f\n' | sort)
[ "${#BUILT_PACKAGES[@]}" -eq "${#EXPECTED_PACKAGES[@]}" ] ||
  die "expected four binary packages; found ${#BUILT_PACKAGES[@]}"

for package_file in "${EXPECTED_PACKAGES[@]}"; do
  [ -f "$OUTPUT_DIR/$package_file" ] || die "missing package: $package_file"
  [ "$(dpkg-deb -f "$OUTPUT_DIR/$package_file" Version)" = "$DEBIAN_VERSION" ] ||
    die "wrong version in $package_file"
  [ "$(dpkg-deb -f "$OUTPUT_DIR/$package_file" Source)" = "marco-pyrcephazine" ] ||
    die "wrong source package in $package_file"
done

INSPECT_DIR="$TEMP_DIR/inspect"
mkdir -p "$INSPECT_DIR"
dpkg-deb --extract "$OUTPUT_DIR/libmarco-private2_${DEBIAN_VERSION}_${ARCH}.deb" "$INSPECT_DIR"
LIBRARY="$(find "$INSPECT_DIR/usr/lib/$MULTIARCH" -type f -name 'libmarco-private.so.2.*' -print -quit)"
[ -n "$LIBRARY" ] || die "packaged libmarco-private was not found"
readelf -d "$LIBRARY" | grep 'SONAME.*libmarco-private.so.2' >/dev/null ||
  die "packaged library has the wrong SONAME"
strings "$LIBRARY" | grep -F 'Select _size' >/dev/null ||
  die "packaged library does not contain the Select size menu"
strings "$LIBRARY" | grep -F 'show-workspace-expo' >/dev/null ||
  die "packaged library does not contain Workspace Expo"

dpkg-deb --extract "$OUTPUT_DIR/marco-common_${DEBIAN_VERSION}_all.deb" "$INSPECT_DIR"
SCHEMA="$INSPECT_DIR/usr/share/glib-2.0/schemas/org.mate.marco.gschema.xml"
grep -q 'change-size-resolutions' "$SCHEMA" || die "Select size schema key is missing"
grep -q 'show-workspace-expo' "$SCHEMA" || die "Workspace Expo schema key is missing"

MARCO_DEB="$OUTPUT_DIR/marco_${DEBIAN_VERSION}_${ARCH}.deb"
dpkg-deb -f "$MARCO_DEB" Depends | grep -F "libmarco-private2 (= $DEBIAN_VERSION)" >/dev/null ||
  die "marco does not require its matching private library"
dpkg-deb -f "$MARCO_DEB" Depends | grep -F "marco-common (= $DEBIAN_VERSION)" >/dev/null ||
  die "marco does not require its matching common package"

(
  cd "$OUTPUT_DIR"
  sha256sum ./*.deb ./*.tar.xz > SHA256SUMS
)

printf '==> Release artifacts written to %s\n' "$OUTPUT_DIR"
printf '    %s\n' "${EXPECTED_PACKAGES[@]}" "$ARCHIVE_BASENAME.tar.xz" SHA256SUMS
