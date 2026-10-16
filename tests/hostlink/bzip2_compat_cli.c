#include "bzlib.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv)
{
    const unsigned capacity = 4 * 1024 * 1024;
    char* input = (char*)malloc(capacity);
    char* output = (char*)malloc(capacity);
    unsigned size, output_size = capacity;
    int status;
    if (argc != 3 || !input || !output) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    size = (unsigned)fread(input, 1, capacity, stdin);
    if (ferror(stdin) || size == capacity) return 2;
    if (argv[1][0] == 'c')
        status = BZ2_bzBuffToBuffCompress(output, &output_size, input, size, atoi(argv[2]), 0, 30);
    else
        status = BZ2_bzBuffToBuffDecompress(output, &output_size, input, size, atoi(argv[2]), 0);
    if (status != BZ_OK)
    {
        free(input);
        free(output);
        return 3;
    }
    status = fwrite(output, 1, output_size, stdout) == output_size ? 0 : 4;
    free(input);
    free(output);
    return status;
}
