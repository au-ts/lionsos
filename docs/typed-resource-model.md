<!--
    Copyright 2026, LionsOS Contributors
    SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Typed resource and authority model

## Purpose

LionsOS should grow toward a coherent, inspectable model of resources and authority.
The inspiration is one of the strongest ideas in Windows NT: externally visible system
resources have explicit types, stable operations, controlled handles, security checks
and meaningful lifetimes. The goal is not to reproduce NT's kernel or its large object
manager. LionsOS already has a better foundation for a small trustworthy system:
seL4 capabilities, isolated protection domains and deliberately narrow shared-memory
protocols.

This document records a direction for future development. It is not a request to
rewrite the existing system immediately.

## Meaning of “object” in LionsOS

An object is a typed resource that can be referenced and operated on through an
explicit authority-bearing handle. It is not a C++ class and does not imply inheritance,
virtual methods, hidden allocation or a kernel-wide object hierarchy.

Examples include:

- a file or directory opened by a filesystem service;
- a GUI surface or window owned by an application;
- a timer subscription;
- a network endpoint;
- a device queue;
- a shared-memory region;
- an application, sandbox or agent identity;
- a grant to read one particular file or use one particular service.

The seL4 objects and capabilities underneath remain the roots of isolation. User-space
services may expose higher-level logical objects, but their handles must not claim
stronger isolation than the capabilities and mappings that enforce them.

## Desired properties

### Explicit types and operations

Every public handle has one resource type and a documented set of legal operations.
Receiving a file handle must not accidentally grant directory administration; receiving
a window must not grant access to the display or another application's input. Reject a
handle used with the wrong protocol or operation.

Prefer small protocol-specific handle types over one universal integer handle. When the
wire format requires an integer, validate its type, generation, owner and current state
at the service boundary.

### No ambient authority

A component should be able to act only through capabilities, mappings and handles it was
explicitly given. Avoid APIs whose meaning depends on global process state, an implicit
current user, unrestricted path lookup or access to every instance of a service.

Configuration injected into a PD should describe its initial authority. Runtime grants
must be explicit messages or capability transfers whose source and destination can be
identified.

### Delegation and attenuation

It should be possible to give another component less authority than the giver holds:
read-only instead of read-write, one file instead of a directory, one window instead of
the display, a bounded memory range instead of an address space, or a rate-limited timer
instead of unrestricted scheduling control.

Prefer creating a restricted derived handle over teaching every consumer to reinterpret
a broad handle safely.

### Revocation and lifetime

Every resource type needs an explicit lifetime model:

- who creates it;
- who owns the authoritative state;
- whether references may be duplicated or delegated;
- what close, revoke and owner failure mean;
- how stale handles are rejected;
- how in-flight operations finish or are cancelled;
- which memory and capabilities are reclaimed.

Generation-tagged handles are useful for preventing stale-reference reuse, but the
generation must match exactly. Revocation must invalidate every derivative that the
contract promises to revoke.

### Inspectable authority graph

The long-term system should be able to answer:

> What exactly is this protection domain, application or AI agent allowed to do?

The answer should be derivable from one conceptual graph:

- subjects are PDs, applications, sandboxes and agents;
- resources are typed objects;
- edges are specific grants with rights and provenance;
- derivation records delegation or attenuation;
- lifecycle state records whether a grant is live, closed or revoked.

The graph may be assembled from multiple isolated services. It does not require a single
privileged global database or kernel object manager. A diagnostic authority service can
collect read-only descriptions from those services without becoming the enforcement
point for all of them.

### Auditing

Security-relevant services should emit structured events for grants, delegation,
denials, use of sensitive operations, close and revocation. Events should identify the
subject, resource type, stable diagnostic identity, requested operation, result and
grant provenance where available.

Do not log secret contents or rely on logs for enforcement. Audit records explain the
authority graph and its use; capabilities and service validation enforce it.

### Stable contracts

Compatibility belongs at protocol boundaries. Public resource protocols should define:

- a version or extensible feature negotiation strategy;
- fixed-width wire types and checked sizes;
- explicit status values;
- ownership and concurrency rules;
- behavior for unknown operations and fields;
- lifecycle and recovery behavior.

Keep implementation details private to the service so schedulers, allocators, storage
backends and policy engines can evolve without changing the client contract.

## User-facing namespace

LionsOS should present an ordinary user with a small, familiar and understandable view
of the machine. Users should not need to learn the internal service graph, seL4
capabilities or a traditional Unix filesystem hierarchy to install an application,
find personal files or understand what belongs to the operating system.

The initial desktop view should resemble this logical structure:

