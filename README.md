# noctalia (local fork)

Fork of [noctalia-dev/noctalia](https://github.com/noctalia-dev/noctalia), MIT
licensed. The `local` branch is based on v5.6.0 and adds five changes:

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
5. **Linked wallpapers and per-wallpaper colors**: link monitor folders whose
   files share exact relative paths, browse each set as one synthetic folder,
   apply all matching monitor files in one write, optionally hide the original
   member folders (including parent folders and symlinked originals), clean
   stale wallpaper references, use bookmarks instead of favorite color
   snapshots, and override colors globally, per linked set, or per wallpaper.
   Applying from a symlinked original still resolves the linked set. Missing
   targets leave the other monitors untouched and raise a notification.
   Settings rejects duplicate monitor members. The panel has a visible
   Back · folder button (Escape also goes up one level), a check badge on the
   selected/current wallpaper that stays visible on dark thumbnails, and a
   Global scope button that removes every override affecting the image. A
   per-image override applies even inside a linked set. Settings > Wallpaper >
   Colors for each wallpaper edits the active wallpaper or a saved
   `[[wallpaper.entry]]`; entries are independent of favorites. The original
   Super+Shift+W menu controls stay in place, with one added
   **Colors for this wallpaper…** button. Saved palettes are Custom snapshots.

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

## NixOS and Home Manager

The flake here is upstream's flake; only the source on `local` differs. Use it
in place of the upstream input and import the normal module:

```nix
inputs.noctalia.url = "github:gluurp/noctalia?ref=local";

# then, as before:
# imports = [ inputs.noctalia.nixosModules.default ];
# or home-manager: imports = [ inputs.noctalia.homeModules.default ];
```

The module API, package name and options are upstream's; the module's
`programs.noctalia.package` already defaults to this flake's package, so the
only difference is the patched source.

If you would rather keep upstream's flake untouched and apply the five changes
as patches, pin it to the tag they target and override the package:

```nix
inputs.noctalia.url = "github:noctalia-dev/noctalia/v5.6.0";

# copy the five patches from gluurp/software/noctalia/patches next to this file
programs.noctalia.package =
  inputs.noctalia.packages.${pkgs.system}.default.overrideAttrs (old: {
    patches = (old.patches or []) ++ [
      ./patches/0001-audio-stream-descriptors.patch
      ./patches/0002-spectrum-visualizer.patch
      ./patches/0003-transparent-sysmon-graph-background.patch
      ./patches/0004-super-reveal.patch
      ./patches/0005-wallpaper-features.patch
    ];
  });
```

Nix reuses the upstream dependencies and rebuilds only Noctalia. The patches
must match the pinned tag, exactly as with `build-patched.sh`. A compiled
Noctalia binary cannot be patched in place.

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
