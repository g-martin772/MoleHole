"""Physical data of the bodies in templates/solar-system.yaml (read by tools/fetch_ephemeris.py and gen_solar_system.py).

Sources (full list in docs/notes/solar_system.md):
  gm      JPL planetary ephemeris DE440/DE441 (Park et al. 2021) and the JPL satellite ephemeris summaries; km^3/s^2.
          For planets with moons the planet's own GM, not the system's: the moons are bodies of their own here.
  radius  IAU/NASA fact sheets, km: equatorial for the planets (the sphere is not flattened, J2 carries the shape in
          the dynamics), mean for everything else.
  pole, w0, wdot   IAU WGCCRE 2015 (Archinal et al. 2018): north pole (RA, Dec of ICRF) and prime meridian W = w0 + wdot d
          with d days from J2000 TDB (deg, deg/day); the periodic nutation/libration terms are left out. A negative wdot
          is a retrograde rotation about the IAU north pole. wdot None = rotation unknown or chaotic.
  j2, j2r  Second zonal harmonic and its reference radius (km), from the planetary gravity-field solutions.
  tint    Linear albedo colour of a body without a map (geometric albedo and colour from the NASA fact sheets and papers).
  tex     Maps under assets/textures/ (tools/texture_manifest.yaml); a body without them shows `tint`.
"""

AU_KM = 149597870.7

# Textures present in the manifest
EARTH = dict(albedo="earth/albedo_8k.jpg", normal="earth/normal_8k.png", roughness="earth/specular_8k.png",
             emissive="earth/night_8k.jpg", clouds="earth/clouds_8k.jpg", height="earth/height_5k.jpg")


def body(id, name, horizons, kind, parent, gm, radius, pole=None, w0=0.0, wdot=None, tint=(0.5, 0.5, 0.5),
         tex=None, j2=0.0, j2r=None, atm=None, color=None, note=""):
    return dict(id=id, name=name, horizons=horizons, kind=kind, parent=parent, gm=gm, radius=radius, pole=pole, w0=w0,
                wdot=wdot, tint=tint, tex=tex or {}, j2=j2, j2r=j2r or radius, atm=atm, color=color, note=note)


