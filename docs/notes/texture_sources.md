# Texture sources for Earth, Saturn and Saturn's rings (M2)

Milestone M2 needs equirectangular (2:1) maps for Earth and Saturn, plus a radial profile for Saturn's rings.
Miller's, Mann's and Edmunds' planets are generated procedurally by a script and need no downloaded source.
Nothing listed here is committed to git: a download script fetches the files into the gitignored
`assets/textures/` directory. The machine-readable list at the end is the input for that script.

All URLs were checked on 2026-10-20 with `curl -I` (or a one-byte range request). Sizes are the `Content-Length`
the server reported. Pixel dimensions come from reading the first bytes of the file, or, where marked "(name)",
from the file name only. Nothing was downloaded in full except the five small Solar System Scope files whose
SHA-256 is given.

---

## 1. Summary and recommendation

| Body | Map | Recommended source | Licence |
|---|---|---|---|
| Earth | day albedo | NASA Blue Marble Next Generation (5400 / 21600 wide) or Solar System Scope (2K / 8K) | NASA media guidelines / CC BY 4.0 |
| Earth | night lights | NASA Black Marble 2016 (3600 / 13500 wide) or Solar System Scope | NASA media guidelines / CC BY 4.0 |
| Earth | clouds | Solar System Scope (2K / 8K), or NASA `cloud_combined` (2048 / 8192) | CC BY 4.0 / NASA media guidelines |
| Earth | height | NASA GEBCO 2008 elevation (5400 / 21600) | NASA media guidelines, GEBCO credit |
| Earth | specular (ocean mask) | Solar System Scope only | CC BY 4.0 |
| Earth | normal | Solar System Scope only | CC BY 4.0 |
| Saturn | albedo | Solar System Scope only (no Cassini mosaic verified) | CC BY 4.0 |
| Saturn rings | radial colour / opacity | Solar System Scope ring strip (RGBA) | CC BY 4.0 |

The simplest consistent choice is **Solar System Scope for everything** (one licence, one attribution line, 2K
and 8K for every map), with NASA GEBCO as a true height map and NASA Black Marble / Blue Marble as optional
higher-quality alternatives.

## 2. Solar System Scope

Page: <https://www.solarsystemscope.com/textures/>

