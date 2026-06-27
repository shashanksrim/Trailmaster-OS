#!/usr/bin/env python3
"""
Regenerate the "sd_files" list in version.json from the contents of sd_files/.

Each file under sd_files/ becomes an entry the device downloads over OTA to its
SD card:  { "path": "/<relative path on SD>", "url": "<raw github url>" }

The device writes to /sd_card + path, so e.g. sd_files/speedometergif/rpm_idle.gif
-> SD path /speedometergif/rpm_idle.gif. Filenames with spaces are URL-encoded
in the url (raw.githubusercontent needs %20) but kept literal in the SD path.

Usage:
    python3 gen_sd_manifest.py            # include sd_files/ contents
    python3 gen_sd_manifest.py --clear    # set sd_files to [] (firmware-only release)
"""
import json, os, sys
from urllib.parse import quote

REPO = os.path.dirname(os.path.abspath(__file__))
SD_DIR = os.path.join(REPO, "sd_files")
VERSION_JSON = os.path.join(REPO, "version.json")
RAW_BASE = "https://raw.githubusercontent.com/shashanksrim/Trailmaster-OS/main/sd_files/"

def build_entries():
    entries = []
    for root, _dirs, files in os.walk(SD_DIR):
        for name in sorted(files):
            if name == ".DS_Store":
                continue
            full = os.path.join(root, name)
            rel = os.path.relpath(full, SD_DIR)        # e.g. speedometergif/rpm_idle.gif
            rel_posix = rel.replace(os.sep, "/")
            entries.append({
                "path": "/" + rel_posix,                # SD path (literal spaces)
                "url": RAW_BASE + quote(rel_posix),      # URL-encoded
            })
    entries.sort(key=lambda e: e["path"])
    return entries

def main():
    clear = "--clear" in sys.argv
    with open(VERSION_JSON) as f:
        data = json.load(f)
    data["sd_files"] = [] if clear else build_entries()
    with open(VERSION_JSON, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print(f"version.json: sd_files = {len(data['sd_files'])} file(s)")

if __name__ == "__main__":
    main()
