#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/log.h>
#include <sys/syscall.h>

void Ls_PrintVersion() {
	printf("ls. Copyright (C) 2021 Zamiatin Iurii and CPL-1 contributors\n");
	printf("This program comes with ABSOLUTELY NO WARRANTY; for details type \"ls --license\"\n");
	printf("This is free software, and you are welcome to redistribute it\n");
	printf("under certain conditions; type \"ls --license\" for details.\n");
}

void Ls_PrintHelp() {
	printf("usage: ls [folder1] [folder2] [folder3] ...\n");
}

void Ls_PrintLicense() {
	char buf[16536];
	int licenseFd = open("/etc/src/COPYING", O_RDONLY);
	if (licenseFd < 0) {
		Log_ErrorMsg("\"ls\" Utility", "Failed to read license from \"/etc/src/COPYING\"");
	}
	int bytes = read(licenseFd, buf, 16535);
	if (bytes < 0) {
		Log_ErrorMsg("\"ls\" Utility", "Failed to read license from \"/etc/src/COPYING\"");
	}
	buf[bytes] = '\0';
	printf("%s\n", buf);
}

void Ls_ListDirectory(const char *dir) {
	int fd = open(dir, O_RDONLY);
	if (fd < 0) {
		Log_ErrorMsg("\"ls\" Utility", "Failed to open directory \"%s\"", dir);
	}
	struct dirent buf;
	bool first = true;
	while (true) {
		int result = getdents(fd, &buf, 1);
		if (result < 0) {
			if (!first) {
				puts("\n");
			}
			Log_ErrorMsg("\"ls\" Utility", "Error occured while enumerating the directory \"%s\"", dir);
		}
		if (result == 0) {
			break;
		}
		if (strcmp(buf.d_name, "..") == 0 || strcmp(buf.d_name, ".") == 0) {
			continue;
		}
		printf("%s ", buf.d_name);
		first = false;
	}
	close(fd);
}

int main(int argc, char const *argv[]) {
	if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
		Ls_PrintVersion();
		return 0;
	}
	if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		Ls_PrintHelp();
		return 0;
	}
	if (argc == 2 && (strcmp(argv[1], "--license") == 0)) {
		Ls_PrintLicense();
		return 0;
	}
	if (argc < 2) {
		Ls_ListDirectory(".");
		puts("\b\n");
	} else if (argc == 2) {
		Ls_ListDirectory(argv[1]);
		puts("\b\n");
	} else {
		for (int i = 1; i < argc; ++i) {
			printf("%s:\n\t", argv[i]);
			Ls_ListDirectory(argv[i]);
			if (i < argc - 1) {
				puts("\b\n\n");
			} else {
				puts("\b\n");
			}
		}
	}
	return 0;
}