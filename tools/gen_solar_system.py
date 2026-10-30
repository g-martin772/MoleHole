#!/usr/bin/env python3
"""Write templates/solar-system.yaml from tools/data/ephemeris_<epoch>.json and tools/solar_system_bodies.py.

  tools/fetch_ephemeris.py       # once, needs the network (JPL Horizons)
  tools/gen_solar_system.py      # offline, deterministic; writes templates/solar-system.yaml
  tools/gen_solar_system.py --black-hole   # templates/solar-system-black-hole.yaml: the Sun replaced by a 1 solar-mass hole

Frame. Horizons gives ecliptic J2000 axes (x to the vernal equinox, z to the ecliptic north pole). The scene is Y up, so
scene (x, y, z) = ecliptic (x, z, -y), a proper rotation: the ecliptic is the X-Z plane and +Y is ecliptic north. The
origin is the solar-system barycentre. Lengths are metres, speeds metres per second.

Spin. The IAU pole (RA, Dec) goes through the equatorial-to-ecliptic rotation of the obliquity 23.4392911 deg and then
the axis map above. The sphere's SpinAxis is that pole; PrimeMeridian is chosen so that the IAU prime meridian
W = w0 + wdot d (d days from J2000 TDB) points where it should at the epoch. The engine turns a body about SpinAxis
from a reference direction (the +X axis carried to the pole by the shortest rotation, see BodyOrientation in
src/rendering/bodies.cppm); the offset between that direction and the IAU node of the equator is folded into
PrimeMeridian. RotationPeriod = 360 deg / wdot, negative for a retrograde body.
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from solar_system_bodies import BODIES, AU_KM  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBLIQUITY = math.radians(23.4392911)
JD_J2000 = 2451545.0
GUID_BASE = 7500000000000000000
GUID_BASE_BLACK_HOLE = 7600000000000000000
TIME_SCALE = 14400
# The renderer draws at most 64 spheres (kMaxSpheres in src/rendering/render_data.cppm); the biggest bodies get one,
# the rest are simulated and drawn as orbit-line markers only
SPHERE_BUDGET = 62
G = 6.67430e-11                 # CODATA 2022
M_P = 1.67262192595e-27         # kg, CODATA 2022
SIGMA_T = 6.6524587051e-29      # m^2, CODATA 2022
C = 299792458.0
L_SUN = 3.828e26                # W, IAU 2015 B3
BLACK_HOLE_ARM_M = 6.0e4        # about 40 r_g of the 1 solar-mass hole


def ecliptic_to_scene(v):
    return (v[0], v[2], -v[1])


def equatorial_to_scene(v):
    c, s = math.cos(OBLIQUITY), math.sin(OBLIQUITY)
    return ecliptic_to_scene((v[0], v[1] * c + v[2] * s, -v[1] * s + v[2] * c))


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return tuple(x / n for x in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def pole_vector(ra_deg, dec_deg):
    ra, dec = math.radians(ra_deg), math.radians(dec_deg)
    return unit(equatorial_to_scene((math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec))))


def reference_direction(pole):
    """The body-frame +X of the engine: +X carried to the pole by the shortest rotation taking +Y to it."""
    y = (0.0, 1.0, 0.0)
    axis = cross(y, pole)
    sin_a = math.sqrt(dot(axis, axis))
    cos_a = dot(y, pole)
    x = (1.0, 0.0, 0.0)
    if sin_a < 1e-12:
        return x if cos_a > 0 else (-1.0, 0.0, 0.0)
    k = tuple(c / sin_a for c in axis)
    # Rodrigues rotation of x about k by the angle between y and the pole
    kx = cross(k, x)
    kd = dot(k, x)
    return tuple(x[i] * cos_a + kx[i] * sin_a + k[i] * kd * (1 - cos_a) for i in range(3))


def signed_angle(a, b, axis):
    return math.atan2(dot(axis, cross(a, b)), dot(a, b))


def spin_phase(body, jd):
    """(SpinAxis, PrimeMeridian deg) putting the IAU prime meridian in place at the epoch."""
    ra, dec = body["pole"]
    pole = pole_vector(ra, dec)
    # Ascending node of the body's equator on the ICRF equator: the direction the IAU measures W from
    node = unit(equatorial_to_scene((-math.sin(math.radians(ra)), math.cos(math.radians(ra)), 0.0)))
    w = body["w0"] + body["wdot"] * (jd - JD_J2000)
    offset = math.degrees(signed_angle(reference_direction(pole), node, pole))
    return pole, (w + offset) % 360.0


def fmt(x):
    if x == 0:
        return "0"
    return repr(float(x))


def vec(v):
    return "[" + ", ".join(fmt(x) for x in v) + "]"


def atmosphere(kind, radius_m):
    """Hillaire-style parameters per planet (see docs/notes/solar_system.md, section atmospheres)."""
    table = {
        # Earth keeps the component defaults (Hillaire 2020, Table 1)
        "earth": None,
        # dense CO2: opaque cloud deck at 50 to 70 km, strong forward-scattering sulphuric-acid haze
        "venus": dict(Height=110e3, R=(7.0e-6, 1.1e-5, 2.4e-5), RH=15e3, M=2.5e-4, MA=1.5e-5, MH=8e3, g=0.7),
        # 6 mbar CO2 with suspended dust: faint blue Rayleigh, butterscotch dust scattering (Mie, red-dominant absorption)
        "mars": dict(Height=60e3, R=(1.6e-7, 3.6e-7, 8.7e-7), RH=11e3, M=2.0e-6, MA=1.0e-6, MH=11e3, g=0.6),
        # H2/He above the ammonia deck: Rayleigh scale height 27 km, Earth's coefficients x 0.35 (H2 is a weaker
        # scatterer per molecule) at the 1 bar level
        "jupiter": dict(Height=300e3, R=(2.0e-6, 4.7e-6, 1.16e-5), RH=27e3, M=2.0e-6, MA=1.0e-6, MH=20e3, g=0.8),
        "saturn": dict(Height=500e3, R=(1.6e-6, 3.8e-6, 9.3e-6), RH=60e3, M=1.5e-6, MA=1.0e-6, MH=40e3, g=0.8),
        # H2/He/CH4: methane absorbs red, so the Rayleigh limb is blue (CH4 as the red absorber below)
        "uranus": dict(Height=500e3, R=(1.0e-6, 2.4e-6, 5.9e-6), RH=27e3, M=2.0e-7, MA=2.0e-7, MH=30e3, g=0.7,
                       absorb=(1.2e-6, 2.0e-7, 0.0)),
        "neptune": dict(Height=500e3, R=(1.0e-6, 2.4e-6, 5.9e-6), RH=20e3, M=3.0e-7, MA=3.0e-7, MH=25e3, g=0.7,
                        absorb=(1.4e-6, 2.0e-7, 0.0)),
        # Titan: 1.5 bar N2 with a thick orange tholin haze up to 500 km
        "titan": dict(Height=500e3, R=(1.0e-6, 2.2e-6, 5.0e-6), RH=21e3, M=6.0e-6, MA=4.0e-6, MH=60e3, g=0.8,
                      absorb=(0.0, 1.0e-6, 3.0e-6)),
        # 10 microbar N2: haze layers to 200 km, blue because the haze particles are small
        "pluto": dict(Height=200e3, R=(0, 0, 0), RH=50e3, M=3.0e-9, MA=5.0e-10, MH=50e3, g=0.5),
    }
    return table.get(kind)


# (body id, label, arm length m, pitch deg, phase angle deg): the arm is about four body radii, so the body fills a third
# of the view. The phase angle is the Sun-body-camera angle (0 = fully lit, 90 = half, 180 = new); None = default 40 deg.
CAMERAS = [
    ("earth", "Earth", 2.6e7, 20, 80),
    ("earth", "Earth and Moon", 9.0e8, 35, 70),
    ("sun", "Inner solar system", 8.0e11, 55, None),
    ("sun", "Outer solar system", 1.3e13, 45, None),
    ("moon", "Moon", 7.0e6, 15, None),
    ("sun", "Sun", 2.8e9, 10, None),
    ("mercury", "Mercury", 1.0e7, 15, None),
    ("venus", "Venus", 2.6e7, 15, None),
    ("mars", "Mars", 1.4e7, 15, None),
    ("phobos", "Phobos", 6.0e4, 20, None),
    ("jupiter", "Jupiter", 3.2e8, 12, None),
    ("jupiter", "Jupiter system", 3.2e9, 25, 60),
    ("io", "Io", 7.0e6, 15, None),
    ("europa", "Europa", 6.5e6, 15, None),
    ("ganymede", "Ganymede", 1.1e7, 15, None),
    ("callisto", "Callisto", 1.0e7, 15, None),
    ("saturn", "Saturn", 4.0e8, 15, None),
    ("saturn", "Saturn system", 3.0e9, 25, None),
    ("titan", "Titan", 1.0e7, 15, None),
    ("enceladus", "Enceladus", 1.0e6, 15, None),
    ("uranus", "Uranus", 1.0e8, 15, None),
    ("neptune", "Neptune", 1.0e8, 15, None),
    ("triton", "Triton", 5.5e6, 15, None),
    ("pluto", "Pluto", 5.0e6, 15, None),
    ("charon", "Charon", 2.4e6, 15, None),
    ("ceres", "Ceres", 2.0e6, 15, None),
    ("halley", "Halley", 2.0e4, 15, None),
]


def look_at_rotation(position, target, up=(0.0, 1.0, 0.0)):
    """Quaternion (x, y, z, w) of a camera at `position` looking at `target`; the camera looks down its -Z."""
    f = unit(tuple(target[i] - position[i] for i in range(3)))
    r = unit(cross(f, up))
    u = cross(r, f)
    m = ((r[0], u[0], -f[0]), (r[1], u[1], -f[1]), (r[2], u[2], -f[2]))  # columns r, u, -f
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        k = math.sqrt(t + 1.0) * 2
        q = ((m[2][1] - m[1][2]) / k, (m[0][2] - m[2][0]) / k, (m[1][0] - m[0][1]) / k, 0.25 * k)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        k = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (0.25 * k, (m[0][1] + m[1][0]) / k, (m[0][2] + m[2][0]) / k, (m[2][1] - m[1][2]) / k)
    elif m[1][1] > m[2][2]:
        k = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / k, 0.25 * k, (m[1][2] + m[2][1]) / k, (m[0][2] - m[2][0]) / k)
    else:
        k = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / k, (m[1][2] + m[2][1]) / k, 0.25 * k, (m[1][0] - m[0][1]) / k)
    n = math.sqrt(sum(x * x for x in q))
    return tuple(x / n for x in q)


def pole_of(key):
    body = next(b for b in BODIES if b["id"] == key)
    return pole_vector(*body["pole"]) if body["pole"] else None


def view_angles(key, target, sun, pole, pitch, phase=None):
    """Yaw and pitch (deg, the spring-arm convention: direction = (cos p sin y, sin p, cos p cos y)) of a camera that sees
    the body at the phase angle `phase` (default: 40 deg round from the Sun's direction, about three quarters lit). A ringed body is seen 25 deg above its ring
    plane instead, so the rings open."""
    if key == "sun":
        return 25.0, pitch
    to_sun = unit(tuple(sun[i] - target[i] for i in range(3)))
    offset = 40.0 if phase is None else math.degrees(math.acos(min(1.0, math.cos(math.radians(phase)) / math.cos(math.radians(pitch)))))
    yaw = math.degrees(math.atan2(to_sun[0], to_sun[2])) + offset
    if key == "saturn" and pole is not None:
        horizontal = tuple(to_sun[i] - dot(to_sun, pole) * pole[i] for i in range(3))
        horizontal = unit(horizontal)
        e = cross(pole, horizontal)
        a = math.radians(40.0)
        round_ = tuple(math.cos(a) * horizontal[i] + math.sin(a) * e[i] for i in range(3))
        up = math.radians(25.0)
        d = unit(tuple(math.cos(up) * round_[i] + math.sin(up) * pole[i] for i in range(3)))
        return math.degrees(math.atan2(d[0], d[2])), math.degrees(math.asin(d[1]))
    return yaw, pitch


def build(epoch, primary=None, black_hole=False):
    path = os.path.join(ROOT, "tools", "data", f"ephemeris_{epoch}.json")
    with open(path) as f:
        data = json.load(f)
    states = data["bodies"]
    jd = next(iter(states.values()))["jd"]
    lines = []
    out = lines.append
    title = "Solar system with a black hole" if black_hole else "Solar system"
    out("# " + title + " on JPL DE441 / SBDB state vectors at " + epoch + " 00:00 TDB (JD " + repr(jd) + ").")
    out("# Generated by tools/gen_solar_system.py from tools/data/ephemeris_" + epoch + ".json; do not edit by hand,")
    out("# change tools/solar_system_bodies.py and run the generator again. Background: docs/notes/solar_system.md")
    out("# Needs the maps from tools/fetch_textures.py (groups core and moons). Run with 'Play': the N-Body module integrates")
    out("# every body with 1PN relativity and the J2 of the oblate planets; TimeScale is simulated seconds per second (4 hours per second).")
    if black_hole:
        out("# The Sun is replaced by a non-rotating black hole of the same GM, so every orbit is unchanged. Light: a point light of")
        out("# the Sun's irradiance at 1 AU, as if the hole were fed at L = 1 L_sun (Mdot = L / (eta c^2), L/L_Edd = 3e-5); a hole")
        out("# that shines at the Eddington limit (1.26e31 W) would give the planets 33,000 times the sunlight.")
    out("Scene: " + title)
    out("SchemaVersion: 2")
    out(f"TimeScale: {TIME_SCALE}")
    out("Entities:")

    guid = {}
    for index, body in enumerate(BODIES):
        guid[body["id"]] = (GUID_BASE_BLACK_HOLE if black_hole else GUID_BASE) + 1 + index

    drawable = [b for b in BODIES if b["kind"] != "probe"]
    drawable.sort(key=lambda b: (b["kind"] != "star", -b["radius"]))
    spheres = {b["id"] for b in drawable[:SPHERE_BUDGET]}

    def entity_header(g, name, tag):
        out(f"  - Guid: {g}")
        out("    Components:")
        out("      Metadata:")
        out(f"        Guid: {g}")
        out(f"        Name: {json.dumps(name)}")
        out(f"        TypeTag: {tag}")

    def transform(position, rotation=(0, 0, 0, 1)):
        out("      Transform:")
        out(f"        Position: {vec(position)}")
        out(f"        Rotation: {vec(rotation)}")
        out("        Scale: [1, 1, 1]")

    positions = {}
    for body in BODIES:
        state = states[body["id"]]
        p = ecliptic_to_scene([c * 1000.0 for c in state["position_km"]])
        v = ecliptic_to_scene([c * 1000.0 for c in state["velocity_km_s"]])
        positions[body["id"]] = p
        radius_m = body["radius"] * 1000.0
        g = guid[body["id"]]
        is_sun = body["kind"] == "star"
        has_sphere = body["id"] in spheres
        if is_sun and black_hole:
            entity_header(g, "Black hole", "BlackHole")
        else:
            entity_header(g, body["name"], "Star" if is_sun else "Sphere" if has_sphere else "Body")
        transform(p)
        pole = (0.0, 1.0, 0.0)
        prime = 0.0
        period = 0.0
        if body["pole"] is not None and body["wdot"] is not None:
            pole, prime = spin_phase(body, jd)
            period = 360.0 / body["wdot"] * 86400.0
        elif body["pole"] is not None:
            pole = pole_vector(*body["pole"])
        color = body["color"] or (1.0, 1.0, 1.0)
        if has_sphere and not (is_sun and black_hole):
            out("      Sphere:")
            out(f"        Radius: {fmt(radius_m)}")
            out("        Spin: 0")
            out(f"        Color: {vec(color)}")
            out(f"        SpinAxis: {vec(pole)}")
            out('        TexturePath: ""')
        out("      NBody:")
        out(f"        GM: {fmt(body['gm'] * 1e9)}")
        out(f"        Velocity: {vec(v)}")
        if body["j2"] and has_sphere and not (is_sun and black_hole):
            out(f"        J2: {fmt(body['j2'])}")
            out(f"        J2Radius: {fmt(body['j2r'] * 1000.0 if body['kind'] != 'star' else radius_m)}")
        if is_sun and black_hole:
            out("      BlackHole:")
            out(f"        Mass: {fmt(body['gm'] * 1e9 / G)}")
            out("        Spin: 0")
            out("        Charge: 0")
            out("        SpinAxis: [0, 1, 0]")
            out("      Light:")
            out("        Kind: Point")
            out("        Color: [0.85, 0.92, 1.0]")
            out("        Intensity: 3.14159")
            out("        Direction: [1, 0, 0]")
            out(f"        ReferenceDistance: {fmt(AU_KM * 1000.0)}")
            continue
        if body["kind"] == "star":
            out("      Star:")
            out("        Teff: 5772")
            out(f"        Radius: {fmt(radius_m)}")
            out(f"        Mass: {fmt(body['gm'] * 1e9 / 6.6743e-11)}")
            out("        LimbDarkening: 0.6")
            out("      Light:")
            out("        Kind: Point")
            out("        Color: [1, 0.96, 0.9]")
            out("        Intensity: 3.14159")
            out("        Direction: [1, 0, 0]")
            out(f"        ReferenceDistance: {fmt(AU_KM * 1000.0)}")
            continue
        if not has_sphere:
            continue
        tex = body["tex"]
        out("      Surface:")
        if "albedo" in tex:
            out(f"        Albedo: assets/textures/{tex['albedo']}")
        if "normal" in tex:
            out(f"        Normal: assets/textures/{tex['normal']}")
        if "roughness" in tex:
            out(f"        Roughness: assets/textures/{tex['roughness']}")
            out("        RoughnessIsSpecular: true")
        if "emissive" in tex:
            out(f"        Emissive: assets/textures/{tex['emissive']}")
            out("        EmissiveStrength: 0.04")
        if "albedo" not in tex:
            out(f"        Tint: {vec(body['tint'])}")
        if body["id"] != "earth":
            out("        RoughnessFactor: 1")
        if period:
            out(f"        RotationPeriod: {fmt(period)}")
            out(f"        PrimeMeridian: {fmt(round(prime, 4))}")
        if body["id"] == "earth":
            out("      Terrain:")
            out("        Height: assets/textures/earth/height_5k.jpg")
            out("        HeightMin: 0")
            out("        HeightMax: 6400")
            out("        Ocean: true")
            out("        DetailAmplitude: 120")
            out("        DetailScale: 3000")
            out("        DetailOctaves: 5")
            out("        Seed: 1")
        atm = body["atm"]
        if body["id"] == "earth":
            out("      Atmosphere:")
            out("        Height: 100000.0")
        elif atm:
            a = atmosphere(atm, radius_m)
            out("      Atmosphere:")
            out(f"        Height: {fmt(a['Height'])}")
            out(f"        RayleighScattering: {vec(a['R'])}")
            out(f"        RayleighScaleHeight: {fmt(a['RH'])}")
            out(f"        MieScattering: {fmt(a['M'])}")
            out(f"        MieAbsorption: {fmt(a['MA'])}")
            out(f"        MieScaleHeight: {fmt(a['MH'])}")
            out(f"        MieAnisotropy: {fmt(a['g'])}")
            absorb = a.get("absorb", (0.0, 0.0, 0.0))
            out(f"        OzoneAbsorption: {vec(absorb)}")
            out(f"        OzoneCenter: {fmt(a['Height'] * 0.3)}")
            out(f"        OzoneWidth: {fmt(a['Height'] * 0.6)}")
        if body["id"] == "earth":
            out("      CloudLayer:")
            out(f"        Coverage: assets/textures/{tex['clouds']}")
            out("        Altitude: 6000.0")
            out("        Opacity: 0.95")
            out("        Color: [1, 1, 1]")
            out("        RotationPeriod: 0.0")
        if "rings" in tex:
            out("      Ring:")
            out(f"        Texture: assets/textures/{tex['rings']}")
            out("        InnerRadius: 74658000.0")
            out("        OuterRadius: 136780000.0")
            out("        Opacity: 1")

    # ---- cameras: each rides the spring-arm script around a body (scripts/spring_arm_camera.luau) -----------------
    cameras = [("sun", "Black hole", BLACK_HOLE_ARM_M, 20, None)] + [c for c in CAMERAS if c[1] != "Sun"] if black_hole else CAMERAS
    for index, (key, label, arm_m, pitch, phase) in enumerate(cameras):
        target = positions[key]
        yaw, pitch = view_angles(key, target, positions["sun"], pole_of(key), pitch, phase)
        d = (math.cos(math.radians(pitch)) * math.sin(math.radians(yaw)), math.sin(math.radians(pitch)),
             math.cos(math.radians(pitch)) * math.cos(math.radians(yaw)))
        position = tuple(target[i] + d[i] * arm_m for i in range(3))
        g = (GUID_BASE_BLACK_HOLE if black_hole else GUID_BASE) + 1000 + index
        entity_header(g, "Camera: " + label, "Camera")
        transform(position, look_at_rotation(position, target))
        out("      Camera:")
        out("        Fov: 45")
        out("        NearPlane: 10")
        out("        FarPlane: {value: 1000, unit: AU}")
        out(f"        Primary: {'true' if (label == primary if primary else index == 0) else 'false'}")
        out("      Scripts:")
        out("        - Name: spring_arm_camera")
        out("          Props:")
        out(f"            ArmLength: {fmt(arm_m)}")
        out("            HeightOffset: 0")
        out("            LookAtTarget: true")
        out("            OrbitSpeed: 2")
        out(f"            Pitch: {fmt(round(pitch, 3))}")
        out("            Smoothing: 0")
        out(f"            Target: {guid[key]}")
        out(f"            Yaw: {fmt(round(yaw, 3))}")

    clean = "Black hole system (clean)" if black_hole else "Solar system (clean)"
    main = "Black hole system" if black_hole else "Solar system"
    base = "Flat"
    out("Render:")
    out("  Version: 1")
    out("  Contexts:")
    out(f"    Edit: {main}")
    out(f"    Play: {main}")
    out("    Reference: Reference")
    out(f"    Export: {clean}")
    out("  Profiles:")
    lensing = "straight rays, the hole drawn as its analytic shadow, photon ring and disk" if black_hole else "straight rays (no black hole)"
    light = "the hole's point light" if black_hole else "the Sun as the point light"
    for name, orbits in ((main, True), (clean, False)):
        out(f"    - Name: {name}")
        out("      Description: " + json.dumps(f"{lensing.capitalize()}, {light}, automatic exposure so every planet shows from Mercury to Neptune"
                                               + (", orbit lines and markers on." if orbits else ", no overlays.")))
        out(f"      Base: {base}")
        out("      Set:")
        out("        post.tonemap: Aces")
        out("        post.exposure_mode: Auto")
        out("        post.exposure_key: 0.09")
        out("        post.bloom: true")
        out("        post.bloom_intensity: 1.5")
        out("        post.lens_flare: false")
        if orbits:
            out("        overlay.body_orbits: true")
    if black_hole:
        out("    - Name: Black hole close-up")
        out("      Description: " + json.dumps("Geodesic integration around the hole with the planets hidden: with them in the scene the lensing kernels take seconds per frame (every ray is tested against all 62 spheres). Use it with the Black hole camera."))
        out("      Base: Traced")
        out("      Set:")
        out("        objects.spheres: false")
        out("        post.tonemap: Aces")
        out("        post.exposure_mode: Auto")
        out("        post.exposure_key: 0.09")
        out("        post.bloom: true")
        out("        post.lens_flare: false")
    if not black_hole:
        out(f"    - Name: {main} (outer planets)")
        out("      Description: " + json.dumps("Fixed exposure +3 EV for Jupiter and beyond: they get 4 % (Jupiter) to 0.1 % (Neptune) of the light Earth does, and automatic exposure would lift the starry sky with them."))
        out("      Base: Flat")
        out("      Set:")
        out("        post.tonemap: Aces")
        out("        post.exposure_mode: Manual")
        out("        post.exposure: 3")
        out("        post.bloom: true")
        out("        post.bloom_intensity: 1.5")
        out("        post.lens_flare: false")
    out(f"    - Name: {main} (sunny 16)")
    out("      Description: " + json.dumps("A physical camera at f/16, 1/100 s, ISO 100: sunlit scenes at 1 AU and the Sun's disc are exposed right, the outer planets fall dark as they do to a real camera."))
    out(f"      Base: {base}")
    out("      Set:")
    out("        post.tonemap: Aces")
    out("        post.exposure_mode: Physical")
    out("        post.camera_aperture: 16")
    out("        post.camera_shutter: 0.01")
    out("        post.camera_iso: 100")
    out("        post.bloom: true")
    out("        post.bloom_intensity: 1.5")
    out("        post.lens_flare: false")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--epoch", default="2026-10-22")
    parser.add_argument("--primary", help="label of the camera that is the primary one (default: the first)")
    parser.add_argument("--black-hole", action="store_true", help="replace the Sun by a 1 solar-mass black hole")
    parser.add_argument("--output")
    args = parser.parse_args()
    if not args.output:
        args.output = os.path.join(ROOT, "templates", "solar-system-black-hole.yaml" if args.black_hole else "solar-system.yaml")
    text = build(args.epoch, args.primary, args.black_hole)
    with open(args.output, "w") as f:
        f.write(text)
    print("wrote", args.output, f"({text.count(chr(10))} lines)")
