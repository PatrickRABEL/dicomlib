# Transfer Syntax Support Plan

This plan records only support that is implemented and verified in this
repository. A Transfer Syntax is not advertised as codec-supported until the
corresponding encode/decode path is implemented and covered by tests.

## Completed

| Transfer Syntax family | UID(s) | Status | Build option | Verification |
| --- | --- | --- | --- | --- |
| Implicit VR Little Endian | `1.2.840.10008.1.2` | Native dataset encode/decode | Always on | `transfer_syntax_roundtrip` |
| Explicit VR Little Endian | `1.2.840.10008.1.2.1` | Native dataset encode/decode | Always on | `transfer_syntax_roundtrip` |
| Explicit VR Big Endian | `1.2.840.10008.1.2.2` | Native dataset encode/decode | `DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN` | `transfer_syntax_roundtrip` |
| Deflated Explicit VR Little Endian | `1.2.840.10008.1.2.1.99` | Dataset encode/decode through zlib raw DEFLATE | `DICOMLIB_WITH_ZLIB` | `transfer_syntax_roundtrip` with zlib |
| RLE Lossless | `1.2.840.10008.1.2.5` | Pixel Data encode/decode through built-in DICOM RLE codec | `DICOMLIB_WITH_RLE` | `rle_codec`, `transfer_syntax_roundtrip` with RLE |
| JPEG Baseline | `1.2.840.10008.1.2.4.50` | 8-bit Pixel Data encode/decode through libjpeg/libjpeg-turbo, with lossy compression attributes written on encode | `DICOMLIB_WITH_JPEG` | `transfer_syntax_roundtrip` with JPEG |
| JPEG-LS | `1.2.840.10008.1.2.4.80`, `.81` | Lossless and Near-Lossless Pixel Data encode/decode through CharLS; `.81` encode uses configured `DICOMLIB_JPEGLS_NEAR_LOSSLESS` | `DICOMLIB_WITH_JPEGLS` | `transfer_syntax_roundtrip` with JPEG-LS |
| JPEG 2000 Part 1 | `1.2.840.10008.1.2.4.90`, `.91` | Lossless Pixel Data encode/decode through OpenJPEG J2K codestreams | `DICOMLIB_WITH_JPEG2000` | `transfer_syntax_roundtrip` with JPEG 2000 |
| JPEG XL Lossless | `1.2.840.10008.1.2.4.110` | Lossless Pixel Data encode/decode through libjxl, with one encoded frame stored as one fragment | `DICOMLIB_WITH_JPEGXL` | `transfer_syntax_roundtrip` with JPEG XL |
| SCU presentation context proposal | Implemented syntaxes only | `PresentationContexts::Add()` proposes enabled native, Deflated, RLE, or pass-through syntaxes | Build options above | `transfer_syntax_support` |
| Encapsulated fragment pass-through | UIDs listed by generated `IsEncapsulatedTransferSyntaxUID()` | Fragment-level read/write without pixel decode | `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH` | `transfer_syntax_support`, `transfer_syntax_roundtrip` |
| External dependency discovery | zlib, JPEG, GDCM, CharLS, OpenJPEG, OpenJPH, libjxl, FFmpeg | CMake detection and missing dependency reporting | `DICOMLIB_PREPARE_EXTERNAL_CODECS` | Configure-time checks |

## Remaining

| Transfer Syntax family | UID(s) | Required backend | Current status |
| --- | --- | --- | --- |
| Legacy JPEG Extended/Lossless | `1.2.840.10008.1.2.4.51`, `.57`, `.70` and retired JPEG process UIDs in the generated UID registry | GDCM and/or libjpeg/libjpeg-turbo, depending on process | Dependencies declared; codec integration not implemented |
| JPEG 2000 Part 2 Multi-component | `1.2.840.10008.1.2.4.92`, `.93` | OpenJPEG Part 2 support or GDCM | Dependencies declared; codec integration not implemented |
| High-Throughput JPEG 2000 | `1.2.840.10008.1.2.4.201`, `.202`, `.203` | OpenJPH | Dependencies declared; codec integration not implemented |
| JPEG XL JPEG Recompression and general JPEG XL | `1.2.840.10008.1.2.4.111`, `.112` | libjxl | Dependencies declared; codec integration not implemented |
| MPEG/video | MPEG and video UIDs in the generated transfer syntax registry | FFmpeg | Dependencies declared; codec integration not implemented |
| JPIP referenced | `1.2.840.10008.1.2.4.94`, `.95`, `.204`, `.205` | JPIP retrieval policy and parser | UID recognition only; no local pixel codec behavior claimed |

## Execution Order

1. Keep native, Deflated, and RLE tests passing in default, zlib, RLE, and combined zlib/RLE builds.
2. Install or provide the missing codec SDKs required by `DICOMLIB_PREPARE_EXTERNAL_CODECS`.
3. Add one codec backend at a time.
4. For each backend, add tests from known valid DICOM files or minimal codestream fixtures before allowing association support.
5. Update `TS::hasCompiledPixelCodec()` only after the codec path and tests are present.
6. Keep unsupported pixel codec CMake options blocked with `FATAL_ERROR` until implementation is complete.

## Local Dependency State

The current macOS workspace detects:

- zlib
- libjpeg/libjpeg-turbo
- GDCM
- CharLS
- OpenJPEG (`libopenjp2`)
- OpenJPH
- JPEG XL (`libjxl`)
- FFmpeg libraries

The current macOS workspace detects all codec dependency probes required by
`DICOMLIB_PREPARE_EXTERNAL_CODECS`.
