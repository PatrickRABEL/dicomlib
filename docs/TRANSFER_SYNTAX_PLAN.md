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
| Deflated Image Frame Compression | `1.2.840.10008.1.2.8.1` | Pixel Data encode/decode through zlib raw DEFLATE as one fragment per frame | `DICOMLIB_WITH_ZLIB` | `transfer_syntax_roundtrip` with zlib |
| RLE Lossless | `1.2.840.10008.1.2.5` | Pixel Data encode/decode through built-in DICOM RLE codec | `DICOMLIB_WITH_RLE` | `rle_codec`, `transfer_syntax_roundtrip` with RLE |
| JPEG Baseline | `1.2.840.10008.1.2.4.50` | 8-bit Pixel Data encode/decode through libjpeg/libjpeg-turbo, with lossy compression attributes written on encode | `DICOMLIB_WITH_JPEG` | `transfer_syntax_roundtrip` with JPEG |
| JPEG Extended Process 2 & 4 | `1.2.840.10008.1.2.4.51` | 12-bit Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| Retired non-hierarchical JPEG | `1.2.840.10008.1.2.4.52`, `.53`, `.55` | Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| JPEG Lossless Process 14 | `1.2.840.10008.1.2.4.57` | Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| JPEG Lossless Process 14 Selection Value 1 | `1.2.840.10008.1.2.4.70` | Pixel Data decode through GDCM; already-fragmented OB Pixel Data can be written for pass-through, but native recompression is not implemented | `DICOMLIB_WITH_GDCM` | `transfer_syntax_roundtrip` with GDCM |
| JPEG-LS | `1.2.840.10008.1.2.4.80`, `.81` | Lossless and Near-Lossless Pixel Data encode/decode through CharLS; `.81` encode uses configured `DICOMLIB_JPEGLS_NEAR_LOSSLESS` | `DICOMLIB_WITH_JPEGLS` | `transfer_syntax_roundtrip` with JPEG-LS |
| JPEG 2000 Part 1 | `1.2.840.10008.1.2.4.90`, `.91` | Lossless Pixel Data encode/decode through OpenJPEG J2K codestreams | `DICOMLIB_WITH_JPEG2000` | `transfer_syntax_roundtrip` with JPEG 2000 |
| JPIP Referenced | `1.2.840.10008.1.2.4.94`, `.204` | Explicit VR Little Endian Data Set encode/decode with validation of `Pixel Data Provider URL` and absence of top-level Pixel Data; no JPIP network retrieval or referenced pixel stream decode | Always on | `transfer_syntax_roundtrip`, `transfer_syntax_support` |
| JPIP Referenced Deflate | `1.2.840.10008.1.2.4.95`, `.205` | JPIP Referenced Data Set encode/decode with zlib raw DEFLATE applied to the whole Data Set after/before Explicit VR Little Endian encoding | `DICOMLIB_WITH_ZLIB` | `transfer_syntax_roundtrip` with zlib, `transfer_syntax_support` |
| High-Throughput JPEG 2000 | `1.2.840.10008.1.2.4.201`, `.202`, `.203` | `.201` and `.202` lossless Pixel Data encode/decode through OpenJPH reversible HTJ2K codestreams; `.202` uses RPCL progression, sufficient decompositions for base resolution <= 64, and TLM markers; `.203` uses irreversible HTJ2K lossy encode with DICOM lossy compression attributes and decodes reversible or irreversible HTJ2K | `DICOMLIB_WITH_HTJ2K` | `transfer_syntax_roundtrip` with HTJ2K |
| JPEG XL | `1.2.840.10008.1.2.4.110`, `.111`, `.112` | `.110` lossless and `.112` lossy Pixel Data encode/decode through libjxl, with one encoded frame stored as one fragment; `.111` decodes JPEG XL JPEG Recompression streams and encodes only from existing encapsulated JPEG fragments through `JxlEncoderAddJPEGFrame`; `.112` encode writes DICOM lossy compression attributes with method `ISO_18181_1` | `DICOMLIB_WITH_JPEGXL` | `transfer_syntax_roundtrip` with JPEG XL |
| SCU presentation context proposal | Implemented syntaxes only | `PresentationContexts::Add()` proposes enabled native, Deflated, RLE, or pass-through syntaxes | Build options above | `transfer_syntax_support` |
| Encapsulated fragment pass-through | UIDs listed by generated `IsEncapsulatedTransferSyntaxUID()` | Fragment-level read/write without pixel decode | `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH` | `transfer_syntax_support`, `transfer_syntax_roundtrip` |
| MPEG/video fragment pass-through | `1.2.840.10008.1.2.4.100`, `.100.1`, `.101`, `.101.1`, `.102`, `.102.1`, `.103`, `.103.1`, `.104`, `.104.1`, `.105`, `.105.1`, `.106`, `.106.1`, `.107`, `.108` | Encapsulated Pixel Data fragments can be read/written unchanged; no FFmpeg decode/encode is advertised | `DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH` | `transfer_syntax_support`, `transfer_syntax_roundtrip` with pass-through |
| SMPTE ST 2110 recognition | `1.2.840.10008.1.2.7.1`, `.7.2`, `.7.3` | Public UID constants and explicit rejection from encapsulated Pixel Data pass-through; no DICOM-RTV flow support is advertised | Always on | `transfer_syntax_support` |
| External dependency discovery | zlib, JPEG, GDCM, CharLS, OpenJPEG, OpenJPH, libjxl, FFmpeg | CMake detection and missing dependency reporting | `DICOMLIB_PREPARE_EXTERNAL_CODECS` | Configure-time checks |

## Remaining

| Transfer Syntax family | UID(s) | Required backend | Current status |
| --- | --- | --- | --- |
| Other retired JPEG process UIDs | `1.2.840.10008.1.2.4.54`, `.56`, `.58`-`.66` | GDCM and/or libjpeg/libjpeg-turbo, depending on process | Not implemented; local GDCM 3.2.7 does not expose direct Transfer Syntax enum values for these UIDs |
| JPEG 2000 Part 2 Multi-component | `1.2.840.10008.1.2.4.92`, `.93` | OpenJPEG Part 2 support or GDCM | Not implemented; local GDCM 3.2.7 exposes Part 2 Transfer Syntax identifiers but `ImageChangeTransferSyntax` cannot generate a verified Part 2 fixture in this workspace |
| MPEG/video local decode/encode | MPEG and video UIDs in the generated transfer syntax registry | FFmpeg | Dependencies declared; codec integration not implemented |
| DICOM-RTV SMPTE ST 2110 flows | `1.2.840.10008.1.2.7.1`, `.7.2`, `.7.3` | DICOM-RTV metadata/audio flow implementation | Not implemented; these UIDs are not accepted through Pixel Data fragment pass-through |
| JPIP network retrieval | JPIP URLs referenced by `1.2.840.10008.1.2.4.94`, `.95`, `.204`, `.205` | JPIP client implementation and retrieval policy | Not implemented; local encode/decode validates and preserves the DICOM Data Set reference only |

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
