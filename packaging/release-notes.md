Native Debian packages are the recommended installation method for this release. They replace Debian's stock Marco packages cleanly and let `apt`/`dpkg` manage upgrades and removal.

This build targets **Debian 13 (trixie) on amd64** and contains the configurable **Select size** window menu plus **Workspace Expo** (`Super+E`).

## Install

Download these three runtime assets into one directory, then run:

```sh
sudo apt install ./libmarco-private2_*_amd64.deb ./marco-common_*_all.deb ./marco_*_amd64.deb
```

`libmarco-dev_*_amd64.deb` is optional and only needed for development against Marco's private library.

If this repository's shell installer was previously run with `--install-mate`, remove that `/usr/local` installation first so it cannot shadow the packaged `/usr/bin/marco`:

```sh
sudo ninja -C build-local uninstall
sudo glib-compile-schemas /usr/local/share/glib-2.0/schemas
sudo rm -f /etc/ld.so.conf.d/marco-pyrcephazine.conf
sudo ldconfig
```

After package installation, `command -v marco` should report `/usr/bin/marco`.

## Verify downloads

```sh
sha256sum --check SHA256SUMS
```

## Source build

The `marco-pyrcephazine-*.tar.xz` asset is provided as a secondary option for building with `install-marco-pyrcephazine.sh`. GitHub's automatically generated source archives refer to the same tagged commit.
