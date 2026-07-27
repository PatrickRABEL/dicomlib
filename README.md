# dicomlib

`dicomlib` is a C++ DICOM library derived from UCDMC99 and earlier work from
the University of California, Davis, and Karl Franzens University, Graz.

## License

The project uses the historical permissive license included in
[`dicomlib/License.txt`](dicomlib/License.txt).

The license grants free access to use, copy, and prepare derivative works from
the software. Source distributions and derivative source distributions must keep
the copyright notice. The software is provided as-is, without warranty.

This is not currently expressed as a standard SPDX license identifier in the
repository.

New contributions made by Patrick RABEL are licensed under the BSD 2-Clause
License, subject to the existing license terms that apply to the original code.
See [`NOTICE.md`](NOTICE.md).

## Current Scope

The maintained build targets are:

- Linux on x86, x86_64, ARM 32-bit, and ARM 64-bit
- macOS on x86_64 and ARM 64-bit

The library is built with CMake and C++17:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Migration to the Current DICOM Standard

The DICOM data dictionary and UID registry were regenerated from the official
DICOM 2026c DocBook XML published by NEMA.

Generated files:

- `dicomlib/DataDictionary.cpp`
- `dicomlib/UIDs.cpp`

Generation tool:

- `tools/generate_dicom_standard_tables.py`

The generated tables include modern DICOM value representations and UID entries
from the current standard source. The generated files should not be edited by
hand; regenerate them from the official XML source instead.

## Code Simplification

The codebase was simplified for the maintained C++ library build:

- Boost usage was removed from the core library.
- C++17 standard library facilities replaced Boost equivalents where needed.
- The old Boost.Python binding and legacy SCons demo build files were removed.
- The build system was consolidated around CMake.
- POSIX threads are used through CMake's `Threads::Threads` target.

## Transfer Syntax Policy

Transfer syntax support is configured through CMake and exposed through the
generated `dicomlib/Config.hpp` header.

Default association support:

- Implicit VR Little Endian
- Explicit VR Little Endian
- Explicit VR Big Endian

Optional configuration:

- `DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN`: enabled by default.
- `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH`: disabled by default; when enabled,
  encapsulated Pixel Data fragments can be accepted without pixel decoding.
- Encapsulated Uncompressed Explicit VR Little Endian
  `1.2.840.10008.1.2.1.98` is always enabled; Pixel Data is encoded and decoded
  as one uncompressed OB fragment per frame.
- `DICOMLIB_WITH_ZLIB`: disabled by default; when enabled, Deflated Explicit VR
  Little Endian is encoded and decoded through zlib raw DEFLATE, and Deflated
  Image Frame Compression `1.2.840.10008.1.2.8.1` Pixel Data is encoded and
  decoded as one RFC1951 deflated fragment per frame. It also enables the
  deflated JPIP Referenced Transfer Syntaxes `1.2.840.10008.1.2.4.95` and
  `1.2.840.10008.1.2.4.205`.
- JPIP Referenced Transfer Syntaxes `1.2.840.10008.1.2.4.94` and
  `1.2.840.10008.1.2.4.204` are always enabled for Data Set encode/decode and
  validation. The library validates `Pixel Data Provider URL` `(0028,7FE0)`,
  rejects top-level `Pixel Data`, `Float Pixel Data`, and
  `Double Float Pixel Data`, and enforces the allowed Photometric
  Interpretation values. It does not implement JPIP network retrieval or local
  decoding of the referenced pixel stream. These Transfer Syntaxes are not
  used for PS3.10 media storage.
- `DICOMLIB_WITH_RLE`: disabled by default; when enabled, RLE Lossless Pixel
  Data is encoded and decoded by the built-in DICOM RLE codec.
- `DICOMLIB_WITH_JPEG`: disabled by default; when enabled, JPEG Baseline 8-bit
  lossy Pixel Data is encoded and decoded through libjpeg/libjpeg-turbo, and
  lossy compression attributes are written during encode.
- `DICOMLIB_WITH_GDCM`: disabled by default; when enabled, JPEG Extended
  Process 2 & 4 Pixel Data for `1.2.840.10008.1.2.4.51`, JPEG Lossless
  Process 14 Pixel Data for `1.2.840.10008.1.2.4.57`, JPEG Lossless Process
  14 Selection Value 1 Pixel Data for `1.2.840.10008.1.2.4.70`, and the
  retired JPEG Process UIDs `1.2.840.10008.1.2.4.52`, `.53`, and `.55` are
  decoded through GDCM. Already-fragmented Pixel Data for those Transfer
  Syntaxes can be written without native recompression.
