Marco for pyrcephazine
===

`marco-pyrcephazine` is a fork of Marco. It adds a couple of features I find useful.

Build:

```sh
./install-marco-pyrcephazine.sh
```

By default, the script only compiles Marco into `build-local`. It checks
the local Meson, Ninja, `pkg-config`, GLib/GIO, GTK, Pango, X11, XRes, and
schema compiler requirements before building. After a build-only run, test the
result without installing:

```sh
GSETTINGS_SCHEMA_DIR=$PWD/build-local/src ./build-local/src/marco --replace
```

Nested display test:

```sh
Xephyr :2 -screen 1280x800 -ac &
GSETTINGS_SCHEMA_DIR=$PWD/build-local/src DISPLAY=:2 ./build-local/src/marco --sm-disable --replace
DISPLAY=:2 zenity --info --text="Right-click my titlebar"
```

Install into MATE as the system Marco:

```sh
./install-marco-pyrcephazine.sh --install-mate
```

The install mode uses `/usr/local` by default. It builds, installs, compiles the
installed GSettings schema, writes an `/etc/ld.so.conf.d/marco-pyrcephazine.conf`
entry for the installed `libmarco-private.so`, runs `ldconfig`, and verifies
that the installed `marco` binary loads a library containing this fork's
`Change size` menu.

Useful options:

```sh
./install-marco-pyrcephazine.sh --build-dir build-local
./install-marco-pyrcephazine.sh --install-mate --prefix /usr/local
./install-marco-pyrcephazine.sh --help
```

After installing, check executable order:

```sh
which -a marco
```

`/usr/local/bin/marco` should appear before the system `/usr/bin/marco`. Also
check the shared library that will actually be used:

```sh
ldd /usr/local/bin/marco | grep libmarco
```

It should point at `/usr/local/lib/.../libmarco-private.so.2`. If it points at
`/lib` or `/usr/lib`, run:

```sh
sudo ldconfig
```

Configure the `Change size` submenu entries:

```sh
gsettings set org.mate.Marco.general change-size-resolutions "['800x600', '1024x768', '1280x720', '1920x1080']"
```

Reset them to the schema default:

```sh
gsettings reset org.mate.Marco.general change-size-resolutions
```

MATE Marco is a fork of GNOME Metacity.

COMPILING MARCO
===

You need Meson 0.47.0 or newer, GLib 2.58.0 or newer, GIO 2.25.10 or newer,
GTK+ 3.22 or newer, Pango 1.2.0 or newer, XRes 1.2.0 or newer, X11, and
libcanberra-gtk3. For startup notification to work you need
libstartup-notification 0.7 or newer from
http://www.freedesktop.org/software/startup-notification/.