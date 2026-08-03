# dicomlib API Overview

This document records the maintained public API surface. It does not claim full
DICOM conformance beyond behavior implemented and covered by repository tests.

## Build

Include headers from the repository root and link the `dicom` CMake target:

```cmake
target_link_libraries(my_app PRIVATE dicom)
```

The maintained targets are Linux and macOS on x86, x86_64, ARM 32-bit, and ARM
64-bit.

## Core Data API

- `dicom::DataSet` stores DICOM attributes keyed by `dicom::Tag`.
- `DataSet::Put<VR_xx>(tag, value)` writes typed values.
- `DataSet::operator()(tag) >> value` reads typed values.
- `dicom::Sequence` is `std::vector<dicom::DataSet>`.
- `dicom::UID` stores and validates UID strings.
- `dicom::File::Read()` and `dicom::File::Write()` read and write PS3.10 files
  for implemented Transfer Syntax paths.

## Transfer Syntax API

- `dicom::TS` represents a Transfer Syntax.
- Native Little Endian, native Explicit VR Big Endian, Deflated, RLE, selected
  JPEG/JPEG-LS/JPEG 2000/HTJ2K/JPEG XL, JPIP-referenced dataset wrappers, and
  encapsulated pass-through paths are documented in
  `docs/TRANSFER_SYNTAX_PLAN.md`.
- A Transfer Syntax is codec-supported only when an encode/decode path and tests
  exist.
- Recognized UID constants alone do not mean local pixel codec support.

Transfer Syntax status categories:

- **Supported**: local read/write or DIMSE negotiation behavior is implemented
  and covered by tests.
- **Recognized**: constants or registry entries exist, but local codec behavior
  is not implied.
- **Pass-through**: encapsulated fragments can be preserved unchanged, but the
  library does not decode or recompress the pixel stream.
- **Not implemented**: the library must not advertise support for this behavior.

## Association API

- `dicom::PresentationContexts` builds proposed abstract/transfer syntax lists.
- `dicom::ClientConnection` opens an association and can propose caller-provided
  User Information, including Role Selection, SOP Class Extended Negotiation,
  and Asynchronous Operations Window sub-items.
- `dicom::Server` accepts associations and dispatches registered service
  handlers.
- `Server::SetImplementationClassUID()` overrides the Implementation Class UID
  announced during association negotiation. Empty string restores the library
  default; non-empty malformed implementation UIDs are ignored. The override
  must be at most 64 characters and contain non-empty numeric components
  separated by single dots.
- `Server::SetImplementationVersionName()` overrides the Implementation Version
  Name. Values longer than 16 characters are ignored.
- `Server::SetForkPerAssociation(true)` is POSIX-only and intended for
  standalone SCP processes. State that must survive an association must be
  external to the child process or explicitly propagated to the parent.
- `Server::SetMaxConcurrentAssociations(n)` limits simultaneous associations;
  `0` means unlimited.

## C-DIMSE API

Implemented generic services:

- C-ECHO
- C-STORE
- C-FIND
- C-GET
- C-MOVE
- C-CANCEL-RQ

Main entry points:

- `ClientConnection::Echo()`
- `ClientConnection::Store()`
- `ClientConnection::Find()`
- `ClientConnection::Move()`
- `CGetSCU`, `CFindSCU`, `CMoveSCU`, `CStoreSCU`, `CEchoSCU`
- `Server::AddHandler()`
- `Server::AddFindHandler()`
- `Server::AddCancellableFindHandler()`
- `Server::AddCancellableGetHandler()`
- `Server::AddCancellableMoveHandler()`
- `Server::AddMoveStoreHandler()`

C-CANCEL can be observed by cancellable handlers through `PollCCancelRQ()`.
The library does not asynchronously terminate arbitrary application callback
code.

## N-DIMSE API

Implemented generic command transport:

- N-EVENT-REPORT
- N-GET
- N-SET
- N-ACTION
- N-CREATE
- N-DELETE

Main entry points:

- `NEventReportSCU`
- `NGetSCU`
- `NSetSCU`
- `NActionSCU`
- `NCreateSCU`
- `NDeleteSCU`
- `Server::AddNEventReportHandler()`
- `Server::AddNGetHandler()`
- `Server::AddNSetHandler()`
- `Server::AddNActionHandler()`
- `Server::AddNCreateHandler()`
- `Server::AddNDeleteHandler()`

The library validates generic N-DIMSE command fields, status classes, role
state, and tested Data Set Type rules. SOP-class-specific normalized service
semantics remain application callback responsibility until concrete service
classes are implemented and tested.

Implemented N-DIMSE means generic PS3.7 command transport and validation for
the six normalized services above. It does not mean full conformance for every
PS3.4 normalized SOP Class. A SOP Class becomes supported only when its
attribute rules, state transitions, status behavior, SCU/SCP paths, and tests
are present.

## Not Implemented

- Asynchronous DIMSE operation scheduler.
- Forced interruption of application callback code after C-CANCEL.
- SOP-class-specific N-DIMSE service logic such as Print Management.
- MPEG/video local pixel decode/encode through FFmpeg.
- DICOM-RTV SMPTE ST 2110 media flows.
- JPIP network retrieval.
- JPEG 2000 Part 2 local decode/recompression.
- Local decode/recompression for the remaining retired JPEG process UIDs listed
  as pass-through only.
