/* Clean fixture for tools/ci/run_cppcheck.py. */
int openamr_clamp(int value, int low, int high)
{
    if (value < low)
    {
        return low;
    }
    return value > high ? high : value;
}