- Retired JPEG Process UIDs `.54`, `.56`, and `.58` through `.66` have no local
  decode/recompression path. Already-fragmented `OB` Pixel Data can be passed
  through only when `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH` is enabled.
- `DICOMLIB_WITH_JPEG2000`: disabled by default; when enabled, JPEG 2000 Part 1
  Pixel Data for `1.2.840.10008.1.2.4.90` and `.91` is encoded and decoded in
  lossless mode through OpenJPEG.
- JPEG 2000 Part 2 Multi-component Transfer Syntaxes `.92` and `.93` have no
  local decode/recompression path. Already-fragmented `OB` Pixel Data can be
  passed through only when `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH` is
  enabled.
- `DICOMLIB_WITH_JPEGLS`: disabled by default; when enabled, JPEG-LS Lossless
  Pixel Data for `1.2.840.10008.1.2.4.80` is encoded and decoded through
  CharLS, and JPEG-LS Near-Lossless Pixel Data for `.81` is encoded and decoded
  through CharLS.
- `DICOMLIB_JPEGLS_NEAR_LOSSLESS`: defaults to `1`; sets the JPEG-LS `NEAR`
  value used when encoding `1.2.840.10008.1.2.4.81`.
- `DICOMLIB_PREPARE_EXTERNAL_CODECS`: disabled by default; when enabled, CMake
  requires the external libraries needed for future pixel-compressed transfer
  syntax support.

External dependency mapping:

- Deflated Explicit VR Little Endian and Deflated Image Frame Compression: zlib
- JPIP Referenced Deflate: zlib
- Encapsulated Uncompressed Explicit VR Little Endian: no external library
  required
- RLE Lossless: no external library required
- JPEG Baseline: libjpeg or libjpeg-turbo through the standard CMake `JPEG`
  package
- JPEG Extended Process 2 & 4, retired JPEG Process UIDs `.52`, `.53`, and
  `.55`, JPEG Lossless Process 14, and Process 14 Selection Value 1: GDCM
  through CMake `find_package(GDCM)`
- JPEG-LS Lossless and Near-Lossless: CharLS through `pkg-config` module
  `charls`
- JPEG 2000 Part 1: OpenJPEG through `pkg-config` module `libopenjp2`
- High-Throughput JPEG 2000 Lossless, Lossless RPCL, and general HTJ2K:
  OpenJPH through `pkg-config` module `openjph`
- JPEG XL Lossless, JPEG Recompression, and general JPEG XL: libjxl through
  `pkg-config` module `libjxl`
- MPEG and video transfer syntaxes: FFmpeg libraries `libavcodec`,
  `libavformat`, `libavutil`, and `libswscale`
- MPEG2, MPEG-4 AVC/H.264, and HEVC/H.265 video Transfer Syntaxes: fragment
  pass-through only with `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH`; no local
  FFmpeg decode/encode is advertised
- SMPTE ST 2110 Transfer Syntaxes are recognized by UID but are not accepted
  through Pixel Data fragment pass-through; DICOM-RTV flow support is not
  implemented.
- General DICOM pixel codec backend: GDCM through CMake `find_package(GDCM)`

The external pixel codec option below is declared but intentionally blocked
until real codec implementations are added:

- `DICOMLIB_WITH_FFMPEG`

This prevents the library from advertising support for pixel-compressed transfer
syntaxes that it cannot actually decode or encode.

The detailed implementation plan and current completion state are maintained in
[`docs/TRANSFER_SYNTAX_PLAN.md`](docs/TRANSFER_SYNTAX_PLAN.md).

## C-DIMSE Status

C-DIMSE command sets are covered by tests for:

- C-ECHO
- C-STORE
- C-FIND
- C-GET command set structure
- C-MOVE

SCU response handling validates the response command field and the Message ID
Being Responded To. SCP dispatch handles C-ECHO, C-STORE, C-FIND, C-MOVE, and
routes C-GET to an explicit `NotYetImplemented` handler.

## Compatibility Notes

The project currently focuses on the C++ library. The maintained platform scope
is Linux and macOS only. Windows-specific and legacy build paths are not part of
the current supported configuration.
