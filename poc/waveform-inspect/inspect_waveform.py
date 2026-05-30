#!/usr/bin/env python3
"""Mixxx Studio Bridge PoC — Stage 1: data-chain inspector (no build, no device).

Validates the SPEC §3.1/§3.2 decode chain end to end on a real Mixxx install:

    mixxxdb.sqlite  ->  pick an analyzed track
                    ->  read analysis/<track_analysis.id>
                    ->  strip Qt qCompress header (4-byte big-endian length)
                    ->  zlib-inflate
                    ->  scan the protobuf wire format and print the REAL field
                        tag numbers + sizes (the SPEC gives semantics but says
                        to verify the proto tags empirically — this does that).

Pure stdlib (sqlite3 + zlib). Run it against your Windows Mixxx data dir:

    python inspect_waveform.py "%APPDATA%\\Mixxx"
    # or let it try the default location
    python inspect_waveform.py

Output feeds Stage 2 (the C++ render+knob PoC): it tells us which protobuf tag
holds visual_sample_rate / audio_visual_ratio / signal_all / signal_filtered so
the C++ decoder uses confirmed numbers, not guesses.
"""

import os
import sys
import zlib
import sqlite3
import struct


# ---- protobuf wire-format scanner (just enough to map the schema) -----------

WIRE_TYPES = {0: "varint", 1: "i64", 2: "len", 5: "i32"}


def read_varint(buf, pos):
    shift = 0
    result = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return result, pos


def scan_protobuf(buf, depth=0, max_depth=2):
    """Yield (field_number, wire_type, summary) for top-level fields."""
    pos = 0
    n = len(buf)
    out = []
    while pos < n:
        try:
            key, pos = read_varint(buf, pos)
        except IndexError:
            break
        field_no = key >> 3
        wire = key & 0x07
        if wire == 0:  # varint
            val, pos = read_varint(buf, pos)
            out.append((field_no, "varint", str(val)))
        elif wire == 1:  # 64-bit (double / fixed64)
            raw = buf[pos:pos + 8]
            pos += 8
            dval = struct.unpack("<d", raw)[0] if len(raw) == 8 else None
            out.append((field_no, "i64/double", f"{dval!r}"))
        elif wire == 2:  # length-delimited (bytes / string / sub-message)
            ln, pos = read_varint(buf, pos)
            chunk = buf[pos:pos + ln]
            pos += ln
            summary = describe_len_field(chunk)
            out.append((field_no, "len", f"len={ln} {summary}"))
        elif wire == 5:  # 32-bit (float / fixed32)
            raw = buf[pos:pos + 4]
            pos += 4
            fval = struct.unpack("<f", raw)[0] if len(raw) == 4 else None
            out.append((field_no, "i32/float", f"{fval!r}"))
        else:
            out.append((field_no, f"wire{wire}?", "UNKNOWN — stopping"))
            break
    return out


def describe_len_field(chunk):
    """Heuristic: is a length-delimited field text, a packed byte array, or a
    sub-message? For the Waveform proto, signal_* are repeated int32 in 0-255."""
    if not chunk:
        return "(empty)"
    # printable ASCII -> likely a string (e.g. a version tag)
    if all(32 <= b < 127 for b in chunk[:32]) and len(chunk) < 64:
        try:
            return f'string? "{chunk.decode("ascii")}"'
        except UnicodeDecodeError:
            pass
    # all bytes 0-255 with a spread -> amplitude envelope (signal_*)
    lo, hi = min(chunk), max(chunk)
    return (f"bytes[{len(chunk)}] min={lo} max={hi} "
            f"first8={list(chunk[:8])}")


def summarize_submessage(chunk):
    """Parse a length-delimited chunk AS a sub-message and summarise its fields:
    for each tag, count occurrences and (for varints) value range. This reveals
    the Signal layout: is the sample array repeated-int32 at some field, plus
    any channels/units metadata fields?"""
    pos, n = 0, len(chunk)
    counts = {}        # (field_no, wire) -> count
    vrange = {}        # field_no -> [min,max] for varint fields
    submsgs = []       # (field_no, length) for nested length fields
    while pos < n:
        try:
            key, pos = read_varint(chunk, pos)
        except IndexError:
            break
        field_no, wire = key >> 3, key & 0x07
        counts[(field_no, wire)] = counts.get((field_no, wire), 0) + 1
        if wire == 0:
            val, pos = read_varint(chunk, pos)
            r = vrange.setdefault(field_no, [val, val])
            r[0], r[1] = min(r[0], val), max(r[1], val)
        elif wire == 1:
            pos += 8
        elif wire == 2:
            ln, pos = read_varint(chunk, pos)
            if len(submsgs) < 8:
                submsgs.append((field_no, ln))
            pos += ln
        elif wire == 5:
            pos += 4
        else:
            return f"(stopped: unknown wire {wire} at {pos})"
    lines = []
    for (fno, wire), cnt in sorted(counts.items()):
        wt = WIRE_TYPES.get(wire, f"wire{wire}")
        extra = ""
        if fno in vrange:
            extra = f" range={vrange[fno][0]}..{vrange[fno][1]}"
        lines.append(f"field #{fno} [{wt}] x{cnt}{extra}")
    out = "; ".join(lines)
    if submsgs:
        out += "  | nested len-fields: " + ", ".join(
            f"#{f}(len {l})" for f, l in submsgs)
    return out


