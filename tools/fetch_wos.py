#!/usr/bin/env python3
"""Fetch ZX Spectrum tape images from World of Spectrum.

Only titles whose publisher is recorded there as "Allowed" - explicit
permission from the rights holder to distribute freely - are fetched, and
the check is made per title against their API rather than assumed. Nothing
fetched here is committed to this repository; roms/ is gitignored.

  python3 tools/fetch_wos.py roms/spectrum
"""
import io
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
import zipfile

API = "https://worldofspectrum.org/infoseek/api/"
ARCHIVE = "https://worldofspectrum.org/archive/software/games/"

ALLOWED = {
    "Hewson Consultants Ltd", "Gremlin Graphics Software Ltd",
    "Gremlin Graphics Software Ltd [FR]", "Firebird Software Ltd",
    "Vortex Software", "Realtime Games Software Ltd",
    "Design Design Software", "Automata UK Ltd",
}

WANTED = [
    # Hewson, the whole set
    "Nebulus", "Uridium", "Cybernoid", "Exolon", "Zynaps", "Ranarama",
    "Quazatron", "Avalon", "Marauder", "Netherworld", "Stormlord",
    # Firebird
    "Elite", "Druid", "Chimera", "Thrust", "Bubble Bobble", "Booty",
    "Rommel's Revenge",
    # Vortex
    "Highway Encounter", "Alien Highway", "Android Two",
    # Realtime
    "3D Starstrike", "Starstrike II", "3D Tank Duel",
    # Design Design
    "Dark Star", "Halls of the Things",
    # Gremlin
    "Auf Wiedersehen Monty", "Jack the Nipper", "Bounder", "Avenger",
    "Deflektor",
    # Automata
    "Deus Ex Machina",
]


def get(url, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read() if binary else r.read().decode("utf-8", "replace")


def find_title(name):
    """Return (title, slug, publisher) for the first edition from a
    publisher that has given permission, or None."""
    url = API + "software?X-API-KEY=test&limit=8&" + urllib.parse.urlencode({"title": name})
    d = json.loads(get(url))
    for x in (d.get("titles") or []):
        pubs = [p["name"] for p in x.get("publishers", [])]
        allowed = [p for p in pubs if p in ALLOWED]
        if allowed and x.get("availability_text") == "Available":
            return x["title"], x["slug"], allowed[0]
    return None


def tap_links(slug):
    """The .tap images on a title's archive page, plainest name first."""
    html = get(ARCHIVE + slug)
    links = re.findall(r'href="([^"]+\.tap\.zip)"', html)
    out = []
    for l in links:
        if l.startswith("http"):
            # the page writes the host itself, and doubles the slash
            l = re.sub(r"(?<!:)//pub", "/pub", l)
        else:
            l = "https://worldofspectrum.org" + l
        out.append(l)
    return sorted(set(out), key=len)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "roms/spectrum"
    os.makedirs(outdir, exist_ok=True)
    got, missing = [], []

    for name in WANTED:
        try:
            found = find_title(name)
        except Exception as e:
            print("%-24s API error: %s" % (name, e)); continue
        if not found:
            missing.append((name, "no edition from a publisher that allows it"))
            continue
        title, slug, pub = found

        try:
            links = tap_links(slug)
        except Exception as e:
            missing.append((name, "archive page: %s" % e)); continue
        if not links:
            missing.append((name, "no .tap image, tape is .tzx only")); continue

        try:
            blob = get(links[0], binary=True)
            zf = zipfile.ZipFile(io.BytesIO(blob))
            member = next(n for n in zf.namelist() if n.lower().endswith(".tap"))
            data = zf.read(member)
        except Exception as e:
            missing.append((name, "download: %s" % e)); continue

        safe = re.sub(r"[^A-Za-z0-9 ()+-]", "", title).strip()
        path = os.path.join(outdir, safe + ".tap")
        open(path, "wb").write(data)
        got.append((title, pub, len(data)))
        print("%-30s %-32s %6d bytes" % (title, pub, len(data)))
        time.sleep(0.8)

    print("\n%d fetched, %d skipped" % (len(got), len(missing)))
    for n, why in missing:
        print("  skipped %-24s %s" % (n, why))
    print("\ntotal %d bytes" % sum(s for _, _, s in got))


main()
