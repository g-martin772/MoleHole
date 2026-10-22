#!/usr/bin/env python3
"""Fetch barycentric state vectors of the solar-system bodies from JPL Horizons into tools/data/ephemeris_<epoch>.json.

  tools/fetch_ephemeris.py                     # epoch 2026-10-22 00:00 TDB, every body in tools/solar_system_bodies.py
  tools/fetch_ephemeris.py --epoch 2000-01-01  # another epoch (JSON name follows it)
  tools/fetch_ephemeris.py --force             # fetch again, ignoring the cache

Frame: ecliptic and mean equinox of J2000, centre the solar-system barycentre (500@0), geometric states (no light-time
or aberration), km and km/s as Horizons prints them. Planets and satellites come from DE441 / the satellite kernels,
small bodies from their JPL SBDB orbit solution. tools/gen_solar_system.py turns the JSON into templates/solar-system.yaml.
"""
import argparse
import datetime
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from solar_system_bodies import BODIES  # noqa: E402

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
API = "https://ssd.jpl.nasa.gov/api/horizons.api"
NUMBER = r"([-+]?\d+\.\d+(?:[Ee][-+]?\d+)?)"


def query(command, epoch):
    stop = (datetime.date.fromisoformat(epoch) + datetime.timedelta(days=1)).isoformat()
    params = {
        "format": "text", "COMMAND": f"'{command}'", "OBJ_DATA": "'NO'", "MAKE_EPHEM": "'YES'", "EPHEM_TYPE": "'VECTORS'",
        "CENTER": "'500@0'", "START_TIME": f"'{epoch}'", "STOP_TIME": f"'{stop}'", "STEP_SIZE": "'1d'", "VEC_TABLE": "'2'",
        "OUT_UNITS": "'KM-S'", "REF_PLANE": "'ECLIPTIC'", "REF_SYSTEM": "'ICRF'", "TIME_TYPE": "'TDB'", "CSV_FORMAT": "'NO'",
    }
    url = API + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=120) as response:
        text = response.read().decode()
    block = text.split("$$SOE")
    if len(block) < 2:
        raise RuntimeError(f"{command}: no ephemeris in the reply:\n{text[:600]}")
    first = block[1].split("$$EOE")[0]
    # lines: "JD = A.D. ... TDB", " X = .. Y = .. Z = ..", " VX= .. VY= .. VZ= .."
    lines = [line for line in first.strip().splitlines() if line.strip()]
    pos = [float(x) for x in re.findall(NUMBER, lines[1])]
    vel = [float(x) for x in re.findall(NUMBER, lines[2])]
    if len(pos) != 3 or len(vel) != 3:
        raise RuntimeError(f"{command}: cannot parse\n{first}")
    target = re.search(r"Target body name: (.*?)\s{2,}", text)
    return {"jd": float(lines[0].split()[0]), "position_km": pos, "velocity_km_s": vel,
            "horizons_name": target.group(1) if target else command}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--epoch", default="2026-10-22")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    path = os.path.join(DATA, f"ephemeris_{args.epoch}.json")
    cache = {}
    if os.path.exists(path) and not args.force:
        with open(path) as f:
            cache = json.load(f)
    failed = []
    for body in BODIES:
        key = body["id"]
        if key in cache.get("bodies", {}):
            continue
        for attempt in range(3):
            try:
                cache.setdefault("bodies", {})[key] = query(body["horizons"], args.epoch)
                print(f"{key:14s} {cache['bodies'][key]['horizons_name']}")
                break
            except Exception as error:  # network or Horizons ambiguity
                print(f"{key}: attempt {attempt + 1} failed: {str(error)[:300]}", file=sys.stderr)
                time.sleep(2)
        else:
            failed.append(key)
        time.sleep(0.3)
    cache["epoch"] = args.epoch
    cache["frame"] = "ecliptic J2000, barycentric, km and km/s, TDB"
    os.makedirs(DATA, exist_ok=True)
    with open(path, "w") as f:
        json.dump(cache, f, indent=1)
    if failed:
        print("failed:", ", ".join(failed), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