Licence text, quoted from that page (the page links <https://creativecommons.org/licenses/by/4.0/>):

> Distributed under Attribution 4.0 International license:
> You may use, adapt, and share these textures for any purpose, even commercially.

So the licence is **CC BY 4.0**, as expected. The textures page states no specific credit wording. CC BY 4.0
requires attribution (creator, a link to the licence, and an indication of changes), so the project should
state the line suggested in §6. The page's text on provenance: "Solar System Scope unique planet maps are based
on NASA elevation and imagery data" and the Earth maps merge "NASA's Blue Marble" with other geo-data
(<https://www.solarsystemscope.com/about>).

Caveats:

- **Download needs a Referer header.** A plain `curl` of `.../textures/download/<file>` returns an HTML page
  (HTTP 200, `text/html`), not the image. With `-e https://www.solarsystemscope.com/textures/` the server
  returns the real file. The fetch script must send that header and check the magic bytes.
- The links are relative on the page (`download/8k_earth_daymap.jpg`), so the full URL is
  `https://www.solarsystemscope.com/textures/download/<file>`.
- The "8k" Saturn albedo is only **4096 x 2048**, not 8192 x 4096 (checked from the file header).
- No download hash or checksum is published by the site.

| File | Pixels | Format | Size (bytes) | Notes |
|---|---|---|---|---|
| `2k_earth_daymap.jpg` | 2048 x 1024 | JPEG | 463 087 | SHA-256 `767ee1dc...b07b7c` (full below) |
| `8k_earth_daymap.jpg` | 8192 x 4096 | JPEG | 4 565 076 | |
| `2k_earth_nightmap.jpg` | 2048 x 1024 (name) | JPEG | 255 287 | |
| `8k_earth_nightmap.jpg` | 8192 x 4096 (name) | JPEG | 3 144 468 | |
| `2k_earth_clouds.jpg` | 2048 x 1024 (name) | JPEG | 965 676 | |
| `8k_earth_clouds.jpg` | 8192 x 4096 (name) | JPEG | 11 619 184 | |
| `2k_earth_specular_map.tif` | 2048 x 1024 (name) | TIFF | 338 524 | ocean mask |
| `8k_earth_specular_map.tif` | 8192 x 4096 (name) | TIFF | 1 843 592 | |
| `2k_earth_normal_map.tif` | 2048 x 1024 (name) | TIFF | 521 376 | |
| `8k_earth_normal_map.tif` | 8192 x 4096 (name) | TIFF | 9 490 024 | |
| `2k_saturn.jpg` | 2048 x 1024 (name) | JPEG | 199 916 | |
| `8k_saturn.jpg` | 4096 x 2048 | JPEG | 1 098 204 | |
| `2k_saturn_ring_alpha.png` | 2048 x 125 | PNG RGBA | 12 119 | radial strip |
| `8k_saturn_ring_alpha.png` | 8192 x 500 | PNG RGBA | 64 790 | radial strip |

There is no 4K Earth/Saturn variant on the page (only 2K "High" and 8K "Ultra"; Venus atmosphere has 4K).

SHA-256 of the files that were fully downloaded:

| File | SHA-256 |
|---|---|
| `2k_earth_daymap.jpg` | `767ee1dc6eb3802699bfccf6f264880f8acd0b80de3191cd24984fe279b07b7c` |
| `8k_earth_daymap.jpg` | `88ab060b6e7d241cfc590c69f528fab2b3247b738d40124cb590999a6fe44abc` |
| `8k_saturn.jpg` | `0d39a4a490c87c3edabe00a3881a29bb3418364178c79c534fe0986e97e09853` |
| `2k_saturn_ring_alpha.png` | `4b0644b2f3ef259fd0bfaedd9dc52f9e7d9738f65f50363e8c4dd19f650d7334` |
| `8k_saturn_ring_alpha.png` | `f1f826933c9ff87d64ecf0518d6256b8ed990b003722794f67e96e3d2b876ae4` |

These pin the file as served today. If the site replaces a file, the hash check fails and the table needs updating.

The ring file is a 1D radial strip (width = radius from inner to outer edge, height only a few hundred rows
of the same profile), with RGB colour and alpha opacity. It is a stylised artist profile, not measured data.

## 3. NASA Earth maps

NASA files are hosted on `eoimages.gsfc.nasa.gov` (the old Visible Earth host; all links below resolve) and on
`assets.science.nasa.gov` (the new host used by the Earth Observatory pages). The Visible Earth record pages
(`visibleearth.nasa.gov/images/<id>`) now redirect to the Earth Observatory landing page, so the per-image
credit lines could not be re-read from them. See "Not verified" in §7.

### 3.1 Licence

Source: NASA Brand Center, images and media (<https://www.nasa.gov/nasa-brand-center/images-and-media/>):

> generally are not subject to copyright in the United States.
> NASA should be acknowledged as the source of the material.
> NASA content used in a factual manner that does not imply endorsement may be used without needing explicit permission.

NASA insignia and logotype are not public domain and must not be used. NASA states that it occasionally hosts
third-party copyrighted material, so each dataset's own credit line matters. None of the files below is marked
as third-party. The GEBCO maps carry a GEBCO / British Oceanographic Data Centre credit (§3.4).

### 3.2 Day albedo (Blue Marble Next Generation, December 2004)

| File | Pixels | Size | Notes |
|---|---|---|---|
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/74000/74218/world.200412.3x5400x2700.jpg` | 5400 x 2700 | 1 878 742 | plain colour, no relief shading |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg` | 5400 x 2700 | 2 566 770 | with topography and bathymetry shading |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x21600x10800.jpg` | 21600 x 10800 | 29 868 040 | |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57752/land_shallow_topo_2048.jpg` | 2048 x 1024 | 238 676 | Blue Marble 2002, shaded topography |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57752/land_shallow_topo_8192.tif` | 8192 x 4096 (name) | 27 797 968 | |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57752/land_shallow_topo_21600.tif` | 21600 x 10800 (name) | 181 981 194 | |

The variants with "topo" bake relief shading into the albedo. That is wrong if the renderer applies its own
lighting and normal map, so prefer `world.200412.3x5400x2700.jpg` (no shading). Only the 5400 wide plain
version was found; the plain 21600 file name was not located (the `world.200412.3x21600x10800.jpg` name was not
tested, so do not assume it).

### 3.3 Night lights (Black Marble 2016, Suomi NPP VIIRS)

| File | Pixels | Size |
|---|---|---|
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/144000/144898/BlackMarble_2016_01deg.jpg` | 3600 x 1800 | 779 638 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/144000/144898/BlackMarble_2016_3km.jpg` | 13500 x 6750 | 8 106 233 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/79000/79765/dnb_land_ocean_ice.2012.3600x1800.jpg` | 3600 x 1800 (name) | 794 479 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/79000/79765/dnb_land_ocean_ice.2012.13500x6750.jpg` | 13500 x 6750 (name) | 7 813 537 |

Credit given by NASA SVS for the 2016 composite (<https://svs.gsfc.nasa.gov/30876>): "Please give credit for
this item to: NASA's Goddard Space Flight Center". Credit lines found in a search (not read on the NASA page
itself): NASA Earth Observatory images by Joshua Stevens, using Suomi NPP VIIRS data from Miguel Román, NASA
GSFC. The 2012 version is the older Black Marble composite. The black-and-white-on-land 2016 version has no
coastline overlay and is the better choice. The 2012 file includes land/ocean/ice colour underneath.

### 3.4 Height (GEBCO 2008, NASA Blue Marble Next Generation relief)

Page: <https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/topography-bathymetry-maps>

Credit line from that page, verbatim: "Imagery by Jesse Allen, NASA's Earth Observatory, using data from the
General Bathymetric Chart of the Oceans (GEBCO)". The page contains no separate copyright statement.

Topography is scaled 0 to 6400 m (land only, sea level is black); bathymetry is scaled -8000 m to 0 m.

| File | Pixels | Size |
|---|---|---|
| `https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/topography/gebco_08_rev_elev_5400x2700.jpg` | 5400 x 2700 | 1 244 508 |
| `https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/topography/gebco_08_rev_elev_21600x10800.jpg` | 21600 x 10800 | 14 107 764 |
| `https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/topography/gebco_08_rev_elev_21600x10800.tif` | 21600 x 10800 | 233 345 166 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73934/gebco_08_rev_elev_21600x10800.png` | 21600 x 10800, 8-bit grey | 18 414 843 |
| `https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bathymetry/gebco_08_rev_bath_5400x2700.jpg` | 5400 x 2700 (name) | 2 940 609 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73963/gebco_08_rev_bath_21600x10800.png` | 21600 x 10800, 8-bit palette | 35 097 740 |

8-bit height is coarse (6400 m / 255 is about 25 m per level), which is fine for a visual bump or normal map
but not for metrology. Downscale the 21600 PNG to 8192 x 4096 for the 8K variant, because NASA offers no 4K
or 8K size.

### 3.5 Clouds

| File | Pixels | Size |
|---|---|---|
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_2048.jpg` | 2048 x 1024 (name) | 829 367 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_2048.tif` | 2048 x 1024 (name) | 2 687 040 |
| `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_8192.tif` | 8192 x 4096 (name) | 35 870 468 |

The file names are confirmed to resolve. What the record contains (a cloud coverage composite, which channel)
was not confirmed, see §7. Solar System Scope's cloud map is the verified alternative.

## 4. Saturn

- **Albedo.** Only the Solar System Scope map was verified (4096 x 2048 for "8k"). No equirectangular Cassini
  mosaic of Saturn from NASA/JPL was found at a stable direct URL, so none is listed. Note that Saturn has no
  solid surface; the "map" is a cloud-top colour/banding texture, and its zonal bands could also be generated.
- **Ring profile, stylised.** `8k_saturn_ring_alpha.png` (8192 x 500, RGBA), see §2. Use it as a polar
  lookup: u = (r - r_inner) / (r_outer - r_inner) along x.
- **Ring profile, measured (optional, not in the fetch list).** The PDS Rings node distributes Cassini
  occultation radial profiles. Two candidates, both public NASA data:
  - Cassini RSS ring profiles, dataset CORSS_8001 v2
    (<https://pds-rings.seti.org/cassini/rss/>). The tarball
    `https://pds-rings.seti.org/holdings/archives-volumes/CORSS_8xxx/CORSS_8001.tar.gz` exists but is about
    **6.1 GB** (`Content-Range: bytes 0-0/6142435253`), too big for a texture fetch. Individual `TAU` files
    exist inside it and can be browsed on the same site.
  - Cassini UVIS solar occultation archive (Jarmak et al. 2022), readme at
    <https://pds-rings.seti.org/pds4/bundles/cassini_uvis_solarocc_beckerjarmak2023/cassini_uvis_solarocc_beckerjarmak2023_v1.0/readme.txt>.
    ASCII `.tab` files with normal optical depth versus radius. Individual file URLs were not checked.
  The data.nasa.gov catalogue entry for the RSS data was seen listed as "us-pd" in a search result, but the
  page itself did not return a licence when fetched, so that is not verified.

## 5. Not recommended / does not work

- `https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57735/land_ocean_ice_8192.png` returns 404.
- `https://assets.science.nasa.gov/.../74218/world.200412.3x5400x2700.jpg` (guessed host path) returns 404;
  use the `eoimages.gsfc.nasa.gov` link for that image.
- `https://visibleearth.nasa.gov/images/<id>/...` pages redirect to the Earth Observatory landing page.
- Solar System Scope download URLs without a Referer return an HTML page that looks like success (HTTP 200).

## 6. Attribution text

For the credits screen / README of the repository (adapt wording, keep the CC BY elements):

- Solar System Scope textures: "Planet textures by Solar System Scope (https://www.solarsystemscope.com/textures/),
  based on NASA elevation and imagery data, licensed under CC BY 4.0
  (https://creativecommons.org/licenses/by/4.0/). Resampled / converted by MoleHole." The wording is the
  project's own; the site specifies none.
- NASA Blue Marble: "NASA Earth Observatory / Blue Marble Next Generation, Reto Stöckli, NASA Goddard Space
  Flight Center." The creator names are from the search results for the Blue Marble series, not read from the
  record page of the exact file, so check before shipping.
- NASA Black Marble 2016: "NASA's Goddard Space Flight Center; Suomi NPP VIIRS data, Miguel Román (NASA GSFC);
  image processing Joshua Stevens."
- GEBCO relief: "Imagery by Jesse Allen, NASA's Earth Observatory, using data from the General Bathymetric Chart
  of the Oceans (GEBCO)."
- Do not use the NASA logo or imply NASA endorsement.

## 7. Not verified

- Per-image credit and description on the Visible Earth record pages (they redirect). Credit lines in §6 for
  Blue Marble and Black Marble come from search results and the SVS page, not from the file records.
- Whether `cloud_combined_*` contains an RGB cloud image or a greyscale alpha, and its date.
- Pixel dimensions marked "(name)", taken from the file name only.
- Whether the Solar System Scope textures are also licensed identically on older mirror pages, and whether any
  individual texture has a separate source credit (the site gives none).
- A direct-download Cassini Saturn global map; individual PDS UVIS `.tab` file URLs; data.nasa.gov licence of
  CORSS_8001.
- SHA-256 of every file except the five listed in §2.
- Long-term stability: NASA reorganised its hosts in 2025/2026 (Visible Earth ids now redirect). The
  `eoimages.gsfc.nasa.gov` links still resolve but may move; the Solar System Scope links could change without notice.

## Sources

- Solar System Scope, Textures. <https://www.solarsystemscope.com/textures/> (licence text, file list), and About,
  <https://www.solarsystemscope.com/about>.
- Creative Commons, Attribution 4.0 International. <https://creativecommons.org/licenses/by/4.0/>.
- NASA Brand Center, Images and media usage guidelines. <https://www.nasa.gov/nasa-brand-center/images-and-media/>.
- NASA Earth Observatory, Blue Marble: Next Generation Topography and Bathymetry Maps.
  <https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/topography-bathymetry-maps>.
- NASA Scientific Visualization Studio, Black Marble 2016. <https://svs.gsfc.nasa.gov/30876>.
- NASA Visible Earth, Blue Marble series (via search result, pages now redirect).
  <https://visibleearth.nasa.gov/images/57752/blue-marble-land-surface-shallow-water-and-shaded-topography>.
- PDS Rings Node, Cassini RSS data users guide. <https://pds-rings.seti.org/cassini/rss/>; Spacecraft-based ring
  occultations. <https://pds-rings.seti.org/ringocc/>.
- Jarmak, S. et al. (2022). Solar Occultation Observations of Saturn's Rings with Cassini UVIS. Icarus 388,
  115237. DOI 10.1016/j.icarus.2022.115237 (as cited in the PDS readme).

---

## Machine-readable list

`target` is relative to `assets/textures/`. Solar System Scope URLs require the HTTP header
`Referer: https://www.solarsystemscope.com/textures/`. `sha256` is present only where it was computed.

```yaml
requires_referer:
  "https://www.solarsystemscope.com/textures/download/": "https://www.solarsystemscope.com/textures/"
textures:
  # Solar System Scope, CC BY 4.0
  - id: earth_albedo_2k
    url: https://www.solarsystemscope.com/textures/download/2k_earth_daymap.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/albedo_2k.jpg
    sha256: 767ee1dc6eb3802699bfccf6f264880f8acd0b80de3191cd24984fe279b07b7c
  - id: earth_albedo_8k
    url: https://www.solarsystemscope.com/textures/download/8k_earth_daymap.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/albedo_8k.jpg
    sha256: 88ab060b6e7d241cfc590c69f528fab2b3247b738d40124cb590999a6fe44abc
  - id: earth_night_2k
    url: https://www.solarsystemscope.com/textures/download/2k_earth_nightmap.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/night_2k.jpg
  - id: earth_night_8k
    url: https://www.solarsystemscope.com/textures/download/8k_earth_nightmap.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/night_8k.jpg
  - id: earth_clouds_2k
    url: https://www.solarsystemscope.com/textures/download/2k_earth_clouds.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/clouds_2k.jpg
  - id: earth_clouds_8k
    url: https://www.solarsystemscope.com/textures/download/8k_earth_clouds.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/clouds_8k.jpg
  - id: earth_specular_2k
    url: https://www.solarsystemscope.com/textures/download/2k_earth_specular_map.tif
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/specular_2k.tif
  - id: earth_specular_8k
    url: https://www.solarsystemscope.com/textures/download/8k_earth_specular_map.tif
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/specular_8k.tif
  - id: earth_normal_2k
    url: https://www.solarsystemscope.com/textures/download/2k_earth_normal_map.tif
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/normal_2k.tif
  - id: earth_normal_8k
    url: https://www.solarsystemscope.com/textures/download/8k_earth_normal_map.tif
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: earth/normal_8k.tif
  - id: saturn_albedo_2k
    url: https://www.solarsystemscope.com/textures/download/2k_saturn.jpg
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: saturn/albedo_2k.jpg
  - id: saturn_albedo_4k
    url: https://www.solarsystemscope.com/textures/download/8k_saturn.jpg   # served file is 4096x2048
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: saturn/albedo_4k.jpg
    sha256: 0d39a4a490c87c3edabe00a3881a29bb3418364178c79c534fe0986e97e09853
  - id: saturn_rings_2k
    url: https://www.solarsystemscope.com/textures/download/2k_saturn_ring_alpha.png
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: saturn/rings_2k.png
    sha256: 4b0644b2f3ef259fd0bfaedd9dc52f9e7d9738f65f50363e8c4dd19f650d7334
  - id: saturn_rings_8k
    url: https://www.solarsystemscope.com/textures/download/8k_saturn_ring_alpha.png
    licence: CC-BY-4.0
    attribution: "Solar System Scope (solarsystemscope.com/textures), CC BY 4.0"
    target: saturn/rings_8k.png
    sha256: f1f826933c9ff87d64ecf0518d6256b8ed990b003722794f67e96e3d2b876ae4

  # NASA (optional higher-quality / true height data)
  - id: earth_albedo_nasa_5k
    url: https://eoimages.gsfc.nasa.gov/images/imagerecords/74000/74218/world.200412.3x5400x2700.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "NASA Earth Observatory, Blue Marble Next Generation (Reto Stockli, NASA GSFC)"
    target: earth/nasa/albedo_5k.jpg
  - id: earth_albedo_nasa_21k
    url: https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x21600x10800.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "NASA Earth Observatory, Blue Marble Next Generation with topography and bathymetry (relief shading baked in)"
    target: earth/nasa/albedo_topo_21k.jpg
  - id: earth_night_nasa_3600
    url: https://eoimages.gsfc.nasa.gov/images/imagerecords/144000/144898/BlackMarble_2016_01deg.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "NASA's Goddard Space Flight Center; Suomi NPP VIIRS data, Miguel Roman (NASA GSFC); processing Joshua Stevens"
    target: earth/nasa/night_3600.jpg
  - id: earth_night_nasa_13500
    url: https://eoimages.gsfc.nasa.gov/images/imagerecords/144000/144898/BlackMarble_2016_3km.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "NASA's Goddard Space Flight Center; Suomi NPP VIIRS data, Miguel Roman (NASA GSFC); processing Joshua Stevens"
    target: earth/nasa/night_13500.jpg
  - id: earth_height_5k
    url: https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/topography/gebco_08_rev_elev_5400x2700.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "Imagery by Jesse Allen, NASA's Earth Observatory, using data from the General Bathymetric Chart of the Oceans (GEBCO)"
    target: earth/height_5k.jpg
  - id: earth_height_21k
    url: https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/topography/gebco_08_rev_elev_21600x10800.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "Imagery by Jesse Allen, NASA's Earth Observatory, using data from the General Bathymetric Chart of the Oceans (GEBCO)"
    target: earth/height_21k.jpg
  - id: earth_clouds_nasa_2k
    url: https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_2048.jpg
    licence: "NASA media usage guidelines (generally not subject to copyright in the US; acknowledge NASA)"
    attribution: "NASA Earth Observatory / Visible Earth, cloud_combined (contents not verified)"
    target: earth/nasa/clouds_2k.jpg
```

---

## 8. Moons and dwarf planets (manifest group `moons`)

Added 2026-10-30. Real spacecraft mosaics, all equirectangular 2:1 (checked from the TIFF headers and by looking at
every result). Source host: USGS Astrogeology (`planetarymaps.usgs.gov/mosaic/*.tif`, product pages under
`astrogeology.usgs.gov/search/map/<slug>`), except Mimas (NASA Photojournal PIA17214). Licence per USGS record:
public domain or no access constraints, "please cite authors"; the underlying data are NASA PDS products.
The TIFFs are 8 to 360 MB and stb_image cannot read them, so `tools/fetch_textures.py` downloads the TIFF, checks
`source_sha256`, and writes a JPEG q90 of at most `max_width` pixels (`sha256` is the hash of that final file and
only a warning if another Pillow/libjpeg build encodes differently).

| Body | Target (`assets/textures/`) | Source product | Final size | Caveats |
|---|---|---|---|---|
| Io | `io/albedo_8k.jpg` | Galileo SSI + Voyager colour-merged global mosaic 1 km (`io_galileo_ssi_voyager_color_merged_global_mosaic_1km`) | 8192x4096 RGB | colour merge, real; smooth at the poles |
| Europa | `europa/albedo_8k.jpg` | Voyager + Galileo SSI global mosaic 500 m (`europa_voyager_galileo_ssi_global_mosaic_500m`) | 8192x4096 grey | grey-scale; visible resolution seams |
| Ganymede | `ganymede/albedo_8k.jpg` | Voyager + Galileo SSI global mosaic 1 km (`ganymede_voyager_galileo_ssi_global_mosaic_1km`) | 8192x4096 grey | grey-scale; seams, dark blank corners at the poles |
| Callisto | `callisto/albedo_8k.jpg` | Galileo SSI + Voyager global mosaic 1 km (`callisto_galileo_voyager_global_mosaic_1km`) | 8192x4096 grey | grey-scale; tile seams, black polar gap |
| Titan | `titan/albedo_4k.jpg` | Cassini ISS global mosaic 4 km (`titan_cassini_iss_global_mosaic_4005m`) | 4040x2020 grey | near-IR through the haze (not visible colour); patchwork of tiles; 450 m near-global mosaic exists but is 1.5 GB |
| Enceladus | `enceladus/albedo_8k.jpg` | Cassini ISS global mosaic 110 m (`enceladus_cassini_global_mosaic_110m`) | 8192x4096 grey | grey-scale; polar smearing |
| Mimas | `mimas/albedo_6k.jpg` | NASA Photojournal PIA17214, Cassini global map June 2017 (216 m/px) | 5760x2880 grey | file used unchanged; polar smearing; NASA media licence (not USGS) |
| Tethys | `tethys/albedo_8k.jpg` | Cassini ISS global mosaic 293 m (`tethys_cassini_global_mosaic_293m`) | 8192x4096 grey | grey-scale |
| Dione | `dione/albedo_8k.jpg` | Cassini + Voyager global mosaic 154 m (`dione_cassini_voyager_global_mosaic_154m`) | 8192x4096 grey | source 23040x11520 (265 MB); grey-scale |
| Rhea | `rhea/albedo_8k.jpg` | Cassini + Voyager global mosaic 417 m (file `Rhea_Cassini_Voyager_mosaic_global_417m.tif`; the USGS page `rhea_cassini_voyager_global_mosaic_417m` links the older 833 m Voyager file) | 8192x4096 grey | grey-scale |
| Iapetus | `iapetus/albedo_6k.jpg` | Cassini + Voyager global mosaic (`iapetus_cassini_voyager_global_mosaic_803m`, file name says 783 m) | 5760x2880 grey | grey-scale; re-encoded only |
| Triton | `triton/albedo_8k.jpg` | Voyager 2 global colour mosaic 600 m, global fill (`triton_voyager_2_global_color_mosaic_600m`) | 8192x4096 RGB | the north (roughly 30 to 40 % of the map) was never imaged: black; the rest is very blurry in places |
| Pluto | `pluto/albedo_8k.jpg` | New Horizons LORRI+MVIC global mosaic 300 m, Jul 2017 (`pluto_new_horizons_lorri_mvic_global_mosaic_300m`) | 8192x4096 grey | source 24888x12444 (310 MB); southern polar night is black (about a third of the map); Sputnik Planitia is visible |
| Charon | `charon/albedo_8k.jpg` | New Horizons LORRI+MVIC global mosaic 300 m, Jul 2017 (`charon_new_horizons_lorri_mvic_global_mosaic_300m`) | 8192x4096 grey | southern third of the map is black (never lit/imaged); the far side is low resolution |
| Ceres | `ceres/albedo_7k.jpg` | Dawn FC global mosaic 400 m (`ceres_dawn_fc_global_mosaic_400m`) | 7383x3691 grey | not 8192 wide in the source; thin black strip at the south pole |
| Vesta | `vesta/albedo_8k.jpg` | Dawn FC HAMO global mosaic (`vesta_dawn_fc_hamo_global_mosaic_60m`) | 8192x4096 grey | source 26704x13352 (357 MB); north polar night is black |
| Phobos | `phobos/albedo_6k.jpg` | Mars Express SRC + Viking global mosaic 16 ppd (`phobos_mars_express_src_global_mosaic_12m`) | 5760x2880 grey | ESA/DLR/FU Berlin data, labelled public domain by USGS only; black patches where there is no coverage; re-encoded only |
| Venus (surface) | `venus/albedo_6k.jpg` | Magellan C3-MDIR synthetic colour mosaic 4641 m (`venus_magellan_global_c3_mdir_synthetic_color_mosaic_4641m`) | 6144x3072 RGB | radar backscatter with Venera-derived synthetic colour, not optical; resized from 8192 to stay near 10 MB |

Not available as a real map:

- Eris, Haumea, Makemake: never imaged beyond a few pixels. The `4k_*_fictional.jpg` files are artists' inventions.
- Deimos: only the old USGS/Stooke cylindrical maps on solarviews.com, whose site-wide copyright notice contradicts the
  per-file "public domain" label, so not used. No Deimos mosaic on astrogeology.usgs.gov.
- Amalthea, Hyperion: no global mosaic exists (only nomenclature reference maps). Phoebe: the Cassini map (233 m,
  IMG/PDF/PNG only) covers only part of the surface, not fetched.
- Mimas has no USGS GeoTIFF, hence the NASA Photojournal file. The old `photojournal.jpl.nasa.gov/jpeg/PIA*.jpg`
  URLs now redirect to an HTML page; the working link is under `assets.science.nasa.gov/content/dam/science/psd/photojournal/pia/`.
