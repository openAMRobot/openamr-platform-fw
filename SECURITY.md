# Security Policy

## Reporting Security Issues

Please do not report security issues publicly in GitHub Issues.

If you discover a vulnerability, unsafe behavior, exposed credential, or other sensitive
technical issue, contact the maintainers privately:

    botshare.ai@gmail.com

Include: repository name, affected files/components, a description, steps to reproduce,
possible impact, and a suggested fix if known.

## Scope

This policy applies to:

- the firmware overlay source (`boards/teensy_4_0/linorobot2_overlay/`)
- configuration and documentation in this repository

## Safety note (firmware-specific)

This firmware drives real motors. Some debug paths can move the motors on command — in
particular the bounded open-loop mode gated by `ENABLE_POWERED_DEBUG` and the
`/debug/openloop` topic. Treat any powered debug feature as a physical-safety concern:
keep the robot on blocks / clear of people when enabling it. See
`docs/safety/motion-safety.md`.
