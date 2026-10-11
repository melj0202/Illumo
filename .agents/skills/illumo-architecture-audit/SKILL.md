---
name: illumo-architecture-audit
description: Audit or explain Illumo architecture using the live repository and canonical documentation. Use for architecture reviews, call-path or ownership mapping, subsystem assessments, design reports, refactor proposals, or explicit NO CHANGES and READ-ONLY requests. Default to read-only and do not implement recommendations unless separately authorized.
---

# Illumo Architecture Audit

Treat the live implementation as current-state evidence and canonical documentation as design intent.

## Establish authority and state

1. Inspect `git status --short` and preserve the worktree byte-for-byte for read-only requests.
2. Read the root and closest nested `AGENTS.md`, `README.md`, `docs/architecture-consensus.md`, relevant package maps, CMake, tests, and live implementation.
3. Read `docs/output/illumo.pdf` when its content is not already available. If it is stale during a read-only task, report that fact instead of regenerating it.
4. Identify disagreements among code, tests, build configuration, instructions, and documentation rather than silently choosing a convenient version.

## Trace the live system

Use this as an orientation map, then verify each edge in code:

```text
Platform entry -> App composition -> Illumo services/modules
  -> Scene ordered drawables
  -> Drawable::AppendCommands -> Renderer -> CommandQueue
  -> IBackend -> GLBackend/GLDevice or MockBackend
```

For the requested subsystem, determine ownership, lifetime, dependency direction, public contracts, error paths, threading or affinity, and resource cleanup. Separate production execution from compatibility fixtures, test-only fallbacks, incomplete stubs, and generated output.

## Classify every conclusion

- **Observed:** directly supported by live code, tests, configuration, or runtime evidence.
- **Documented intent:** asserted by canonical repository guidance or architecture documents.
- **Mismatch:** code and authority disagree, with both sides cited.
- **Inference:** a reasoned conclusion whose missing evidence is named.
- **Proposal:** a possible change, its benefit, cost, migration impact, and authorization boundary.

## Apply architectural discipline

- Preserve Illumo as a modular C++ game framework centered on rendering (`docs/charter-direction.md`); application behavior and content meaning stay in consuming products such as CSim.
- Preserve App composition, Engine service ownership, renderer-token execution, sparse-domain versus bounded-view separation, primitive-composed UI, and compatibility-fixture boundaries unless change is explicitly authorized.
- Add abstraction only for demonstrated consumers or volatility. Do not propose an ECS, render graph, scene graph, backend, or framework for completeness.
- Treat ownership, lifetime, threading, persistence, dependency direction, subsystem boundaries, and public contracts as architectural changes requiring explicit authorization.

## Report the audit

Lead with the current architecture and material findings. Rank findings by correctness or maintenance impact, cite files and symbols, and separate required fixes from optional follow-ups. State which checks were performed and what remains uncertain. A read-only audit must leave the repository unchanged.
