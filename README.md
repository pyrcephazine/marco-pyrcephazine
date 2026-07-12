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
`Select size` menu.

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

Configure the `Select size` submenu entries:

```sh
gsettings set org.mate.Marco.general change-size-resolutions "['800x600', '1024x768', '1280x720', '1920x1080']"
```

Only configured sizes that fit on the monitor containing the window are shown.
For example, larger presets such as `2048x1536` (twice `1024x768`) appear only
when that monitor is large enough.

Reset them to the schema default:

```sh
gsettings reset org.mate.Marco.general change-size-resolutions
```

Workspace Expo
---

Press `Super+E` to cover the desktop with an overview of every configured
workspace. Click a workspace to switch to it, click a window preview to switch
and focus that window, or drag a preview to another workspace. Expo remains
open after a drag so several windows can be organized in one session. Arrow
keys change the selected workspace, `Enter` opens it, and `Escape` or
`Super+E` closes the overview.

With compositing enabled, Expo uses snapshots of the windows as they appeared
when the overview opened. If a snapshot is unavailable or compositing is
disabled, the same interface remains usable with window icons.

Change or disable the shortcut through MATE's keyboard-shortcut settings, or
directly with GSettings:

```sh
gsettings set org.mate.Marco.global-keybindings show-workspace-expo '<Super>e'
gsettings set org.mate.Marco.global-keybindings show-workspace-expo 'disabled'
```

MATE Marco is a fork of GNOME Metacity.

COMPILING MARCO
===

You need Meson 0.47.0 or newer, GLib 2.58.0 or newer, GIO 2.25.10 or newer,
GTK+ 3.22 or newer, Pango 1.2.0 or newer, XRes 1.2.0 or newer, X11, and
libcanberra-gtk3. For startup notification to work you need
libstartup-notification 0.7 or newer from
http://www.freedesktop.org/software/startup-notification/.
