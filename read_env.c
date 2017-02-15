#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <error.h>


#define MAX_VAR_LEN 256
#define MAX_CMD_LEN 128

//p = strchr(p_dirent->d_name, '.')

int search_dir(const char* path, const char* fn) {
	struct dirent *p_dirent;
	DIR* dir = opendir(path);
	char* p;
	size_t size = strlen(fn)-1;
	if(dir == NULL) {
		//fprintf(stderr, "Can't open this directory.");
		return -1;	/* Can't open it */
	}
	while((p_dirent = readdir(dir)) != NULL) {
		if(memcmp(p_dirent->d_name, fn, size) == 0 &&
			p_dirent->d_name[size] == '.') {
			return 1;	/* found */
		}
	}
	closedir(dir);
	return 0;	/* Not found */
}

char* parse_path_variable(const char* fn) {
	static char path_var[MAX_VAR_LEN];
	char* s = getenv("PATH");
	char* p = s;
	char* t = path_var;
	int c = 0;

	while(*p && *p == ' ') p++;

	while(*p != '\0') {
		while(*p && *p != ':') {
			*t++ = *p++;
		}
		*t = '\0';
		if(search_dir(path_var, fn) > 0) {
			return path_var;
		}
		p++;
		while(*p && *p == ' ') p++;
		t = path_var;
	}
	return NULL;
}

int main() {
	char cwd[1024];
	char cmdline[MAX_CMD_LEN];
	char* dir;
	while(1) {
		if(fgets(cmdline, MAX_CMD_LEN, stdin) != NULL && ferror(stdin))
			fprintf(stderr, "fgets error\n");
		if(feof(stdin)) {
			fflush(stdout);
			exit(0);
		}
		if((dir = dir = parse_path_variable(cmdline)) != NULL) {
			printf("Found at: %s\n", dir);
		}
		break;
	}

	if(getcwd(cwd, sizeof(cwd)) != NULL) {
		fprintf(stdout, "Current working dir: %s\n", cwd);
	}
	else {
		perror("getcwd() error");
	}

	return 0;
}