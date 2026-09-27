# Illumo brand kit

Final logo: **R09 · Amber neon**. It's the Comfortaa Bold wordmark with the bulb as the "o", tilted 30° left, with five burst rays.
All SVGs have the text converted to outlines, so they don't need the font installed.

## What's where

| Folder | Files | Use it for |
|---|---|---|
| `logo/` | `illumo-logo-neon` · `-light` · `-flat-dark` · `-mono-white` · `-mono-black` (SVG, PNG 1200 px, `@2x` 2400 px) | The full wordmark. **neon** goes on dark backgrounds (glow built in). **light** goes on light backgrounds. **flat-dark** is for dark backgrounds where glow won't work (print, small sizes). **mono** versions are for single-colour use (stamps, embossing, merch). |
| `mark/` | `illumo-mark-*` (SVG, PNG 512 / 1024) | The bulb on its own, with the same five variants. |
| `icon/` | `illumo-icon.svg`, `png/illumo-icon-{16…1024}.png` | App or window icon: a rounded dark tile with the glowing bulb. |
| `icon/` | `illumo-icon-square.svg`, `-512/-1024.png` | The same icon with square corners, for avatars and platforms that round the corners themselves (GitHub org avatar, Discord, etc.). |
| `favicon/` | `favicon.ico` (16/32/48), `favicon.svg`, `favicon-16x16.png`, `favicon-32x32.png`, `apple-touch-icon.png`, `android-chrome-192/512.png` | Website favicon set. Uses the flat version with a larger bulb, because glow blurs at tab size. |
| `github/` | `social-preview.png` (1280×640) + SVG | The repository's social preview image. |
| `github/` | `readme-banner-dark.png` / `-light.png` (1280×400) + SVGs | README header. |

This folder holds the rendered kit. The generator (`build.py`, the Comfortaa
font and its `OFL.txt`) stays with the master copy of the kit outside the
repository. Regenerate there, then copy the output folders over these.

## Where the repository uses it

**README header:** the root `README.md` shows `github/readme-banner-dark.png`
or `-light.png` through a `<picture>` element, switching with GitHub's theme.

**Runtime icon:** `IllumoRuntime.exe` embeds `Illumo/Source/Wasm/IllumoRuntime.ico`
as the `GLFW_ICON` resource (`Illumo/Source/Wasm/IllumoRuntime.rc`). GLFW gives
its window class that icon, so it covers Explorer, the title bar, the taskbar
and Alt+Tab for every app window, detached panels included. The `.ico` holds
`favicon/favicon-16x16.png` (the flat art, which stays sharp at 16 px) and
`icon/png/illumo-icon-{32,48,64,128,256}.png`. After changing those PNGs, run
`python tools/make_brand_assets.py` from the repository root.

**Engine splash and badge:** `tools/make_brand_assets.py` also writes
`Illumo/Assets/Branding/illumo-splash.png` (the neon logo) and
`illumo-badge.png` (the flat wordmark), cropped and pre-sized because guest
textures have no mipmaps. `IllumoRuntime` plays the splash before every app
(`GuiEngineSplash`); CSim's title screen draws the "Powered by" badge in its
lower left corner (`GuiEngineBadge`). See D-UI14.
**Social preview (manual):** go to the repository's **Settings → General**,
find **Social preview**, click **Edit**, and upload `github/social-preview.png`.

## Colours

| Role | Neon (dark bg) | Light bg |
|---|---|---|
| Letters | `#FFF1D6` | `#2B2014` |
| Bulb glass | `#FFFFFF` | `#E08600` |
| Bulb base | `#FFE7B0` | `#2B2014` |
| Rays | `#FFC24A` | `#E08600` |
| Glow | `#FFD08A` → `#FFA53A` → `#FF9628` (50%) | `#FFA53A` (45%), bulb only |
| Background | radial `#1C1308` → `#070503` | `#FAF5EA` |

## Rules of thumb

- Leave clear space around the logo at least as wide as the bulb.
- Neon version: dark backgrounds only. On anything light, use `-light` or `-mono-black`.
- Below about 120 px wide, use the flat or mono wordmark; the glow turns to haze at that size.
- Don't recolour the letters and bulb separately outside the palettes above, and don't straighten the tilt or remove the rays.

## Font

Comfortaa Bold by The Comfortaa Project Authors, under the SIL Open Font License 1.1 (its `OFL.txt` travels with the generator). The logo files contain outlines, not the font. The font is only needed to run `build.py` or to set matching text elsewhere.
