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
| Local P-DATA C-DIMSE coverage | C-CANCEL-RQ SCU write/SCP read/handle and C-FIND SCU response Message ID validation are covered through `ServiceBase` over a local socket pair | `cdimse_commandsets` |
| Local association primitive coverage | A-ASSOCIATE-RQ and A-ASSOCIATE-AC are exchanged over a local socket pair, then C-ECHO is executed on the negotiated association state | `cdimse_commandsets` |
| Thread-backed C-ECHO | `ClientConnection::Echo()` is verified against `Server` running in a background thread with A-ASSOCIATE negotiation | `cdimse_commandsets` |
| Thread-backed C-STORE | `ClientConnection::Store()` is verified against a registered `Server` C-STORE handler with A-ASSOCIATE negotiation and success response validation | `cdimse_commandsets` |
| Thread-backed C-FIND | `ClientConnection::Find()` is verified against a registered `Server` C-FIND handler with pending match data and final success response | `cdimse_commandsets` |
| Thread-backed C-MOVE dispatch | `ClientConnection::Move()` is verified against a registered `Server` handler that receives the Identifier and writes a final C-MOVE response | `cdimse_commandsets` |
| Thread-backed C-GET dispatch | `CGetSCU` is verified against a registered `Server` handler that receives the Identifier and writes a final C-GET response | `cdimse_commandsets` |
| Thread-backed C-CANCEL dispatch | `CFindSCU::writeCancelRQ()` is verified on a `Server`/`ClientConnection` association and SCP dispatch logs a handled C-CANCEL-RQ | `cdimse_commandsets` |
| C-CANCEL observation by running handler | `PollCCancelRQ()` lets a running C-FIND/C-GET/C-MOVE handler poll the association for a pending C-CANCEL-RQ; verified on a thread-backed C-FIND association | `cdimse_commandsets` |

## Remaining

| Area | Current status |
| --- | --- |
| C-CANCEL final cancel semantics | A running handler can now observe C-CANCEL by polling, but the library does not automatically interrupt the handler or automatically generate final C-FIND, C-GET, or C-MOVE responses with Cancel status. That response behavior remains application-handler responsibility until a cancellable handler contract is added. |
| C-GET sub-operation orchestration | The SCP dispatch delegates to the registered handler. The library does not yet provide a built-in C-STORE sub-operation scheduler, counters, or final response generator for C-GET. |
| C-MOVE sub-operation orchestration | The SCP dispatch delegates to the registered handler. The library does not yet provide a built-in association opener to the Move Destination, C-STORE scheduler, counters, or final response generator for C-MOVE. |
| Service-specific status ranges | The command set can carry status values, but service/SOP-class-specific status validation and detailed failed/warning related fields are not complete. |
| Network end-to-end tests | Current tests cover command set construction, selected dispatch/SCU code paths, local P-DATA round trips, a local A-ASSOCIATE primitive exchange with C-ECHO, thread-backed C-ECHO/C-STORE/C-FIND/C-GET dispatch/C-MOVE dispatch/C-CANCEL dispatch, and C-CANCEL polling by a running C-FIND handler. |
| Extended negotiation | SCU/SCP Role Selection, SOP Class Extended Negotiation, and Asynchronous Operations Window are not implemented as verified C-DIMSE behavior. |

## Execution Order

1. Define and test a cancellable handler contract for C-FIND, C-GET, and C-MOVE
   that maps observed C-CANCEL to final Cancel status responses.
2. Implement and test C-GET C-STORE sub-operation orchestration on the same
   association, including pending/final response counters.
3. Implement and test C-MOVE sub-operation orchestration on destination
   associations, including pending/final response counters.
4. Add service/SOP-class-specific status helpers and tests.
5. Add verified extended negotiation support only after the association-layer
   behavior is implemented and covered.
