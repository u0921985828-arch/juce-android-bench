#!/usr/bin/env python3
"""ZATI expo bench — runs the self-measuring build across the matrix and
judges every dump against the rules a stand at a music-tech show gets judged on:
a finger fits, nothing overlaps, no caption is clipped, in any language, on any
phone anyone will bring to the stand."""
import subprocess, os, json, sys, itertools, collections, re

BIN = "/home/user/FX-404/build/Zati_artefacts/Release/Zati"

# Real devices, not round numbers. dp at the density the app actually lays out in.
SIZES = [
    ("360x640",  "small 16:9 (Galaxy A-series, Redmi 9A)"),
    ("393x851",  "Pixel 8 / modern 19.5:9"),
    ("412x915",  "Pixel 8 Pro / big phone"),
    ("344x882",  "narrow tall (Z Flip cover-open, Xperia)"),
    ("280x653",  "Galaxy Fold FRONT screen — the worst case anyone ships"),
    ("800x1280", "tablet portrait"),
    ("915x412",  "LANDSCAPE — the orientation nobody tests"),
]
LANGS = ["es", "en", "zh", "ar"]
SHEETS = ["", "pads", "sec", "paso", "song", "mix", "xy", "set", "proj", "gest", "rack", "chop", "browse"]

MIN_TOUCH = 40   # Metrics::hit — Android's own guideline is 48dp, this is the floor

