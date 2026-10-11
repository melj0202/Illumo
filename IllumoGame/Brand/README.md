# CSim logo — asset pack

Direction **M1 · Menu glider**: a glider on a 5×5 cell grid, plus "CELLULAR PLAYGROUND" / "CSIM" and a cyan→violet bar.

## Files

| Folder | What | Use |
|---|---|---|
| `svg/csim-mark.svg` | 5×5 grid + glider, flat | Dark backgrounds; safe for simple SVG loaders (no filters) |
| `svg/csim-mark-glow.svg` | Same with cyan glow (SVG filter) | Dark backgrounds, browsers/design tools |
| `svg/csim-mark-light-bg.svg` | Navy glider, pale grid | Light backgrounds |
| `svg/csim-glider-{cyan,navy}.svg` | Glider only, no grid | Small sizes, inline UI, favicons |
| `svg/csim-lockup*.svg` | Full lockup: flat / glow / on navy / light-bg | Title screen, README header, store art |
| `svg/csim-wordmark-{white,navy}.svg` | "CSIM" only | Tight horizontal spaces |
| `svg/csim-app-icon.svg` | 1024 master app icon | Source for icon exports |
| `png/app-icon/` | 16, 24, 32, 48 (pixel-snapped, drawn per size) and 64–1024 (from master) | Window/taskbar icon, launchers |
| `png/mark/`, `png/lockup/` | Rasters at several sizes, transparent unless named `on-navy` | Engine textures, docs |
| `ico/csim.ico` | Windows icon: 16, 24, 32, 48, 64, 128, 256 | `.exe` resource / window icon |

All text is converted to outlines, so no font needs to be installed.

## Colors

| Token | Hex | Role |
|---|---|---|
| Navy | `#0b1624` | Background, icon tile |
| Dim cell | `#1a2940` | Dead grid cells |
| Border | `#274058` | Icon tile edge |
| Cyan | `#45d6e6` | Live cells, kicker text |
| Violet | `#7a6cf2` | End of the accent bar |
| White | `#eef4f8` | Wordmark |
| Light-bg teal / violet | `#0a6e7a` / `#5a4bd6` | Kicker and bar on light backgrounds (contrast-safe on white) |

The colours were matched by eye from a screenshot of the main menu. Replace them with the exact values from the game if they differ.

## Type

**Chakra Petch Bold** (SIL Open Font License 1.1, see `FONT-LICENSE-OFL.txt`) stands in for the menu font. "CSIM" has −0.01em tracking and the kicker +0.32em. In the lockup, CSIM is sized so the column's top (kicker cap height) and bottom (the bar) line up exactly with the 132 px mark.

## Usage

- Clear space: at least one grid cell pitch (the gap between two cells) on every side of the mark; at least the mark's width around the lockup.
- Minimum sizes: grid mark 48 px; below that, use the glider only. Lockup: 240 px wide.
- Don't recolour the glider, rotate it (the orientation is the direction of travel), or put the cyan version on light backgrounds.
