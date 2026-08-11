#!/usr/bin/env python3
"""Generate the checked-in decode corpus from the unit-test PNG fixtures."""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
FIXTURES = REPOSITORY / "test" / "26png-decode" / "fixtures.hpp"
CORPUS = Path(__file__).resolve().parent / "corpus" / "decode"
ARRAY = re.compile(
    r"constexpr\s+std::array<std::uint8_t,\s*\d+>\s+(\w+)\s*\{\{(.*?)\}\};",
    re.DOTALL,
)


def load_fixtures() -> dict[str, bytes]:
    source = FIXTURES.read_text(encoding="utf-8")
    fixtures: dict[str, bytes] = {}
    for name, initializer in ARRAY.findall(source):
        fixtures[name] = bytes(
            int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", initializer)
        )
    required = {
        "rgb_png",
        "rgba_png",
        "gray1_png",
        "gray_alpha_png",
        "palette_png",
        "gray16_png",
        "interlaced_png",
        "unknown_critical_png",
    }
    missing = required - fixtures.keys()
    if missing:
        raise RuntimeError(f"missing fixture arrays: {', '.join(sorted(missing))}")
    return fixtures


def expected_corpus() -> dict[str, bytes]:
    fixtures = load_fixtures()
    signature = fixtures["rgb_png"][:8]
    rgb = fixtures["rgb_png"]

    seeds: dict[str, bytes] = {"empty": b""}
    for size in range(1, len(signature)):
        seeds[f"signature-prefix-{size:02d}"] = signature[:size]
        mismatch = bytearray(signature[:size])
        mismatch[-1] ^= 0xFF
        seeds[f"signature-mismatch-{size:02d}"] = bytes(mismatch)

    seeds.update(
        {
            "signature-complete": signature,
            "signature-with-trailing": signature + (b"\xA5" * 64),
            "signature-unknown": b"\xA5" * len(signature),
            "input-limit-precedence": bytes([signature[0] ^ 0xFF]) + signature[1:],
            "png-truncated-ihdr": rgb[:20],
            "png-truncated-idat": rgb[:40],
            "png-truncated-iend": rgb[:-1],
        }
    )

    corrupt = bytearray(rgb)
    corrupt[46] ^= 0x80
    seeds["png-corrupt-idat"] = bytes(corrupt)

    zero_width = bytearray(rgb)
    struct.pack_into(">I", zero_width, 16, 0)
    seeds["png-zero-width-header"] = bytes(zero_width[:24])

    overflowing = bytearray(rgb)
    struct.pack_into(">I", overflowing, 16, 0xFFFFFFFF)
    struct.pack_into(">I", overflowing, 20, 0xFFFFFFFF)
    seeds["png-overflowing-header"] = bytes(overflowing[:24])

    for name in (
        "rgb_png",
        "rgba_png",
        "gray1_png",
        "gray_alpha_png",
        "palette_png",
        "gray16_png",
        "interlaced_png",
        "unknown_critical_png",
    ):
        seeds[name.removesuffix("_png").replace("_", "-") + ".png"] = fixtures[name]
    return seeds


def check(seeds: dict[str, bytes]) -> int:
    actual_names = (
        {path.name for path in CORPUS.iterdir()} if CORPUS.exists() else set()
    )
    expected_names = set(seeds)
    errors: list[str] = []
    for missing in sorted(expected_names - actual_names):
        errors.append(f"missing {missing}")
    for extra in sorted(actual_names - expected_names):
        errors.append(f"unexpected {extra}")
    for name in sorted(expected_names & actual_names):
        if (CORPUS / name).read_bytes() != seeds[name]:
            errors.append(f"out of date {name}")
    if errors:
        print("decode corpus does not match its generator:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    return 0


def write(seeds: dict[str, bytes]) -> None:
    CORPUS.mkdir(parents=True, exist_ok=True)
    for path in CORPUS.iterdir():
        if path.is_file() and path.name not in seeds:
            path.unlink()
    for name, contents in seeds.items():
        path = CORPUS / name
        if not path.exists() or path.read_bytes() != contents:
            path.write_bytes(contents)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check", action="store_true", help="verify without changing the corpus"
    )
    arguments = parser.parse_args()
    seeds = expected_corpus()
    if arguments.check:
        return check(seeds)
    write(seeds)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
