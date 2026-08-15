# ADR 0010: EXIF orientation is normalized through bounded generic metadata

- Status: accepted
- Date: 2026-08-15
- Issue: [RF-04c](https://github.com/gobha-me/rasterforge/issues/18)

## Context

PNG and JPEG can carry TIFF orientation metadata, but their codec APIs expose
it differently. RasterForge needs one public representation, exact behavior
for every rotation and reflection, and resource checks before an oriented
image is allocated. Optional metadata must not introduce an unbounded retained
blob or leak codec constants through the public API.

## Decision

`Orientation` uses the eight EXIF values with codec-neutral names.
`DecodedImage::source_orientation()` returns a value only for one valid,
unambiguous orientation tag. `OrientationStatus` distinguishes missing,
applied, caller-ignored, and invalid-ignored metadata. A present identity value
is reported as applied even though it does not move pixels.

Malformed optional EXIF does not reject otherwise decodable image pixels.
Invalid byte order, TIFF magic, offsets, entry layout, tag type/count/value,
and duplicate or conflicting orientation records produce `invalid_ignored`,
no source orientation, and unchanged pixels. Corrupt JPEG marker framing or PNG
chunk framing remains malformed image data and follows the codec error path.

The shared TIFF parser is allocation-free, supports both byte orders, and reads
only IFD0 orientation tag `0x0112`. JPEG installs a per-decode APP1 processor
that examines the marker in the in-memory source and skips it without retaining
the payload. PNG obtains the bounded `eXIf` payload from libpng. Libpng 1.6.31
or newer is therefore required; the pinned fallback remains 1.6.58.

Applying values 2 through 8 allocates a checked destination and copies exact
RGBA pixels using destination-to-source coordinates. Values 5 through 8 swap
the output axes. The decoded codec-native image counts against
`max_temporary_bytes` while it coexists with the final normalized image;
identity, absent, invalid, and caller-ignored cases allocate no second image.
Both native and normalized layouts are validated before codec pixel storage is
allocated. Transform output is deterministic and preserves all four bytes,
including colored transparent pixels.

## Consequences

Callers can distinguish source metadata from output layout without handling
JPEG or PNG constants. Metadata damage remains observable without discarding
safe pixels. Large oriented decodes may require callers to raise the temporary
budget as well as the output budget because normalization deliberately avoids
in-place permutation complexity.

Each decode owns its codec and metadata state. No shared mutable state, codec
handle, filesystem access, diagnostics, cancellation claim, or color-profile
behavior is introduced.
