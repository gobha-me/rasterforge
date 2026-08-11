#!/usr/bin/env python3
"""Generate RasterForge's checked-in decode and fit fuzz corpora."""

from __future__ import annotations

import argparse
import base64
import re
import struct
import sys
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
FIXTURES = REPOSITORY / "test" / "26png-decode" / "fixtures.hpp"
JPEG_FIXTURES = REPOSITORY / "test" / "34jpeg-decode" / "fixtures.hpp"
CORPUS_ROOT = Path(__file__).resolve().parent / "corpus"
DECODE_CORPUS = CORPUS_ROOT / "decode"
FIT_CORPUS = CORPUS_ROOT / "fit"
ARRAY = re.compile(
    r"constexpr\s+std::array<std::uint8_t,\s*\d+>\s+(\w+)\s*\{\{(.*?)\}\};",
    re.DOTALL,
)
BASE64_FIXTURE = re.compile(
    r"inline\s+constexpr\s+std::string_view\s+(\w+)_base64\s*\{(.*?)\};",
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


def load_jpeg_fixtures() -> dict[str, bytes]:
    source = JPEG_FIXTURES.read_text(encoding="utf-8")
    fixtures: dict[str, bytes] = {}
    for name, initializer in BASE64_FIXTURE.findall(source):
        encoded = "".join(re.findall(r'"([^"]*)"', initializer))
        fixtures[name] = base64.b64decode(encoded, validate=True)
    required = {"rgb444", "progressive", "cmyk"}
    missing = required - fixtures.keys()
    if missing:
        raise RuntimeError(
            f"missing JPEG fixture strings: {', '.join(sorted(missing))}"
        )
    return fixtures


def expected_decode_corpus() -> dict[str, bytes]:
    fixtures = load_fixtures()
    jpeg_fixtures = load_jpeg_fixtures()
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

    jpeg = jpeg_fixtures["rgb444"]
    jpeg_corrupt = bytearray(jpeg)
    scan = jpeg.index(b"\xFF\xDA")
    scan_header_length = struct.unpack_from(">H", jpeg, scan + 2)[0]
    entropy = scan + 2 + scan_header_length
    jpeg_corrupt[entropy : entropy + 2] = b"\xFF\xC0"
    seeds.update(
        {
            "jpeg-signature-prefix": b"\xFF",
            "jpeg-signature-complete": b"\xFF\xD8",
            "jpeg-truncated-header": jpeg[:20],
            "jpeg-truncated-scan": jpeg[:-2],
            "jpeg-corrupt-entropy": bytes(jpeg_corrupt),
            "jpeg-baseline.jpg": jpeg,
            "jpeg-progressive.jpg": jpeg_fixtures["progressive"],
            "jpeg-unsupported-cmyk.jpg": jpeg_fixtures["cmyk"],
        }
    )

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


def expected_fit_corpus() -> dict[str, bytes]:
    # fit_fuzzer.cpp reads thirteen control bytes before optional RGBA samples:
    # source/destination extents, policy, filter, two focal selectors, matte,
    # and temporary-budget selector. These small seeds put every behavior class
    # on the initial coverage frontier; libFuzzer supplies arbitrary pixel tails.
    return {
        "contain-nearest": bytes([4, 2, 5, 5, 0, 0, 2, 2, 9, 8, 7, 0, 3]),
        "cover-triangle": bytes([5, 2, 2, 4, 1, 1, 3, 1, 6, 5, 4, 0, 3]),
        "stretch-nearest": bytes([2, 5, 5, 2, 2, 0, 2, 2, 3, 2, 1, 0, 3]),
        "none-triangle": bytes([5, 2, 3, 4, 3, 1, 1, 3, 12, 11, 10, 0, 3]),
        "zero-source": bytes([0, 2, 3, 3, 0, 0, 2, 2, 0, 0, 0, 0, 3]),
        "zero-destination": bytes([2, 2, 0, 3, 1, 1, 2, 2, 0, 0, 0, 0, 3]),
        "nonfinite-focus": bytes([2, 2, 3, 3, 3, 0, 5, 6, 0, 0, 0, 0, 3]),
        "clamped-focus": bytes([5, 2, 3, 4, 1, 1, 0, 4, 0, 0, 0, 0, 3]),
        "tight-temporary": bytes([4, 4, 7, 7, 2, 1, 2, 2, 0, 0, 0, 0, 0]),
        "destination-limit": bytes([2, 2, 24, 24, 2, 0, 2, 2, 0, 0, 0, 0, 3]),
        "invalid-selectors": bytes([2, 2, 3, 3, 4, 2, 2, 2, 0, 0, 0, 0, 3]),
        "arbitrary-pixels": bytes(
            [2, 2, 3, 3, 0, 1, 2, 2, 90, 80, 70, 60, 3]
            + list(range(16))
        ),
    }


def check_corpus(corpus: Path, seeds: dict[str, bytes]) -> list[str]:
    actual_names = (
        {path.name for path in corpus.iterdir()} if corpus.exists() else set()
    )
    expected_names = set(seeds)
    errors: list[str] = []
    for missing in sorted(expected_names - actual_names):
        errors.append(f"missing {missing}")
    for extra in sorted(actual_names - expected_names):
        errors.append(f"unexpected {extra}")
    for name in sorted(expected_names & actual_names):
        if (corpus / name).read_bytes() != seeds[name]:
            errors.append(f"out of date {name}")
    return errors


def check(corpora: dict[str, tuple[Path, dict[str, bytes]]]) -> int:
    failed = False
    for label, (corpus, seeds) in corpora.items():
        errors = check_corpus(corpus, seeds)
        if not errors:
            continue
        failed = True
        print(f"{label} corpus does not match its generator:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
    return int(failed)


def write_corpus(corpus: Path, seeds: dict[str, bytes]) -> None:
    corpus.mkdir(parents=True, exist_ok=True)
    for path in corpus.iterdir():
        if path.is_file() and path.name not in seeds:
            path.unlink()
    for name, contents in seeds.items():
        path = corpus / name
        if not path.exists() or path.read_bytes() != contents:
            path.write_bytes(contents)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check", action="store_true", help="verify without changing the corpus"
    )
    arguments = parser.parse_args()
    corpora = {
        "decode": (DECODE_CORPUS, expected_decode_corpus()),
        "fit": (FIT_CORPUS, expected_fit_corpus()),
    }
    if arguments.check:
        return check(corpora)
    for corpus, seeds in corpora.values():
        write_corpus(corpus, seeds)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
