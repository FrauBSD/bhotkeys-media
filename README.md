[//]: # ($FrauBSD: bhotkeys-media/README.md 2026-10-05 19:44:02 -0700 Devin Teske $)

# bhotkeys-media

Play, rewind, and fast-forward keys for FreeBSD X11.
`mpris-control` talks to the active MPRIS player. These keys are
session-only; the greeter has no player.

Home: [FrauBSD/bhotkeys-media](https://github.com/FrauBSD/bhotkeys-media)

## Requirements

- `bhotkeys`
- `dbus`
- `libkvm` (find a private session bus when the window manager has none)

## Build / install

```sh
make
make install    # PREFIX=/usr/local by default
make clean
```

Builds `mpris-control` and installs it into `${PREFIX}/bin`.
Plugins `fn-play`, `fn-prev`, and `fn-next` go to
`${PREFIX}/share/bhotkeys/plugins.d`.
