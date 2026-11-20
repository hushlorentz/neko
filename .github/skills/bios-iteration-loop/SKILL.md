---
name: bios-iteration-loop
description: Run an explicitly authorized bounded batch of researched BIOS discovery fixes, with local code-associated commits, campaign verification and final optimized/ASan gates. Pause only for a concrete question requiring user input.
---

# Bounded BIOS iteration loop

This skill coordinates development; `drive-bios-campaign` remains the execution,
monitoring, evidence and finalization owner. Invoke that skill for every
campaign operation. Do not create another runner or multi-agent workflow.

Read `AGENTS.md`, the current direction and active ownership in `PROJECT.md`,
and the relevant `ROADMAP.md` block before starting. Preserve manual-first
research, source authorization, red/green development, authored conformance,
risk-based independent review, validation and evidence requirements.

## Authorization and durable state

Require explicit user authorization for the batch, its iteration limit and
agent-owned commits. Skill invocation alone does not grant commit
permission. The authorization recorded in `PROJECT.md` on 2026-11-19 permits
one batch of five iterations and local commits only.
Do not restart the counter or renew this grant implicitly.
The user's 2026-11-19 amendment removes the easy-change-only restriction:
research and implement complex capabilities autonomously when the evidence
supports a bounded contract. Complexity, architecture work and active
handshake/command/IRQ/DMA/timing work are not automatic pause reasons.

Only within that batch, the agent-owned implementation commit replaces the
user-owned checkpoint in AGENTS.md and drive-bios-campaign, and the amended
question-driven rule replaces routine difficulty and active-hardware approval
pauses. Evidence, source authorization, validation, review and campaign gates
remain; the usage gate is explicitly removed below.
Do not push, amend, rewrite history or change remotes.

Record batch/session identity, completed iteration count, in-progress stage,
commit identities, campaign ownership,
the agreed discovery objective and its observable success criteria, next action,
and the existing full-test cadence in `PROJECT.md`. Resume from
that state, not from a fresh counter. Reconcile any active campaign before
rebuilding its executable or changing evidence.
Use the current agreed objective, not an invented broader milestone. If that
objective is ambiguous at launch, clarify it before beginning the batch.

## Usage amendment

On 2026-11-19 the user removed the 1,000-credit constraint because manual usage
requests defeat automation. Do not gate progress on session credits or request
`/usage` at boundaries. Keep durable stage/count/ownership records so context
compaction does not restart the batch or lose an owned campaign.

## One coherent iteration

1. Attribute the earliest originating failure from retained evidence, not a
   later exception-handler stop. Assess scope, existing state owner, evidence
   and risk. Review the block breakdown before beginning a new block.
2. Research relevant local manuals first, then authorized secondary sources
   with required refresh/provenance. Select and record an evidence-backed
   bounded contract and coherent architecture, then implement autonomously
   regardless of difficulty. Ask only when a concrete unresolved question
   requires user input; do not ask for routine permission to do the work.
3. Add the narrowest failing regression, verify the expected failure, implement
   independently, and complete authored conformance, focused integration
   validation and required read-only independent review. Resolve findings and
   run any immediately due broader gates before committing.
4. Inspect the diff and stage only this iteration's code, tests and associated
   documentation. Preserve unrelated dirty changes. Use the agreed plain
   imperative commit-message convention and required Copilot trailers. Choose
   a short capability-specific subject under that convention; honor any exact
   subject supplied by the user. Make one local implementation commit;
   no ROADMAP-only commits.
5. Record risk and justified finite verification scope, then invoke
   drive-bios-campaign for fresh unchanged-SCPH-70012 same-build verification.
   Preserve the selected mode's traced/plain, fault-identity, snapshot and full
   canonical comparisons, lifecycle and finalization. Inspect actual former
   capability coverage; cycle advance alone does not prove retirement or boot.
6. Reconcile evidence, record the next origin/action, update project/roadmap
   state and inspect/prune only obsolete generated output. Post-verification
   documentation may accompany the next code commit; do not create a standalone
   progress commit. Increment the iteration count only after verification and
   handoff are complete. Check whether the agreed discovery objective has been
   achieved before selecting another dependency.

## Stop and closure

Stop further iterations as soon as retained evidence establishes the agreed
discovery objective, even before five iterations. Reconcile/finalize any owned
campaign, finish any already-due validation and report the observed success
and its bounded scope. Do not turn success into a new objective automatically
or infer completed boot from fault retirement, cycle advance or helper exit0.

Pause only when there is a concrete question the user must answer. Resolve
research, architectural decisions, reviewer findings, test failures and
suspicious verification autonomously when available evidence permits it.
If evidence cannot support a bounded choice, an input/identity/settings change
needs approval or required source access is not authorized, state the question
and ask through ask_user.
Do not fabricate hardware behavior or weaken acceptance to avoid a question.
A BIOS-exposed repair remains part of the current iteration and requires a
new tested/reviewed code commit; it is not another completed iteration.

After five completed iterations, run the usual complete optimized and ASan
checks:

```sh
cmake -P cmake/Check.cmake
cmake -P cmake/Sanitize.cmake
```

Do not run concurrent complete gates against the same build directory.
Risk-triggered earlier gates, milestone leak checks and review requirements
still apply. Resolve test failures within the batch's capability scope.
Pause only if resolving them requires a concrete user decision; report the
failing gate and evidence with that question.

End with completed count, local commits, validation results
or pending gates, bounded BIOS outcome, next dependency and retained evidence/
reclaimed space. Pause after closure; never push or begin a sixth iteration.
