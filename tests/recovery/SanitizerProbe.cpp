#include <climits>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "heap") == 0) {
        volatile int* values = new int[4];
        values[8] = 7;
        delete[] values;
        return 0;
    }
    if (std::strcmp(argv[1], "uaf") == 0) {
        int* value = new int(11);
        delete value;
        volatile int observed = *value;
        return observed == 11 ? 0 : 1;
    }
    if (std::strcmp(argv[1], "undefined") == 0) {
        volatile int maximum = INT_MAX;
        volatile int overflow = maximum + 1;
        return overflow == 0 ? 0 : 1;
    }
    return 2;
}
