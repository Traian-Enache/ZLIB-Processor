#ifndef _LZSS_H
#define _LZSS_H 1

#include <stdio.h>

void lzss_compress(FILE *in, FILE *out);

void lzss_decompress(FILE *in, FILE *out);

#endif  // _LZSS_H
