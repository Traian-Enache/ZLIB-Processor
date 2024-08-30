#include <stdio.h>
#include "lzss.h"
#include <assert.h>

int main(int argc, char **argv)
{
	if (argc != 2)
		return -1;
	FILE *in = fopen("lz.out", "rb");
	FILE *out = fopen("lz.ref", "wb");

	assert(in);
	assert(out);

	if (*argv[1] == 'd')
		lzss_decompress(in, out);
	else
		lzss_compress(in, out);
	fclose(in);
	fclose(out);
	return 0;
}