BODIES = [
    # ---- the Sun ----------------------------------------------------------------------------------------------
    body("sun", "Sun", "10", "star", None, 132712440041.279419, 695700.0, pole=(286.13, 63.87), w0=84.176, wdot=14.1844,
         j2=2.2e-7, note="Teff 5772 K (IAU 2015 B3), sidereal rotation at the equator 25.05 d"),
    # ---- planets and their moons --------------------------------------------------------------------------------
    body("mercury", "Mercury", "199", "planet", "sun", 22031.868551, 2439.4, pole=(281.0103, 61.4155), w0=329.5988,
         wdot=6.1385108, tint=(0.30, 0.28, 0.26), tex=dict(albedo="mercury/albedo_8k.jpg")),
    body("venus", "Venus", "299", "planet", "sun", 324858.592, 6051.8, pole=(272.76, 67.16), w0=160.20, wdot=-1.4813688,
         tint=(0.8, 0.7, 0.5), tex=dict(albedo="venus/atmosphere_4k.jpg"), j2=4.458e-6, atm="venus"),
    body("earth", "Earth", "399", "planet", "sun", 398600.435507, 6378.137, pole=(0.0, 90.0), w0=190.147,
         wdot=360.9856235, tint=(1, 1, 1), tex=EARTH, j2=1.08262668e-3, j2r=6378.137, atm="earth"),
    body("moon", "Moon", "301", "moon", "earth", 4902.800118, 1737.4, pole=(269.9949, 66.5392), w0=38.3213,
         wdot=13.17635815, tint=(0.14, 0.14, 0.14), tex=dict(albedo="moon/albedo_8k.jpg"), j2=2.0321568e-4, j2r=1738.0),
    body("mars", "Mars", "499", "planet", "sun", 42828.3752, 3396.19, pole=(317.68143, 52.8865), w0=176.630,
         wdot=350.89198226, tint=(0.5, 0.3, 0.2), tex=dict(albedo="mars/albedo_8k.jpg"), j2=1.96045e-3, j2r=3396.0,
         atm="mars"),
    body("phobos", "Phobos", "401", "moon", "mars", 7.087e-4, 11.1, pole=(317.671, 52.886), w0=35.1877,
         wdot=1128.844585, tint=(0.07, 0.065, 0.06)),
    body("deimos", "Deimos", "402", "moon", "mars", 9.62e-5, 6.2, pole=(316.65, 53.52), w0=79.41, wdot=285.161897,
         tint=(0.10, 0.09, 0.08)),
    body("jupiter", "Jupiter", "599", "planet", "sun", 126686531.9, 71492.0, pole=(268.056595, 64.495303), w0=284.95,
         wdot=870.536, tint=(0.6, 0.5, 0.4), tex=dict(albedo="jupiter/albedo_4k.jpg"), j2=1.4736e-2, j2r=71492.0,
         atm="jupiter"),
    body("io", "Io", "501", "moon", "jupiter", 5959.916, 1821.49, pole=(268.05, 64.50), w0=200.39, wdot=203.4889538,
         tint=(0.60, 0.50, 0.20)),
    body("europa", "Europa", "502", "moon", "jupiter", 3202.739, 1560.8, pole=(268.08, 64.51), w0=36.022,
         wdot=101.3747235, tint=(0.60, 0.55, 0.50)),
    body("ganymede", "Ganymede", "503", "moon", "jupiter", 9887.834, 2631.2, pole=(268.20, 64.57), w0=44.064,
         wdot=50.3176081, tint=(0.30, 0.28, 0.25)),
    body("callisto", "Callisto", "504", "moon", "jupiter", 7179.289, 2410.3, pole=(268.72, 64.83), w0=259.51,
         wdot=21.5710715, tint=(0.12, 0.11, 0.10)),
    body("amalthea", "Amalthea", "505", "moon", "jupiter", 0.138, 83.5, pole=(268.05, 64.49), w0=231.67,
         wdot=722.631456, tint=(0.10, 0.045, 0.035)),
    body("saturn", "Saturn", "699", "planet", "sun", 37931206.234, 60268.0, pole=(40.589, 83.537), w0=38.90,
         wdot=810.7939024, tint=(0.7, 0.6, 0.45), tex=dict(albedo="saturn/albedo_4k.jpg", rings="saturn/rings_8k.png"),
         j2=1.6298e-2, j2r=60330.0, atm="saturn"),
    body("mimas", "Mimas", "601", "moon", "saturn", 2.5026, 198.2, pole=(40.66, 83.52), w0=333.46, wdot=381.994555,
         tint=(0.50, 0.50, 0.50)),
    body("enceladus", "Enceladus", "602", "moon", "saturn", 7.2111, 252.1, pole=(40.66, 83.52), w0=6.32,
         wdot=262.7318996, tint=(0.85, 0.87, 0.90)),
    body("tethys", "Tethys", "603", "moon", "saturn", 41.2097, 531.1, pole=(40.66, 83.52), w0=8.95, wdot=190.6979085,
         tint=(0.65, 0.65, 0.65)),
    body("dione", "Dione", "604", "moon", "saturn", 73.1146, 561.4, pole=(40.66, 83.52), w0=357.6, wdot=131.5349316,
         tint=(0.50, 0.50, 0.50)),
    body("rhea", "Rhea", "605", "moon", "saturn", 153.9426, 763.8, pole=(40.38, 83.55), w0=235.16, wdot=79.6900478,
         tint=(0.45, 0.45, 0.45)),
    body("titan", "Titan", "606", "moon", "saturn", 8978.1382, 2574.73, pole=(39.4827, 83.4279), w0=189.64,
         wdot=22.5769768, tint=(0.45, 0.30, 0.10), atm="titan"),
    body("hyperion", "Hyperion", "607", "moon", "saturn", 0.3727, 135.0, tint=(0.20, 0.18, 0.17),
         note="chaotic rotation, not turned"),
    body("iapetus", "Iapetus", "608", "moon", "saturn", 120.5038, 734.5, pole=(318.16, 75.03), w0=355.2,
         wdot=4.5379572, tint=(0.25, 0.22, 0.20)),
    body("uranus", "Uranus", "799", "planet", "sun", 5793951.256, 25559.0, pole=(257.311, -15.175), w0=203.81,
         wdot=-501.1600928, tint=(0.55, 0.75, 0.80), tex=dict(albedo="uranus/albedo_2k.jpg"), j2=3.34343e-3, j2r=25559.0,
         atm="uranus"),
    body("miranda", "Miranda", "705", "moon", "uranus", 4.4, 235.8, pole=(257.43, -15.08), w0=30.7, wdot=-254.6906892,
         tint=(0.30, 0.30, 0.30)),
    body("ariel", "Ariel", "701", "moon", "uranus", 86.4, 578.9, pole=(257.43, -15.10), w0=156.22, wdot=-142.8356681,
         tint=(0.35, 0.35, 0.35)),
    body("umbriel", "Umbriel", "702", "moon", "uranus", 81.5, 584.7, pole=(257.43, -15.10), w0=108.05,
         wdot=-86.8688923, tint=(0.12, 0.12, 0.12)),
    body("titania", "Titania", "703", "moon", "uranus", 228.2, 788.9, pole=(257.43, -15.10), w0=77.74,
         wdot=-41.3514316, tint=(0.25, 0.24, 0.23)),
    body("oberon", "Oberon", "704", "moon", "uranus", 192.4, 761.4, pole=(257.43, -15.10), w0=6.77, wdot=-26.7394932,
         tint=(0.20, 0.18, 0.17)),
    body("neptune", "Neptune", "899", "planet", "sun", 6835099.5, 24764.0, pole=(299.36, 43.46), w0=253.18,
         wdot=536.3128492, tint=(0.25, 0.40, 0.80), tex=dict(albedo="neptune/albedo_2k.jpg"), j2=3.411e-3, j2r=24764.0,
         atm="neptune"),
    body("triton", "Triton", "801", "moon", "neptune", 1427.6, 1353.4, pole=(299.36, 41.17), w0=296.53,
         wdot=-61.2572637, tint=(0.70, 0.65, 0.60)),
    body("nereid", "Nereid", "802", "moon", "neptune", 2.06, 170.0, tint=(0.15, 0.15, 0.15), note="rotation unknown"),
    # ---- dwarf planets ------------------------------------------------------------------------------------------
    body("pluto", "Pluto", "999", "dwarf", "sun", 869.6, 1188.3, pole=(132.993, -6.163), w0=302.695,
         wdot=56.3625225, tint=(0.50, 0.38, 0.28), atm="pluto"),
    body("charon", "Charon", "901", "moon", "pluto", 105.88, 606.0, pole=(132.993, -6.163), w0=122.695,
         wdot=56.3625225, tint=(0.30, 0.28, 0.27)),
    body("ceres", "Ceres", "2000001", "dwarf", "sun", 62.62890, 469.7, pole=(291.418, 66.764), w0=170.65,
         wdot=952.1532, tint=(0.09, 0.09, 0.09)),
    body("eris", "Eris", "136199;", "dwarf", "sun", 1108.0, 1163.0, tint=(0.80, 0.80, 0.78)),
    body("haumea", "Haumea", "136108;", "dwarf", "sun", 267.4, 780.0, tint=(0.75, 0.78, 0.80),
         note="triaxial 1161 x 852 x 513 km, drawn as a sphere of the volumetric mean radius"),
    body("makemake", "Makemake", "136472;", "dwarf", "sun", 206.0, 715.0, tint=(0.55, 0.38, 0.28)),
    # ---- asteroids ---------------------------------------------------------------------------------------------------
    body("vesta", "Vesta", "2000004", "asteroid", "sun", 17.288245, 262.7, pole=(309.031, 42.235), w0=285.39,
         wdot=1617.3331, tint=(0.38, 0.36, 0.33)),
    body("pallas", "Pallas", "2;", "asteroid", "sun", 13.665878, 256.0, tint=(0.14, 0.14, 0.15)),
    body("hygiea", "Hygiea", "10;", "asteroid", "sun", 5.78, 217.0, tint=(0.07, 0.07, 0.07)),
    # ---- comets and interstellar visitors (test particles: they feel the planets, they do not pull) ------------
    body("halley", "1P/Halley", "DES=1P;CAP", "comet", "sun", 0.0, 5.5, tint=(0.04, 0.04, 0.04),
         note="nucleus 15 x 8 x 8 km"),
    body("hale_bopp", "C/1995 O1 (Hale-Bopp)", "DES=C/1995 O1;", "comet", "sun", 0.0, 30.0, tint=(0.04, 0.04, 0.04)),
    body("67p", "67P/Churyumov-Gerasimenko", "DES=67P;CAP", "comet", "sun", 0.0, 1.65, tint=(0.06, 0.06, 0.06)),
    body("oumuamua", "1I/'Oumuamua", "DES=1I;", "interstellar", "sun", 0.0, 0.05, tint=(0.10, 0.07, 0.05),
         note="about 115 x 111 x 19 m, tumbling"),
    body("atlas", "3I/ATLAS", "DES=C/2025 N1;", "interstellar", "sun", 0.0, 1.5, tint=(0.05, 0.04, 0.04),
         note="nucleus size poorly known (0.4 to 5.6 km)"),
    # ---- spacecraft (point masses with no gravity of their own) --------------------------------------------------
    body("voyager1", "Voyager 1", "-31", "probe", "sun", 0.0, 0.0018, color=(1.0, 0.85, 0.4)),
    body("voyager2", "Voyager 2", "-32", "probe", "sun", 0.0, 0.0018, color=(1.0, 0.85, 0.4)),
    body("new_horizons", "New Horizons", "-98", "probe", "sun", 0.0, 0.0010, color=(1.0, 0.85, 0.4)),
    body("pioneer10", "Pioneer 10", "-23", "probe", "sun", 0.0, 0.0014, color=(1.0, 0.85, 0.4)),
    body("pioneer11", "Pioneer 11", "-24", "probe", "sun", 0.0, 0.0014, color=(1.0, 0.85, 0.4)),
    body("parker", "Parker Solar Probe", "-96", "probe", "sun", 0.0, 0.0010, color=(1.0, 0.85, 0.4)),
]
