/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/* Negative test object (SAF-MCU-001): a reference object linked into a board build but never
 * referenced. --gc-sections drops its code, so the ELF can carry no simref_ symbol, yet the
 * object was in the board link. tools/ci/check_board_image_isolation.py must fail on the map.
 * Used only by guard_test/poison_unreferenced_board.cmake in CI. */
int simref_unreferenced_marker(int x);
int simref_unreferenced_marker(int x) { return x + 1; }
