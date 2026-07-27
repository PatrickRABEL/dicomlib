# Transfer Syntax Support Plan

This plan records only support that is implemented and verified in this
repository. A Transfer Syntax is not advertised as codec-supported until the
corresponding encode/decode path is implemented and covered by tests.

## Completed

| Transfer Syntax family | UID(s) | Status | Build option | Verification |
| --- | --- | --- | --- | --- |
| Implicit VR Little Endian | `1.2.840.10008.1.2` | Native dataset encode/decode | Always on | `transfer_syntax_roundtrip` |
| Explicit VR Little Endian | `1.2.840.10008.1.2.1` | Native dataset encode/decode | Always on | `transfer_syntax_roundtrip` |
| Encapsulated Uncompressed Explicit VR Little Endian | `1.2.840.10008.1.2.1.98` | Uncompressed Pixel Data encode/decode as one OB fragment per frame | Always on | `transfer_syntax_roundtrip` |
| Explicit VR Big Endian | `1.2.840.10008.1.2.2` | Native dataset encode/decode | `DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN` | `transfer_syntax_roundtrip` |
| Deflated Explicit VR Little Endian | `1.2.840.10008.1.2.1.99` | Dataset encode/decode through zlib raw DEFLATE | `DICOMLIB_WITH_ZLIB` | `transfer_syntax_roundtrip` with zlib |
| RLE Lossless | `1.2.840.10008.1.2.5` | Pixel Data encode/decode through built-in DICOM RLE codec | `DICOMLIB_WITH_RLE` | `rle_codec`, `transfer_syntax_roundtrip` with RLE |
| JPEG Baseline | `1.2.840.10008.1.2.4.50` | 8-bit Pixel Data encode/decode through libjpeg/libjpeg-turbo, with lossy compression attributes written on encode | `DICOMLIB_WITH_JPEG` | `transfer_syntax_roundtrip` with JPEG |
| JPEG Extended Process 2 & 4 | `1.2.840.10008.1.2.4.51` | 12-bit Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| Retired non-hierarchical JPEG | `1.2.840.10008.1.2.4.52`, `.53`, `.55` | Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| JPEG Lossless Process 14 | `1.2.840.10008.1.2.4.57` | Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| JPEG Lossless Process 14 Selection Value 1 | `1.2.840.10008.1.2.4.70` | Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| JPEG-LS | `1.2.840.10008.1.2.4.80`, `.81` | Lossless and Near-Lossless Pixel Data encode/decode through CharLS; `.81` encode uses configured `DICOMLIB_JPEGLS_NEAR_LOSSLESS` | `DICOMLIB_WITH_JPEGLS` | `transfer_syntax_roundtrip` with JPEG-LS |
| JPEG 2000 Part 1 | `1.2.840.10008.1.2.4.90`, `.91` | Lossless Pixel Data encode/decode through OpenJPEG J2K codestreams | `DICOMLIB_WITH_JPEG2000` | `transfer_syntax_roundtrip` with JPEG 2000 |
| High-Throughput JPEG 2000 | `1.2.840.10008.1.2.4.201`, `.202`, `.203` | `.201` and `.202` lossless Pixel Data encode/decode through OpenJPH reversible HTJ2K codestreams; `.202` uses RPCL progression, sufficient decompositions for base resolution <= 64, and TLM markers; `.203` uses irreversible HTJ2K lossy encode with DICOM lossy compression attributes and decodes reversible or irreversible HTJ2K | `DICOMLIB_WITH_HTJ2K` | `transfer_syntax_roundtrip` with HTJ2K |
| JPEG XL | `1.2.840.10008.1.2.4.110`, `.111`, `.112` | `.110` lossless and `.112` lossy Pixel Data encode/decode through libjxl, with one encoded frame stored as one fragment; `.111` decodes JPEG XL JPEG Recompression streams and encodes only from existing encapsulated JPEG fragments through `JxlEncoderAddJPEGFrame`; `.112` encode writes DICOM lossy compression attributes with method `ISO_18181_1` | `DICOMLIB_WITH_JPEGXL` | `transfer_syntax_roundtrip` with JPEG XL |
| SCU presentation context proposal | Implemented syntaxes only | `PresentationContexts::Add()` proposes enabled native, Deflated, RLE, or pass-through syntaxes | Build options above | `transfer_syntax_support` |
| Encapsulated fragment pass-through | UIDs listed by generated `IsEncapsulatedTransferSyntaxUID()` | Fragment-level read/write without pixel decode | `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH` | `transfer_syntax_support`, `transfer_syntax_roundtrip` |
| External dependency discovery | zlib, JPEG, GDCM, CharLS, OpenJPEG, OpenJPH, libjxl, FFmpeg | CMake detection and missing dependency reporting | `DICOMLIB_PREPARE_EXTERNAL_CODECS` | Configure-time checks |

## Remaining

| Transfer Syntax family | UID(s) | Required backend | Current status |
| --- | --- | --- | --- |
| Other retired JPEG process UIDs | `1.2.840.10008.1.2.4.54`, `.56`, `.58`-`.66` | GDCM and/or libjpeg/libjpeg-turbo, depending on process | Not implemented; local GDCM 3.2.7 does not expose direct Transfer Syntax enum values for these UIDs |
| JPEG 2000 Part 2 Multi-component | `1.2.840.10008.1.2.4.92`, `.93` | OpenJPEG Part 2 support or GDCM | Not implemented; local GDCM 3.2.7 exposes Part 2 Transfer Syntax identifiers but `ImageChangeTransferSyntax` cannot generate a verified Part 2 fixture in this workspace |
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
