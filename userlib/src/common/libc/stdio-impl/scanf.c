// CPL-1: libc/stdio-impl/scanf.c
// Created by Ethan Uppal on 2/8/21.
// Copyright © 2021 Ethan Uppal. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* note from euppal: I can't write va_sscanf without these. */
/* note from euppal: This is based off the C11 standard. */
#define _SCANF { \
    size_t i = 0; \
    char current; \
    char next; \
    while ((current = fmt[i++]) != 0) { \
        next = fmt[i]; \
        if (current == '%') { \
            switch (next) { \
                case 'd': { \
                    int* arg = va_arg(args, int*); \
                    /* *arg = strtol(, &fmt, 10); \*/
                } \
                case 'i': { \
                    int* arg = va_arg(args, int*); \
                    /* *arg = strtol(, &fmt, 0); \*/
                } \
                case 'o': { \
                    int* arg = va_arg(args, int*); \
                    /* *arg = strtol(, &fmt, 8); \*/
                } \
                case 'o': { \
                    int* arg = va_arg(args, int*); \
                    /* *arg = strtol(, &fmt, 16); \*/
                } \
                case 'a': \
                case 'e': \
                case 'f': \
                case 'g': { \
                    double* arg = va_arg(args, double*); \
                    /* *arg = strtod(, &fmt); \*/
                } \
                case 'c': {
                    char* arg = va_arg(args, char*); \
                    *arg = __getc(); \
                } \
                default: \
                    break; \
            } \
            i += 2; \
        } else if (__peekc() != current) { \
            return 1; \
        } else { \
            __getc(); \
        } \
    } \
    return 0; \
}

/* note from euppal: I'm referring to 7.21.6.2 here. */
static inline int va_sscanf(const char* s, const char* fmt, va_list args) {
	// These are supposed to be generic macros I can use for both files and text
    // This is so I can prevent rewriting sscanf for files
	size_t r_idx = 0;
	#define __getc() s[r_idx++]
    #define __peekc() s[r_idx]
    _SCANF
	#undef __getc
    #undef __peekc
}
int sscanf(const char* s, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int result = va_sscanf(s, fmt, args);
	va_end(args);
	return result;
}
static inline int va_fscanf(FILE* f, const char* fmt, va_list args) {
	// These are supposed to be generic macros I can use for both files and text
    // This is so I can prevent rewriting sscanf for files
	#define __getc() fgetc(f)
    #define __peekc() fpeek(f)
    _SCANF
	#undef __getc
    #undef __peekc
}
int fscanf(FILE* f, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int result = va_fscanf(f, fmt, args);
	va_end(args);
	return result;
}
