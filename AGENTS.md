<!-- BEGIN OPENAMROBOT SHARED RULES v2 -->
# OpenAMRobot rules for contributors and agents
Canonical: openAMRobot/.github, agent-rules/SHARED_RULES.md, copied verbatim into every
repository's AGENTS.md (tools/check_agent_rules.py reports drift). This file holds process;
approved technical values live in openAMRobot/.github decisions.yaml. Labels: [check: tool]
means the tool detects that violation, and only in repositories where its workflow is installed
and required (rollout/README.md); [template: file]; [human: role, evidence] is a reviewer
decision. A text check proves textual consistency, never mechanical, electrical or safety correctness.

## Decisions
- decisions.yaml is the only register of approved values, limits, exclusions and distinctions.
  No file states a contradicting value; kept history carries `decision-allow: <ID> <reason>`.
  [check: check_decisions.py, listed patterns only] [human: entry's reviewer, entry's evidence]
- Changing a decision: (1) open a contract change request issue naming the entry; (2) the
  owner updates the source document; (3) one reviewed PR updates the register entry, its
  supersedes history, its check patterns with a test, and every affected consumer, or links
  each consumer PR; (4) the owner approves. [template: contract change request] [human: owner]
- Source documents are provenance. CI reads only the pinned register, never a drive or the
  docs site; a disagreement is reported to the owner, and no tool rewrites either side. [human: owner]
- Read STATE.md before work in a repository; update it in the same PR. [check: check_pr_evidence.py]

## Contracts
- Messages, services, actions, schemas, topic names, launch argument names and configuration
  IDs change only through a contract change request and one PR that updates the contract
  package and its consumers together. [template: contract change request] [human: software lead]
- New contract proposals stay labelled Proposed until the owner accepts them. [human: software lead]

## Tests
- Every behaviour change carries a test that fails when the change is reverted; the Tests
  section shows that failing run. [template: PR Tests section] [human: reviewer, the revert run]
- A suite that executes zero tests fails. [check: verify.sh; check_pr_evidence.py on reported counts]
- skip, xfail and importorskip name a tracking issue on the same line. [check: verify.sh]
- SKIP or BLOCKED is not PASS. Fixtures and fake hardware are not simulation, physical
  acceptance or release readiness. [human: reviewer, Not verified section]

## Evidence
- Every PR states base SHA, head SHA, exact commands, test counts and a Not verified section.
  [check: check_pr_evidence.py, presence only] [human: reviewer, that the commands were run]
- A draft becomes ready only when the evidence check passes on the current head. [human: author;
  ruleset required check once installed]

## Dependencies and licences
- Nothing is added, removed or upgraded as a side effect. A changed dependency manifest needs
  a Dependencies section naming each change, its licence and source. [check: check_pr_evidence.py]
- Licence headers and package.xml tags match the licence map: MIT software and firmware,
  CERN-OHL-P-2.0 hardware, CC-BY-4.0 documentation. [human: repository owner]
- The Integration Gate section lists overlapping PRs, reused existing or upstream work and
  what was rejected. Third-party provenance stays intact. [template: PR template]
- LICENSE, LICENSING.md, NOTICE and CODEOWNERS change only in a PR whose task names them.
  [human: platform lead]

## Safety
- No agent authors or modifies E-stop, brake, contactor, watchdog, motor-enable or
  charge-inhibit logic; agents report the need in an issue. [check: check_pr_evidence.py fails
  a safety-path change whose AI disclosure is not None] [human: platform lead, undisclosed use]
- A safety-path change needs two human approvals including the platform lead. The check only
  reports "safety path touched, two human approvals required" and that reviewers are requested;
  approvals are a ruleset requirement. [human: platform lead and one more maintainer]
- Functional telemetry, watchdogs, status displays and fixtures are never safety evidence.
  [check: check_decisions.py wording only] [human: platform lead, hardwired safety-chain test record]
- Automated or untrusted PR jobs never reach motion hardware or secrets. [human: CI owner]

## Publication
- Public material (docs/, assets/, README.md, any path containing "public") has no internal
  document links, prices, contact data or credentials. [check: check_public_extract.py]
- It names no private person, customer or partner; the application name is Use_Case_1. [human: docs owner]

## Agents
- Before any write, state a precondition block: repository (exact full name), branch, parent
  SHA, expected outcome. Read-only unless the task says otherwise. [template: agent-prompts/]
- Agent PRs stay draft until the work-package owner writes adopt, adapt or reject in the thread.
  [human: work-package owner]
- The PR's AI disclosure section names the tool and what it produced. [check: section present]
- Commits carry DCO sign-off with the contributor's own identity; never invent an identity or
  attestation. [human: maintainer; DCO check where installed]
- Gate A Teensy/MPU6500 and Gate B STM32/ICM-42688-P stay distinct; Jetson is the 2.0 compute.
  [check: check_decisions.py] Arm vendor SDKs stay behind device packages. [human: software lead]

## Failure
- A failed precondition (repository, branch, SHA, access, source) stops the task. Report the
  command, the error and the next step; never work around it. [template: agent-prompts/]
- Never merge, force-push, change settings, weaken a check or invent a result. [human: ruleset]

## Learning
- Every agent or process mistake gets an issue labelled harness. [template: harness mistake form]
- The monthly retro turns harness issues into one PR against this block. [human: software lead]
<!-- END OPENAMROBOT SHARED RULES v2 -->

# Repository-specific rules: openamr-platform-fw
- Scope: base-controller firmware. Gate A: Teensy 4.0 with the MPU6500, a linorobot2 overlay
  (`boards/teensy_4_0/linorobot2_overlay/`). Gate B and 2.0: STM32H723ZG on the NUCLEO-H723ZG
  (`boards/stm32/`, CubeMX project in PR #22). The portable C core goes under `firmware/`.
- Owners (.github/CODEOWNERS): every path @evans-kwashie @BotshareAI.
- Licences: MIT for OpenAMRobot work; files derived from linorobot2 stay Apache-2.0 with their
  headers (NOTICE.md, base linorobot2_hardware `aaf9d59`). STMicroelectronics files under
  `Drivers/` stay unmodified with their own licences.
- Only the overlay files are kept; never copy or edit upstream linorobot2 files outside the
  overlay, and never edit vendored ST files under `Drivers/`.
- Checks (PR #23 and its follow-ups, not on main yet): STM32 Debug/Release build with the
  RAM_D2 descriptor check, Teensy Gate A PlatformIO build, `python -m unittest discover -s tests`.
- Pins, buses and settings come from the register (BASE-CONTROLLER-IO) and the electrical
  contract in openamr-platform-hw; a CubeMX change that differs from it is reported, not
  pushed into either side.
- Agents never author or change E-stop, brake, contactor, watchdog (including the `/cmd_vel`
  timeout), motor-enable or charge-inhibit logic; tests may read that code. No automated job
  flashes a board or drives a safety output.
