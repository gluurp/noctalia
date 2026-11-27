# noctalia (local fork)

Fork of [noctalia-dev/noctalia](https://github.com/noctalia-dev/noctalia), MIT
licensed. The `local` branch is based on v5.6.0 and adds four changes:

1. **Audio stream descriptors**: the Application Volumes panel shows where each
   stream comes from and which sink it plays to, alongside the app name.
2. **Spectrum visualizer fixes**: selectable target node, inspector visualizer
   source, and per-band FFT power instead of a centre-bin sample.
3. **Transparent sysmon graph background**: the plot follows the bar capsule
   instead of painting an opaque `surface_container` box.
4. **Super reveal**: holding Super shows the bar, release hides it after a
   workspace change, the key is read from `/dev/input` (no Hyprland binds), and
   `reveal_on_super_delay_ms` prevents quick shortcuts from flashing the bar.
   Smart auto-hide treats an open scratchpad/special workspace as visible
   content, per output; `smart_auto_hide_workspaces` can restrict this to named
   special workspaces such as `special:special` or `special:scratchpad`.

Machine config and detailed notes live in
[gluurp/software/noctalia](https://github.com/gluurp/software/tree/main/noctalia).

## Build and install

Requires `meson`, `ninja`, `pkg-config`, and Noctalia's build dependencies; see
upstream `BUILDING.md`. The script builds the branch in `build/` and installs to
`/usr/local` (so it shadows a packaged `noctalia` in `PATH`).

```sh
./install.sh              # build and install, using sudo for the install step
./install.sh --build-only # compile without installing
./install.sh --prefix /usr
```

Then restart the shell:

```sh
systemctl --user restart noctalia-shell.service
```

## Update

```sh
git fetch upstream --tags
git rebase v<new-version> local
# resolve conflicts, then:
git push --force-with-lease origin local
```

The rebased branch is the source of truth. `gluurp/software/noctalia` carries the
matching patch series and the local build script for machines that prefer the
patch workflow.

Upstream documentation: <https://github.com/noctalia-dev/noctalia#readme>
