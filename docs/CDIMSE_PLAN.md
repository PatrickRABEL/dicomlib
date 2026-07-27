# C-DIMSE Support Plan

This plan records only behavior implemented and verified in this repository.
C-DIMSE support is not described as complete unless the command set, SCU path,
SCP dispatch path, response validation, cancellation behavior, and relevant
network flow are covered by tests.

## Completed

| Service | Status | Verification |
| --- | --- | --- |
| C-ECHO | SCU request/response and SCP success response | `cdimse_commandsets` |
| C-STORE | SCU request/response and SCP callback dispatch | `cdimse_commandsets` |
| C-FIND | SCU request/response and SCP callback dispatch with pending matches followed by success | `cdimse_commandsets` |
| C-MOVE | SCU request/response command sets and SCP callback dispatch after Identifier read | `cdimse_commandsets` |
| C-GET | SCU request/response command sets and SCP callback dispatch after Identifier read; C-STORE sub-operations are application-handler responsibility | `cdimse_commandsets` |
| C-CANCEL-RQ command set | Command set for cancelling C-FIND, C-GET, or C-MOVE; SCP dispatch accepts the command without requiring a SOP Class UID and records the referenced Message ID on the association state | `cdimse_commandsets` |
| SCU response validation | Response Command Field and Message ID Being Responded To are checked for C-ECHO, C-STORE, C-FIND, C-GET, and C-MOVE | Code path in `Cdimse.cpp` |

## Remaining

| Area | Current status |
| --- | --- |
| C-CANCEL asynchronous effect | The command is encoded, accepted by SCP dispatch, and recorded on `ServiceBase`. The single-thread-per-association request loop does not interrupt an already-running C-FIND, C-GET, or C-MOVE handler. A cancellable handler API and network test are required before claiming full cancellation semantics. |
| C-GET sub-operation orchestration | The SCP dispatch delegates to the registered handler. The library does not yet provide a built-in C-STORE sub-operation scheduler, counters, or final response generator for C-GET. |
| C-MOVE sub-operation orchestration | The SCP dispatch delegates to the registered handler. The library does not yet provide a built-in association opener to the Move Destination, C-STORE scheduler, counters, or final response generator for C-MOVE. |
| Service-specific status ranges | The command set can carry status values, but service/SOP-class-specific status validation and detailed failed/warning related fields are not complete. |
| Network end-to-end tests | Current tests cover command set construction and selected dispatch/SCU code paths. Full SCU/SCP association tests for each C-DIMSE service are still required. |
| Extended negotiation | SCU/SCP Role Selection, SOP Class Extended Negotiation, and Asynchronous Operations Window are not implemented as verified C-DIMSE behavior. |

## Execution Order

1. Add an in-process association test harness for SCU/SCP message exchange.
2. Add network coverage for C-CANCEL state observation during a running
   cancellable C-FIND, C-GET, or C-MOVE handler.
3. Implement and test C-GET C-STORE sub-operation orchestration on the same
   association, including pending/final response counters.
4. Implement and test C-MOVE sub-operation orchestration on destination
   associations, including pending/final response counters.
5. Add service/SOP-class-specific status helpers and tests.
6. Add verified extended negotiation support only after the association-layer
   behavior is implemented and covered.
