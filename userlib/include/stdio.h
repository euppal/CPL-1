#ifndef __CPL1_LIBC_STDIO_H_INCLUDED__
#define __CPL1_LIBC_STDIO_H_INCLUDED__

#include <stdarg.h>
#include <stddef.h>

#define EOF -1

int printf(const char *fmt, ...);
int snprintf(char *buf, int size, const char *fmt, ...);
int va_printf(const char *fmt, va_list args);
int va_snprintf(char *buf, int size, const char *fmt, va_list args);
int puts(const char *str);

int getchar(void);
int putchar(int c);

int sscanf(const char* s, const char* fmt, ...);
int fscanf(FILE* f, const char* fmt, ...);

ssize_t getline(char** lineptr, size_t* n, FILE* stream);

extern FILE* __stdin_ptr;
extern FILE* __stdout_ptr;
extern FILE* __stderr_ptr;

int fpeek(FILE* stream);

#define stdin __stdin_ptr
#define stdout __stdout_ptr
#define stderr __stderr_ptr

#endif
