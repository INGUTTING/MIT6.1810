#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/stat.h"

int
isNumber(char s)
{
	if( s >= '0' && s <= '9'){
		return 1;
		}
	return 0;
}

int 
isSeparator(char s){
	char SeparatorList[] = " -\r\t\n./,";
	return strchr(SeparatorList,s) != 0;
}

int
isSixFive(int number){
	if( (number!= 0) && ( (number % 5 == 0) || (number % 6 == 0))){
		return 1;
	}
	return 0;
}

int 
main(int argc, char* argv[]){
	int fd, n;
	int number = 0;
	// add a new flag to decide whether to print the /
	int Ilegal = 0;
	// use to go through the argument of argv
	int i = 1;
	// buffer
	char buf;
	int isNumberPrint = 0;
	if(argc < 2){
		exit(1);
	}


	// for sixfive , it only need to solve the second segment past to the command line
	while(argv[i]){
		fd = open(argv[i],O_RDONLY);
		if(fd < 0){
			exit(1);
		}

		// read the file fd pointing to and rewrite a char to buf[0] everytime
		while( (n = read(fd, &buf, 1)) > 0){
			if(isNumber(buf)){
				number = number * 10 + (buf - '0');
				isNumberPrint = 1;
			}
			else if(isSeparator(buf)){
				if(isSixFive(number) && isNumberPrint){
					printf("%s%d\n", Ilegal ? "/" : "",number);
				}

				number = 0;
				Ilegal = 0;
				isNumberPrint = 0;
			}
			else{
				if(!isNumberPrint){
					Ilegal = 1;
				}
			}
		}
		// print the last
		if(isSixFive(number) && isNumberPrint){
			printf("%s%d\n", Ilegal ? "/" : "",number);
		}
		
		close(fd);
		i++;
	}
	exit(0);
}

