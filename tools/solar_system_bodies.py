"""Physical data of the bodies in templates/solar-system.yaml (read by tools/fetch_ephemeris.py and gen_solar_system.py).

Sources (full list in docs/notes/solar_system.md):
  gm      JPL planetary ephemeris DE440/DE441 (Park et al. 2021) and the JPL satellite ephemeris summaries; km^3/s^2.
          For planets with moons the planet's own GM, not the system's: the moons are bodies of their own here.
          Moons added after the first eighteen: JPL SSD "Planetary Satellite Physical Parameters"
          (https://ssd.jpl.nasa.gov/sats/phys_par/, accessed 2026-10-30), GM column (ephemeris JUP365, JUP344, SAT415,
          SAT441, NEP097, PLU060). Where JPL gives only an upper limit (Kerberos, Styx) the GM is 0.0 (test particle).
          Where its sigma is as large as the value (small Neptunian moons, Adrastea, Pan) the value is JPL's anyway.
          The second batch has no JPL phys_par row: GM 0.0 (test particle). SAT441 itself sets GM 0 for Telesto, Calypso,
          Polydeuces and Methone (ssd NAIF sat441.cmt); the small Uranian GMs of Jacobson (2014) are not in any JPL/NAIF
          file readable here (ura116xl and ura184 carry only the five large moons), so they stay 0.
  radius  IAU/NASA fact sheets, km: equatorial for the planets (the sphere is not flattened, J2 carries the shape in
          the dynamics), mean for everything else. The added moons: the Mean Radius column of the JPL page above
          (Archinal et al. 2018 for Neptune's, Thomas 1998/2010 and Weaver et al. 2016 for the others, as cited there).
          Second batch: the mean of the three semi-axes (or the single radius) of BODY<id>_RADII in pck00011.tpc.
  pole, w0, wdot   IAU WGCCRE 2015 (Archinal et al. 2018): north pole (RA, Dec of ICRF) and prime meridian W = w0 + wdot d
          with d days from J2000 TDB (deg, deg/day); the periodic nutation/libration terms are left out. A negative wdot
          is a retrograde rotation about the IAU north pole. wdot None = rotation unknown or chaotic. The added moons:
          first (constant) term of BODY<id>_POLE_RA/POLE_DEC/PM in NAIF pck00011.tpc
          (https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc, Archinal et al. 2018), the linear term
          of PM as wdot; Himalia, the Jovian and Saturnian irregulars among the added moons, Methone, Pallene, Anthe,
          Polydeuces, Aegaeon and Pluto's small moons have no elements there. Mars, Phobos, Deimos, Titan,
          Neptune and Vesta were brought to the same file (IAU 2015); every other pole/w0/wdot was checked against it.
          The second batch of moons (Jovian irregulars, Saturn's Telesto to Aegaeon, Uranus' Cordelia to Puck) are the
          satellites with a Horizons state and a radius in pck00011 (Daphnis has no Horizons ephemeris after 2018).
  j2, j2r  Second zonal harmonic and its reference radius (km), from the planetary gravity-field solutions.
  tint    Linear albedo colour of a body without a map (geometric albedo and colour from the NASA fact sheets and papers).
          JPL's physical-parameter page lists no albedo: for the added moons it is the geometric albedo of the
          literature (Karkoschka 2003 for Neptune's, Weaver et al. 2016 for Pluto's, Verbiscer et al. 2007, Thomas et al.
          2010 and Simonelli et al. 1999 for Saturn's, Simonelli et al. 2000 for Jupiter's) as grey, warm for Himalia
          and Phoebe. The second batch is rough where uncited: 0.08 for the dark Jovian irregulars, 0.07 (0.10 for Puck,
          after Karkoschka 2001) for Uranus' inner moons, 0.5 for Saturn's small bright inner moons.
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
    body("mars", "Mars", "499", "planet", "sun", 42828.3752, 3396.19, pole=(317.269202, 54.432516), w0=176.049863,
         wdot=350.891982443297, tint=(0.5, 0.3, 0.2), tex=dict(albedo="mars/albedo_8k.jpg"), j2=1.96045e-3, j2r=3396.0,
         atm="mars"),
    body("phobos", "Phobos", "401", "moon", "mars", 7.087e-4, 11.1, pole=(317.67071657, 52.88627266), w0=35.1877444,
         wdot=1128.84475928, tint=(0.07, 0.065, 0.06)),
    body("deimos", "Deimos", "402", "moon", "mars", 9.62e-5, 6.2, pole=(316.65705808, 53.50992033), w0=79.39932954,
         wdot=285.16188899,
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
    body("himalia", "Himalia", "506", "moon", "jupiter", 0.15155, 85.0, tint=(0.045, 0.04, 0.036),
         note="irregular prograde, Sun-perturbed; no IAU pole"),
    body("thebe", "Thebe", "514", "moon", "jupiter", 0.03015, 49.3, pole=(268.05, 64.49), w0=8.56, wdot=533.70041,
         tint=(0.047, 0.047, 0.047)),
    body("adrastea", "Adrastea", "515", "moon", "jupiter", 0.00014, 8.2, pole=(268.05, 64.49), w0=33.29,
         wdot=1206.9986602, tint=(0.1, 0.1, 0.1), note="GM 0.00014 +- 0.0002 (JUP365): mass barely determined"),
    body("metis", "Metis", "516", "moon", "jupiter", 0.0025, 21.5, pole=(268.05, 64.49), w0=346.09, wdot=1221.2547301,
         tint=(0.061, 0.061, 0.061)),
    body("elara", "Elara", "507", "moon", "jupiter", 0.0, 40.0, tint=(0.08, 0.08, 0.08),
         note="irregular prograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
    body("pasiphae", "Pasiphae", "508", "moon", "jupiter", 0.0, 18.0, tint=(0.08, 0.08, 0.08),
         note="irregular retrograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
    body("sinope", "Sinope", "509", "moon", "jupiter", 0.0, 14.0, tint=(0.08, 0.08, 0.08),
         note="irregular retrograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
    body("lysithea", "Lysithea", "510", "moon", "jupiter", 0.0, 12.0, tint=(0.08, 0.08, 0.08),
         note="irregular prograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
    body("carme", "Carme", "511", "moon", "jupiter", 0.0, 15.0, tint=(0.08, 0.08, 0.08),
         note="irregular retrograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
    body("ananke", "Ananke", "512", "moon", "jupiter", 0.0, 10.0, tint=(0.08, 0.08, 0.08),
         note="irregular retrograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
    body("leda", "Leda", "513", "moon", "jupiter", 0.0, 5.0, tint=(0.08, 0.08, 0.08),
         note="irregular prograde, Sun-perturbed; no IAU pole; no published GM, test particle"),
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
    body("titan", "Titan", "606", "moon", "saturn", 8978.1382, 2574.73, pole=(39.4827, 83.4279), w0=186.5855,
         wdot=22.5769768, tint=(0.45, 0.30, 0.10), atm="titan"),
    body("hyperion", "Hyperion", "607", "moon", "saturn", 0.3727, 135.0, tint=(0.20, 0.18, 0.17),
         note="chaotic rotation, not turned"),
    body("iapetus", "Iapetus", "608", "moon", "saturn", 120.5038, 734.5, pole=(318.16, 75.03), w0=355.2,
         wdot=4.5379572, tint=(0.25, 0.22, 0.20)),
    body("phoebe", "Phoebe", "609", "moon", "saturn", 0.55479, 106.5, pole=(356.9, 77.8), w0=178.58, wdot=931.639,
         tint=(0.09, 0.08, 0.072), note="retrograde irregular, Sun-perturbed"),
    body("janus", "Janus", "610", "moon", "saturn", 0.12662, 89.2, pole=(40.58, 83.52), w0=58.83, wdot=518.2359876,
         tint=(0.71, 0.71, 0.71), note="co-orbital with Epimetheus"),
    body("epimetheus", "Epimetheus", "611", "moon", "saturn", 0.03514, 58.2, pole=(40.58, 83.52), w0=293.87,
         wdot=518.4907239, tint=(0.73, 0.73, 0.73), note="co-orbital with Janus"),
    body("helene", "Helene", "612", "moon", "saturn", 0.00048, 18.0, pole=(40.85, 83.34), w0=245.12, wdot=131.6174056,
         tint=(0.7, 0.7, 0.7), note="Dione L4 Trojan"),
    body("atlas_moon", "Atlas", "615", "moon", "saturn", 0.00037, 15.1, pole=(40.58, 83.53), w0=137.88, wdot=598.306,
         tint=(0.4, 0.4, 0.4)),
    body("prometheus", "Prometheus", "616", "moon", "saturn", 0.01071, 43.1, pole=(40.58, 83.53), w0=296.14,
         wdot=587.289, tint=(0.5, 0.5, 0.5)),
    body("pandora", "Pandora", "617", "moon", "saturn", 0.00926, 40.6, pole=(40.58, 83.53), w0=162.92, wdot=572.7891,
         tint=(0.5, 0.5, 0.5)),
    body("pan", "Pan", "618", "moon", "saturn", 0.00028, 14.0, pole=(40.6, 83.5), w0=48.8, wdot=626.044,
         tint=(0.5, 0.5, 0.5), note="GM 0.00028 +- 0.00014 (SAT415)"),
    body("telesto", "Telesto", "613", "moon", "saturn", 0.0, 12.633, pole=(50.51, 84.06), w0=56.88, wdot=190.6979332,
         tint=(0.5, 0.5, 0.5), note="Tethys L4 Trojan; GM 0 in SAT441 as well; no published GM, test particle"),
    body("calypso", "Calypso", "614", "moon", "saturn", 0.0, 10.3, pole=(36.41, 85.04), w0=153.51, wdot=190.6742373,
         tint=(0.5, 0.5, 0.5), note="Tethys L5 Trojan; GM 0 in SAT441 as well; no published GM, test particle"),
    body("polydeuces", "Polydeuces", "634", "moon", "saturn", 0.0, 1.233, tint=(0.5, 0.5, 0.5),
         note="Dione L5 Trojan; GM 0 in SAT441 as well; no IAU pole; no published GM, test particle"),
    body("methone", "Methone", "632", "moon", "saturn", 0.0, 1.48, tint=(0.5, 0.5, 0.5),
         note="GM 0 in SAT441 as well; no IAU pole; no published GM, test particle"),
    body("pallene", "Pallene", "633", "moon", "saturn", 0.0, 2.253, tint=(0.5, 0.5, 0.5),
         note="no IAU pole; no published GM, test particle"),
    body("anthe", "Anthe", "649", "moon", "saturn", 0.0, 0.5, tint=(0.5, 0.5, 0.5),
         note="no IAU pole; no published GM, test particle"),
    body("aegaeon", "Aegaeon", "653", "moon", "saturn", 0.0, 0.383, tint=(0.5, 0.5, 0.5),
         note="in the G ring arc; no IAU pole; no published GM, test particle"),
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
    body("cordelia", "Cordelia", "706", "moon", "uranus", 0.0, 13.0, pole=(257.31, -15.18), w0=127.69,
         wdot=-1074.520573, tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("ophelia", "Ophelia", "707", "moon", "uranus", 0.0, 15.0, pole=(257.31, -15.18), w0=130.35, wdot=-956.406815,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("bianca", "Bianca", "708", "moon", "uranus", 0.0, 21.0, pole=(257.31, -15.18), w0=105.46, wdot=-828.391476,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("cressida", "Cressida", "709", "moon", "uranus", 0.0, 31.0, pole=(257.31, -15.18), w0=59.16, wdot=-776.581632,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("desdemona", "Desdemona", "710", "moon", "uranus", 0.0, 27.0, pole=(257.31, -15.18), w0=95.08,
         wdot=-760.053169, tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("juliet", "Juliet", "711", "moon", "uranus", 0.0, 42.0, pole=(257.31, -15.18), w0=302.56, wdot=-730.125366,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("portia", "Portia", "712", "moon", "uranus", 0.0, 54.0, pole=(257.31, -15.18), w0=25.03, wdot=-701.486587,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("rosalind", "Rosalind", "713", "moon", "uranus", 0.0, 27.0, pole=(257.31, -15.18), w0=314.9, wdot=-644.631126,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("belinda", "Belinda", "714", "moon", "uranus", 0.0, 33.0, pole=(257.31, -15.18), w0=297.46, wdot=-577.362817,
         tint=(0.07, 0.07, 0.07), note="no published GM, test particle"),
    body("puck", "Puck", "715", "moon", "uranus", 0.0, 77.0, pole=(257.31, -15.18), w0=91.24, wdot=-472.545069,
         tint=(0.1, 0.1, 0.1), note="no published GM, test particle"),
    body("neptune", "Neptune", "899", "planet", "sun", 6835099.5, 24764.0, pole=(299.36, 43.46), w0=249.978,
         wdot=541.1397757, tint=(0.25, 0.40, 0.80), tex=dict(albedo="neptune/albedo_2k.jpg"), j2=3.411e-3, j2r=24764.0,
         atm="neptune"),
    body("triton", "Triton", "801", "moon", "neptune", 1427.6, 1353.4, pole=(299.36, 41.17), w0=296.53,
         wdot=-61.2572637, tint=(0.70, 0.65, 0.60)),
    body("nereid", "Nereid", "802", "moon", "neptune", 2.06, 170.0, tint=(0.15, 0.15, 0.15), note="rotation unknown"),
    body("naiad", "Naiad", "803", "moon", "neptune", 0.00853, 29.0, pole=(299.36, 43.36), w0=254.06, wdot=1222.8441209,
         tint=(0.072, 0.072, 0.072), note="GM 0.00853 +- 0.0048 (NEP097)"),
    body("thalassa", "Thalassa", "804", "moon", "neptune", 0.02359, 40.0, pole=(299.36, 43.45), w0=102.06,
         wdot=1155.7555612, tint=(0.091, 0.091, 0.091), note="GM 0.02359 +- 0.0052 (NEP097)"),
    body("despina", "Despina", "805", "moon", "neptune", 0.11673, 74.0, pole=(299.36, 43.45), w0=306.51,
         wdot=1075.7341562, tint=(0.09, 0.09, 0.09), note="GM sigma larger than half the value (NEP097)"),
    body("galatea", "Galatea", "806", "moon", "neptune", 0.1899, 79.0, pole=(299.36, 43.43), w0=258.09,
         wdot=839.6597686, tint=(0.079, 0.079, 0.079), note="GM sigma larger than the value (NEP097)"),
    body("larissa", "Larissa", "807", "moon", "neptune", 0.25484, 96.0, pole=(299.36, 43.41), w0=179.41,
         wdot=649.053447, tint=(0.091, 0.091, 0.091), note="GM sigma larger than the value (NEP097)"),
    body("proteus", "Proteus", "808", "moon", "neptune", 2.58342, 208.0, pole=(299.27, 42.91), w0=93.38,
         wdot=320.7654228, tint=(0.096, 0.096, 0.096), note="GM 2.58 +- 2.4 (NEP097)"),
    # ---- dwarf planets ------------------------------------------------------------------------------------------
    body("pluto", "Pluto", "999", "dwarf", "sun", 869.6, 1188.3, pole=(132.993, -6.163), w0=302.695,
         wdot=56.3625225, tint=(0.50, 0.38, 0.28), atm="pluto"),
    body("charon", "Charon", "901", "moon", "pluto", 105.88, 606.0, pole=(132.993, -6.163), w0=122.695,
         wdot=56.3625225, tint=(0.30, 0.28, 0.27)),
    body("nix", "Nix", "902", "moon", "pluto", 0.0015, 18.0, tint=(0.56, 0.56, 0.56),
         note="circumbinary with Pluto-Charon; no IAU rotation, chaotic"),
    body("hydra", "Hydra", "903", "moon", "pluto", 0.002, 18.5, tint=(0.83, 0.83, 0.83),
         note="circumbinary; chaotic rotation"),
    body("kerberos", "Kerberos", "904", "moon", "pluto", 0.0, 6.0, tint=(0.56, 0.56, 0.56),
         note="JPL gives only GM < 0.0002: test particle; chaotic rotation"),
    body("styx", "Styx", "905", "moon", "pluto", 0.0, 5.2, tint=(0.65, 0.65, 0.65),
         note="JPL gives only GM < 0.0003: test particle; chaotic rotation"),
    body("ceres", "Ceres", "2000001", "dwarf", "sun", 62.62890, 469.7, pole=(291.418, 66.764), w0=170.65,
         wdot=952.1532, tint=(0.09, 0.09, 0.09)),
    body("eris", "Eris", "136199;", "dwarf", "sun", 1108.0, 1163.0, tint=(0.80, 0.80, 0.78)),
    body("haumea", "Haumea", "136108;", "dwarf", "sun", 267.4, 780.0, tint=(0.75, 0.78, 0.80),
         note="triaxial 1161 x 852 x 513 km, drawn as a sphere of the volumetric mean radius"),
    body("makemake", "Makemake", "136472;", "dwarf", "sun", 206.0, 715.0, tint=(0.55, 0.38, 0.28)),
    # ---- asteroids ---------------------------------------------------------------------------------------------------
    body("vesta", "Vesta", "2000004", "asteroid", "sun", 17.288245, 262.7, pole=(309.031, 42.235), w0=285.39,
         wdot=1617.3329428, tint=(0.38, 0.36, 0.33)),
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


# Real spacecraft maps of the moons and dwarf planets (tools/fetch_textures.py --groups moons, sources in
# docs/notes/texture_sources.md). Eris, Haumea and Makemake have never been imaged: they keep their tint.
MOON_MAPS = {
    "io": "io/albedo_8k.jpg", "europa": "europa/albedo_8k.jpg", "ganymede": "ganymede/albedo_8k.jpg",
    "callisto": "callisto/albedo_8k.jpg", "titan": "titan/albedo_4k.jpg", "enceladus": "enceladus/albedo_8k.jpg",
    "mimas": "mimas/albedo_6k.jpg", "tethys": "tethys/albedo_8k.jpg", "dione": "dione/albedo_8k.jpg",
    "rhea": "rhea/albedo_8k.jpg", "iapetus": "iapetus/albedo_6k.jpg", "triton": "triton/albedo_8k.jpg",
    "pluto": "pluto/albedo_8k.jpg", "charon": "charon/albedo_8k.jpg", "ceres": "ceres/albedo_7k.jpg",
    "vesta": "vesta/albedo_8k.jpg", "phobos": "phobos/albedo_6k.jpg",
}
for _body in BODIES:
    if _body["id"] in MOON_MAPS:
        _body["tex"] = dict(albedo=MOON_MAPS[_body["id"]])
