#ifndef __CPL1_LIBC_STDLIB_H_INCLUDED__
#define __CPL1_LIBC_STDLIB_H_INCLUDED__

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void *reallocarray(void *ptr, size_t nmemb, size_t size);

// https://man7.org/linux/man-pages/man3/strtol.3.html
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);

// https://man7.org/linux/man-pages/man3/strtod.3.html
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
 
#endif
