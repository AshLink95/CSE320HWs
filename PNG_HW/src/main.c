#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "png_reader.h"
#include "png_chunks.h"
#include "png_steg.h"
#include "png_overlay.h"

char* USAGE = "Usage: bin/png -f png_file [options]\n"
"Options:\n"
"  -f png_file        Input PNG file (required)\n"
"  -h                    Print this help message\n"
"  -s                    Print chunk summary\n"
"  -p                    Print palette summary\n"
"  -i                    Print IHDR fields\n"
"  -e message -o out_file Encode message and write to output file\n"
"  -d                    Decode and print hidden message\n"
"  -m file2 -o out_file [-w width] [-g height]  Overlay file2 (smaller) over input and write to output\n";

int main(int argc, char **argv)
{
    printf("%s", USAGE);
    return 0;
}