def run(size, lang, sheet):
    env = dict(os.environ, ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG=lang,
               ZATI_OPEN=sheet, DISPLAY=":99")
    try:
        out = subprocess.run([BIN], env=env, capture_output=True, timeout=60).stdout.decode("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None
    rows = []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try: rows.append(json.loads(line))
            except Exception: pass
    return rows or None

def judge(rows, size, lang, sheet):
    findings = []
    head = rows[0] if rows and rows[0].get("root") else {}
    W, H = head.get("w", 0), head.get("h", 0)
    comps = [r for r in rows if "path" in r]

    for r in comps:
        tag = f"{size}/{lang}/{sheet or 'face'}"
        # 1. A finger has to fit.
        if r.get("hit") and r.get("on"):
            if r["w"] < MIN_TOUCH or r["h"] < MIN_TOUCH:
                findings.append(("TOUCH", tag, f'{r.get("text","?")} {r["w"]}x{r["h"]}', min(r["w"], r["h"])))
        # 2. Nothing may be laid out off the window.
        if r["w"] > 0 and r["h"] > 0 and not r.get("scrolled"):
            if r["x"] < -1 or r["y"] < -1 or r["x"] + r["w"] > W + 1 or r["y"] + r["h"] > H + 1:
                findings.append(("OFFSCREEN", tag, f'{r.get("text",r["path"].split("/")[-1])} @{r["x"]},{r["y"]} {r["w"]}x{r["h"]}', 0))
        # 3. Type that has to be squeezed to fit.
        #    drawFittedText squeezes to 0.9 horizontal scale and may wrap to a
        #    second line before it gives up, so this is not "invisible text" —
        #    it is a cap whose lettering no longer matches the cap beside it,
        #    which is precisely what reads as amateur on a stand.
        if "needW" in r and r["haveW"] > 0:
            over = r["needW"] - r["haveW"]
            if over > 0.5:
                kind = "TRUNC" if r["needW"] > r["haveW"] / 0.9 else "SQUEEZE"
                findings.append((kind, tag, f'"{r.get("text","")}" needs {r["needW"]:.0f} has {r["haveW"]:.0f}', -over))

    # 4. Interactive siblings must not overlap. Only siblings: a sheet sits
    #    over the face by design, and comparing across layers reports the
    #    design as a bug a thousand times over.
    hits = [r for r in comps if r.get("hit") and r["w"] > 0 and r["h"] > 0]
    par = lambda r: r["path"].rsplit("/", 1)[0]
    for a, b in itertools.combinations(hits, 2):
        if par(a) != par(b): continue
        ox = min(a["x"]+a["w"], b["x"]+b["w"]) - max(a["x"], b["x"])
        oy = min(a["y"]+a["h"], b["y"]+b["h"]) - max(a["y"], b["y"])
        if ox > 1 and oy > 1:
            findings.append(("OVERLAP", f"{size}/{lang}/{sheet or 'face'}",
                             f'{a.get("text",a["path"].split("/")[-1])} x {b.get("text",b["path"].split("/")[-1])} by {ox}x{oy}', 0))
    return findings


# ---------------------------------------------------------------------------
#  5. UNTRANSLATED TEXT.
#
#  Two readouts printed the Spanish word in all four languages ("recto", the
#  bar count) and the three tabs of the SETTINGS card - the card that CONTAINS
#  the language selector - were never retranslated at all. None of it was
#  caught, because every rule above judges GEOMETRY: a Spanish caption in an
#  English build fits its cap perfectly.
#
#  The test is comparative and needs no dictionary: lay out the same component
#  in Spanish and in English and compare the string at the same PATH. If it did
#  not change, either it went through T() and the two languages agree - which
#  is legitimate and lives in OK below - or it never went through T() at all.
#  A row that is genuinely identical is a one-line entry here; a row that is
#  not is a bug, and it is one line of output instead of nobody noticing.
#
#  PUNTO CIEGO CONOCIDO: esto compara el TEXTO DE LOS COMPONENTES, porque es lo
#  que UiAudit vuelca. El texto pintado a mano en un paint() no tiene componente
#  y por lo tanto no se mide. Asi sobrevivieron los dieciocho nombres de
#  parametro de los seis efectos - CUTOFF, RESO, FREQ, DRIVE, TIME, FBK, BITS,
#  RATE, SIZE, DAMP - en espanol y en las cuatro compilaciones, en la cara de la
#  maquina. Se encontraron leyendo fxDefs, no corriendo esto. Mientras el
#  volcado no lleve tambien lo que se pinta, esta prueba cubre los rotulos de
#  los controles y no los de la pintura.
UNTRANSLATED_OK = {
    # A number, a unit, a symbol, a path, a file name.
    # (handled by the regex below)
    "ZATI",                                    # the wordmark
    "L", "R", "C", "M", "S", "A", "B", "D",    # channel, pan and bank letters
    "ISO", "HPF", "DRV", "DLY", "BIT", "REV",  # effect abbreviations
    "XY",                                      # los dos ejes se llaman igual en todas partes
    "PADS", "SEC", "MIX", "SET", "SONG", "REC", "PLAY", "STOP", "LOAD",
    "RACK", "TEST", "AUDIO", "AUTOCUT", "AUTO CHOP", "SWING", "off",
    "PAPEL", "GRAFITO", "ACERO", "LACA",       # the four chassis, named not translated
    "ESPANOL", "ENGLISH",                      # each language names itself
    "file:",
    "\u4e2d\u6587", "\u0627\u0644\u0639\u0631\u0628\u064a\u0629",   # each language names itself, in itself
}
#  A number with a unit welded to it - "0 st", "120 bpm", "2 ms", "0 c" - is
#  the same string in every language and always will be.
UNTRANSLATED_UNIT = re.compile(r'^[+\-]?[0-9][0-9.,]*\s*(st|c|ms|s|bpm|dB|Hz|kHz|%|x)?$', re.I)
UNTRANSLATED_SAFE = re.compile(r'^[\s0-9%.,:;+\-/|×xX\u00b7\u00b0"\'()\[\]_@#]*$')

def judge_lang(rows_es, rows_en, size, sheet):
    if not rows_es or not rows_en: return []
    def m(rows):
        return {r["path"]: r["text"] for r in rows if r.get("path") and r.get("text")}
    es, en = m(rows_es), m(rows_en)
    out = []
    for path, t in es.items():
        if t in UNTRANSLATED_OK or UNTRANSLATED_SAFE.match(t): continue
        if UNTRANSLATED_UNIT.match(t): continue
        if "/" in t or t.startswith("P") and t[1:].isdigit(): continue
        if en.get(path) == t:
            out.append(("UNTRANSLATED", f"{size}/{sheet or 'face'}", f'"{t}" identical in es and en', 0))
    return out

def main():
    only = sys.argv[1:]
    allf = []
    pairs = collections.defaultdict(dict)
    runs = fails = 0
    for size, _ in SIZES:
        for lang in LANGS:
            for sheet in SHEETS:
                if only and not any(o in f"{size}{lang}{sheet}" for o in only): continue
                rows = run(size, lang, sheet)
                runs += 1
                if rows is None:
                    fails += 1
                    allf.append(("CRASH", f"{size}/{lang}/{sheet or 'face'}", "no dump — crash or hang", 0))
                    continue
                allf += judge(rows, size, lang, sheet)
                if lang in ("es", "en"): pairs[(size, sheet)][lang] = rows
    for (size, sheet), d in pairs.items():
        allf += judge_lang(d.get("es"), d.get("en"), size, sheet)
    by = collections.Counter(f[0] for f in allf)
    print(f"\n=== {runs} runs, {fails} produced nothing ===")
    print("findings:", dict(by))
    # Group identical messages across the matrix so one bug is one line.
    grouped = collections.defaultdict(list)
    for kind, tag, msg, sev in allf: grouped[(kind, msg)].append(tag)
    for (kind, msg), tags in sorted(grouped.items(), key=lambda kv: (kv[0][0], -len(kv[1]))):
        print(f"{kind:9} x{len(tags):<3} {msg}   [{tags[0]}{' +'+str(len(tags)-1) if len(tags)>1 else ''}]")

main()