```text
C:\
|-- Applications
|-- Users
|-- System
|-- Shared
`-- Devices
```

`C:` is a presentation name for the primary system volume, not a kernel primitive and
not the source of a resource's identity or authority. Additional volumes may receive
similarly friendly names. Compatibility environments may expose aliases such as
`C:\Program Files` or POSIX paths such as `/usr`, `/home` and `/etc`, but the native
services should not depend on either compatibility spelling.

Paths are names that help humans find resources. Resolving a path should produce a
typed handle whose rights are checked by the owning service; possession of a path must
not by itself grant access. Renaming a resource or viewing it through another namespace
must not silently change its permissions.

The intended meanings are:

- `Applications` contains discoverable application packages and their manifests;
- `Users` contains each user's documents and user-owned application data;
- `System` contains operating-system components and is read-only during normal use;
- `Shared` contains explicitly shared data rather than data made public by accident;
- `Devices` is a friendly view of available devices, while device access remains
  controlled by typed capabilities and device services.

Application installation should be package-oriented and reversible. An application
package declares its identity, executable components, resources, protocol versions and
requested capabilities. Mutable settings, caches and user documents live outside the
package. The application manager should install, update and remove packages without
requiring users to manipulate implementation directories, and removal should clearly
offer whether to retain or delete user data.

The desktop, file manager and application manager are the primary interface to this
namespace. Technical backing objects and service-private storage should be hidden by
default but inspectable with developer tools. A compatibility layer may translate
familiar paths into native object lookups; it must not become a second, contradictory
security model.

## Where enforcement belongs

Use the narrowest trustworthy layer that can enforce a property:

- seL4 capabilities and mappings enforce access to kernel objects and memory;
- a device virtualiser enforces access to device queues and buffers;
- a filesystem service enforces file-handle operations and path policy;
- the compositor enforces surface ownership, focus and input routing;
- a sandbox host enforces application-level service grants;
- an optional policy service decides which grants should be created.

Policy may be centralised for consistency, but enforcement should remain at each resource
boundary. Avoid a universal privileged broker through which every operation must pass:
it would enlarge the trusted computing base, create a bottleneck and weaken LionsOS's
component isolation.

## Relationship to the current code

Several current designs already point in this direction:

- seL4 capabilities and Microkit channels establish initial PD authority;
- filesystem descriptors are typed by server state and carry generations;
- GUI slots separate surfaces, state and input queues per application;
- WebAssembly capability tables name and check window, timer, console and file grants;
- the experimental sandbox maps only the memory represented by its grants and revokes
  derived objects when it stops;
- the dynamic-capability example explores runtime creation, delegation and revocation.

These are useful prototypes, not yet one system-wide model. In particular, an
application-level integer capability checked inside a host PD is not equivalent to an
seL4 capability. Documentation and APIs must state where enforcement actually occurs.

## Guidance for new work

When adding a service or resource, answer these questions before fixing its wire format:

1. What is the resource's precise type and authoritative owner?
2. Which operations exist, and what right does each require?
3. What unforgeable reference or validated handle names it?
4. Can the reference be delegated, duplicated or attenuated?
5. How is it closed or revoked, including while operations are in flight?
6. How are stale, forged, cross-client and wrong-type handles rejected?
7. What memory or kernel capabilities technically enforce the stated boundary?
8. Which events are needed to reconstruct grants, denials and revocations?
9. How can the protocol evolve while old clients fail safely?
10. Can the design be tested with malformed messages, exhaustion and lifecycle races?

Prefer extending an existing resource protocol when the semantics truly match. Create a
new type when rights, ownership or lifecycle differ. Do not force unrelated resources
through a generic interface merely to make the model appear uniform.

## Adoption strategy

Adopt this model incrementally:

1. Use it for every new subsystem and protocol.
2. Correct unsafe handle validation and document existing lifetimes.
3. Give existing services typed diagnostic descriptions of their live resources and
   grants.
4. Standardise common audit-event fields without centralising enforcement.
5. Add delegation and revocation only where there is a real use case and an enforcing
   mechanism.
6. Build an authority-graph inspection tool after the underlying services can report
   truthful state.

Do not pause useful desktop, driver or application work for a wholesale abstraction
rewrite. The model succeeds when it makes security boundaries simpler and more visible,
not when it adds layers or terminology.

## Incremental delivery

LionsOS values progression over premature perfection. Development should favour small
vertical slices that can be booted, seen and used: a desktop appears, an icon launches
one application, a file manager displays the namespace, or an application can be
installed and removed. A visibly incomplete feature can provide more learning and
motivation than an unseen subsystem polished in isolation.

This principle is not permission to hide failures or weaken the trust model. Prototype
limitations, crashes and missing behavior should be visible and recorded. Security
claims must describe the enforcement that actually exists, persistent data formats
must avoid known corruption, and experimental interfaces should remain easy to replace.

For each substantial desktop capability, prefer this sequence:

1. Establish the narrow end-to-end path through the real component boundaries.
2. Make the result observable in the desktop or a useful diagnostic tool.
3. Record known limitations and failure behavior instead of pretending completeness.
4. Stabilise the protocol and data model after the interaction has taught us what is
   actually needed.
5. Harden validation, recovery and tests in proportion to the feature's authority and
   persistence risk.

Agents should therefore avoid spending long periods perfecting an isolated layer when
a smaller integrated milestone would test the architecture sooner. They should also
avoid throwaway shortcuts that bypass capability checks or create a parallel design
that cannot evolve into the intended system. The preferred prototype is incomplete but
architecturally honest.

## Non-goals

- Reimplementing Windows NT or importing its kernel architecture.
- Moving high-level resource management into the seL4 kernel.
- Building one omnipotent object manager.
- Introducing C++ inheritance or mandatory object-oriented implementation techniques.
- Replacing concrete typed protocols with a universal message format.
- Claiming application-level handles provide kernel-enforced isolation when they do not.
- Retrofitting stable code without a security, lifecycle or compatibility benefit.

The intended result is recognisably LionsOS: a small seL4 kernel, simple isolated
services and explicit high-performance shared-memory protocols, with a consistent answer
to who holds authority over each resource and how that authority changes over time.
