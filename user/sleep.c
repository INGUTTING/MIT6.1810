#include "kernel/types.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"

int Osleep(int time){
	pause(time);
	return 1;
}

int main(int argc, char *argv[]){
	if(argc < 2){
		fprintf(2, "sleep ticks\n");
		exit(1);
	}

	int n = atoi(argv[1]);
	if(n < 0){
		fprintf(2, "segment must be positive");
		exit(1);
	}
	if(Osleep(n) < 0){
		fprintf(2, "sleep out of control");
		exit(1);
	}
	exit(0);
}
