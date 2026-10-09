#!/usr/bin/env python3
"""Report Folio metadata content and FAT cluster budgets; this is not measured free space."""
import argparse
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]


def define(path, name):
    match = re.search(r"^#define\s+" + re.escape(name) + r"\s+(\d+)\s*$", (root / path).read_text(), re.M)
    if not match:
        raise SystemExit(f"Review changed source constant: {path}:{name}")
    return int(match.group(1))


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--edited-books", type=int, default=128)
parser.add_argument("--baseline-books", type=int, default=0)
parser.add_argument("--legacy-books", type=int, default=128)
parser.add_argument("--cluster", type=int, default=4096)
args = parser.parse_args()
limit = define("firmware/main/folio/format_store.c", "FORMAT_BOOK_LIMIT")
if min(args.edited_books, args.baseline_books, args.legacy_books) < 0 or args.edited_books + args.baseline_books > limit or args.legacy_books > 128:
    parser.error("Book counts must be nonnegative and within the current engineering limits")
if args.cluster < 512 or args.cluster > 65536 or args.cluster & (args.cluster - 1):
    parser.error("Cluster size must be a power of two from 512 to 65536")
header = define("firmware/main/folio/checkpoint_store.c", "HEADER_LEN")
wire = define("firmware/main/folio/format_store.c", "FORMAT_WIRE_SIZE")
resume = define("firmware/main/folio/format_store.c", "RESUME_WIRE_SIZE")
owner = define("firmware/main/folio/format_transaction.c", "OWNER_BYTES")
legacy = define("firmware/main/folio/reader_draft.c", "DRAFT_BYTES")
books = define("firmware/main/folio/reading_service.h", "FOLIO_READING_MAX_BOOKS")
days = define("firmware/main/folio/reading_service.h", "FOLIO_READING_MAX_DAYS")
path = define("firmware/main/folio/reading_service.h", "FOLIO_READING_PATH_MAX")
journal = 3 * wire + 64
stats = 40 + books * (path + 128 + 8) + days * 14
rows = []


def add(name, files, payload):
    file_bytes = payload + header
    rows.append({"kind": name, "files": files, "bytes_per_file": file_bytes,
                 "content_bytes": files * file_bytes,
                 "cluster_bytes": files * ((file_bytes + args.cluster - 1) // args.cluster) * args.cluster})


add("M2 edited book records", 2 * args.edited_books, wire)
add("M2 edited book journals", 2 * args.edited_books, journal)
add("M2 initial baseline (first slot only)", args.baseline_books, wire)
add("DEFAULTS records", 2, wire)
add("DEFAULTS journal", 2, journal)
add("M2 resume source", 2, resume)
add("M2 pending owner", 2, owner)
add("Retained M1 book drafts", 2 * args.legacy_books, legacy)
add("Retained M1 resume source", 2, path + 24)
add("Reading statistics at both limits", 2, stats)
content = sum(row["content_bytes"] for row in rows)
allocated = sum(row["cluster_bytes"] for row in rows)
print(json.dumps({"cluster_size_assumption": args.cluster, "rows": rows,
                  "content_bytes": content, "cluster_bytes": allocated,
                  "raw_partition_bytes": 5 * 1024 * 1024,
                  "raw_remainder_before_other_costs": 5 * 1024 * 1024 - allocated,
                  "excludes": ["FAT tables/directories", "wear levelling", "books", "extra in-flight file growth", "NVS"],
                  "not_a_free_space_guarantee": True}, indent=2))
