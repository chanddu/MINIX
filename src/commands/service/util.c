
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void fatal(char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "fatal error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	exit(1);
}
