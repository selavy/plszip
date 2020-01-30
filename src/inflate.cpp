#include <libgen.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>


#define panic(fmt, ...)                                   \
    do {                                                  \
        fprintf(stderr, "ERR: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                          \
    } while (0)

int main(int argc, char** argv) {
    const char* input_filename;
    const char* output_filename;
    if (argc == 2) {
        input_filename = argv[1];
        output_filename = nullptr;
    } else if (argc == 3) {
        input_filename = argv[1];
        output_filename = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [FILE] [OUT]\n", argv[0]);
        exit(0);
    }

    FILE* fp = fopen(input_filename, "rb");
    if (!fp) {
        panic("failed to open input file: %s", input_filename);
    }

    FILE* output = output_filename ? fopen(output_filename, "wb") : stdout;
    if (!output) {
        panic("failed to open output file: %s", output_filename ?: "<stdout>");
    }


    fclose(fp);
    fclose(output);
    return 0;
}
