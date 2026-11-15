/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/* Negative test object: a simulation-reference symbol injected into a board build so that
 * the post-link guard must fail it. Used only by guard_test/poison_board.cmake in CI. */
void simref_poison_marker(void);
void simref_poison_marker(void) {}
