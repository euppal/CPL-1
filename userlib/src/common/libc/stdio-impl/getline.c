// CPL-1: libc/stdio-impl/getline.c
// Created by Ethan Uppal on 2/7/21.
// Copyright © 2021 Ethan Uppal. All rights reserved.

#include <stdio.h>

/* note from euppal: I can't write getline without this. */
#define fgetc(f) getchar()

ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
	if (lineptr == NULL) return -1;
	if (n == NULL) return -1;
	char* line = malloc(4);
	size_t len = 0;
	size_t cap = 4;

	char c;
	while ((c = fgetc(stream))) {
		if (len + 1 > cap) {
			cap *= 2;
			line = realloc(line, cap);
		}
		line[len++] = c;
	}

	*lineptr = line;
	return *n;
}
