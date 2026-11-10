/* Failing fixture for tools/ci/run_cppcheck.py: out-of-bounds write. */
int openamr_bug(void)
{
    int table[3] = {0, 1, 2};
    table[3] = 4;
    return table[0];
}