# ---- Mixxx data access ------------------------------------------------------

def default_mixxx_dir():
    # NOTE: real installs put the data dir under %LOCALAPPDATA%\Mixxx
    # (C:\Users\<user>\AppData\Local\Mixxx), NOT %APPDATA%\Mixxx (Roaming) as the
    # SPEC §3.1/§11.1/§12 currently assume. Check Local first, then Roaming.
    for env in ("LOCALAPPDATA", "APPDATA"):
        base = os.environ.get(env)
        if base and os.path.isdir(os.path.join(base, "Mixxx")):
            return os.path.join(base, "Mixxx")
    # macOS / Linux fallbacks (handy for inspecting a copied dir)
    home = os.path.expanduser("~")
    for cand in (os.path.join(home, "Library", "Containers",
                              "org.mixxx.mixxx", "Data", "Library",
                              "Application Support", "Mixxx"),
                 os.path.join(home, ".mixxx")):
        if os.path.isdir(cand):
            return cand
    # last-resort guess
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or home
    return os.path.join(base, "Mixxx")


def main():
    mixxx_dir = sys.argv[1] if len(sys.argv) > 1 else default_mixxx_dir()
    db_path = os.path.join(mixxx_dir, "mixxxdb.sqlite")
    analysis_dir = os.path.join(mixxx_dir, "analysis")

    print(f"Mixxx dir:     {mixxx_dir}")
    print(f"Database:      {db_path}")
    print(f"Analysis dir:  {analysis_dir}")
    print()

    if not os.path.isfile(db_path):
        print("ERROR: mixxxdb.sqlite not found. Pass the Mixxx data dir as arg 1.")
        return 2

    # Read-only, even while Mixxx is running (WAL). immutable=1 avoids lock waits.
    uri = f"file:{db_path}?mode=ro&immutable=1"
    con = sqlite3.connect(uri, uri=True)
    con.row_factory = sqlite3.Row

    # SPEC §3.2 lookup: prefer the detailed (type=1), newest-format waveform.
    rows = con.execute(
        """
        SELECT ta.id   AS analysis_id,
               ta.track_id,
               ta.type,
               ta.version,
               l.artist,
               l.title,
               tl.location
        FROM track_analysis ta
        JOIN library l          ON l.id = ta.track_id
        JOIN track_locations tl ON tl.id = l.location
        WHERE ta.type = 1
        ORDER BY (ta.version = 'Waveform-6.1') DESC, ta.id DESC
        LIMIT 5
        """
    ).fetchall()

    if not rows:
        print("No detailed (type=1) waveform analyses found.")
        print("Load & play a track in Mixxx once so it gets analyzed, then retry.")
        return 1

    print(f"Found {len(rows)} analyzed track(s). Inspecting the first:\n")
    for r in rows:
        print(f"  analysis_id={r['analysis_id']:<6} version={r['version']:<14} "
              f"{r['artist']} - {r['title']}")
    print()

    target = rows[0]
    blob_path = os.path.join(analysis_dir, str(target["analysis_id"]))
    print(f"=== Decoding analysis blob: {blob_path} ===")
    print(f"    track: {target['artist']} - {target['title']}")
    print(f"    version: {target['version']}")
    if not os.path.isfile(blob_path):
        print(f"ERROR: blob file missing at {blob_path}")
        return 1

    raw = open(blob_path, "rb").read()
    print(f"    raw file size: {len(raw)} bytes")

    # SPEC §3.2: 4-byte big-endian uncompressed length, then zlib payload.
    declared_len = struct.unpack(">I", raw[:4])[0]
    print(f"    qCompress declared uncompressed length: {declared_len}")
    try:
        payload = zlib.decompress(raw[4:])
    except zlib.error as e:
        print(f"ERROR: zlib.decompress failed: {e}")
        return 1
    print(f"    inflated payload: {len(payload)} bytes "
          f"({'MATCHES' if len(payload) == declared_len else 'MISMATCH!'} declared)")
    print()

    print("=== Protobuf top-level fields (the REAL schema/tag numbers) ===")
    fields = scan_protobuf(payload)
    for field_no, wire, summary in fields:
        print(f"    field #{field_no:<2} [{wire:<10}] {summary}")

    # Recurse one level into the length-delimited fields (signal_all /
    # signal_filtered) so the C++ extractor knows their internal layout.
    print()
    print("=== Sub-message layout of length-delimited fields ===")
    pos, n = 0, len(payload)
    while pos < n:
        key, pos = read_varint(payload, pos)
        field_no, wire = key >> 3, key & 0x07
        if wire == 0:
            _, pos = read_varint(payload, pos)
        elif wire == 1:
            pos += 8
        elif wire == 5:
            pos += 4
        elif wire == 2:
            ln, pos = read_varint(payload, pos)
            chunk = payload[pos:pos + ln]
            pos += ln
            print(f"    field #{field_no} (len {ln}):")
            print(f"        {summarize_submessage(chunk)}")
        else:
            break

    print()
    print("=== Interpretation hint (map against SPEC §3.2) ===")
    print("    Expected Waveform fields: visual_sample_rate (double),")
    print("    audio_visual_ratio (double), signal_all (bytes/repeated int32),")
    print("    signal_filtered.{low,mid,high} (bytes), optional signal_stems[].")
    print("    Use the field #s above for the Stage-2 C++ decoder.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
