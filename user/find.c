#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"



// void runExec(char* file){
//     if(!exec_argvs[0]) return;

//     int pid = fork();
//     if(pid < 0){
//         fprinf(2, "fork error\n");
//         return;
//     }
//     if(pid == 0){
//         char *argv[MAXARG];
//         int i = 1;
//         while(exec_argvs[i]){
//             argv[i-1] = exec_argvs[i];
//             i++;
//         }
//         argv[i] = 0;
//         exec(exec_argvs[0],argv);
//         exit(1);
//     }
//     else{
//         wait(0);
//     }
// }

char*
fmtname(char* path){
	static char buf[DIRSIZ+1];
	char *p;

	for(p=path+strlen(path); p >= path && *p != '/'; p--)
		;
	p++;

	if(strlen(p) >= DIRSIZ)
		return p;
	memmove(buf, p , strlen(p));
	memset(buf+strlen(p), ' ',DIRSIZ - strlen(p));
	buf[sizeof(buf) - 1] = '\0';
	return buf;
}


int match(const char* desName, char *s){
	if(strlen(desName) != strlen(s)){
		return 0;
	}
	for(int i = 0;i < strlen(desName); i++){
		if(desName[i] != s[i]){
			return 0;
		}
	}
	return 1;
}

// useful function to add fullpath getting in ls
char* fullpath(char *path, char* name){
	char *buf,*p;
	buf = malloc(sizeof(char) * 512);
	memset(buf,'\0',512);
	strcpy(buf,path);
	p = buf + strlen(buf);
	*p++ = '/';
	for(int i = 0; i < strlen(name);i++){
		*p++ = name[i];
	}
	*p = '\0';
	return buf;
}


// achieve the find function
void find(char *path, char *file_name,int exec_mode, char **exec_argv){
	struct dirent de;
	struct stat st;
	int fd;

	fd = open(path,0);
	// if the file cannot open then return 
	if(fd < 0) return;

    // if(fstat(fd,&st) < 0){
    //     // fprintf(1, "cannot open ");
    //     close(fd);
    //     return;
    // }

	while(read(fd,&de,sizeof(de)) == sizeof(de)){
		if(de.inum == 0){
			continue;
		}
		char *full_path = fullpath(path,de.name);
		// if stat fail then continue
		if (!full_path)
            continue;

        if(stat(full_path, &st) < 0) {
            free(full_path);
            continue;
        }
		switch(st.type){
			case T_DEVICE:
				break;
			case T_DIR:
				if(strcmp(de.name,".") == 0 ||strcmp(de.name,"..") == 0){
					continue;
				}
				find(full_path, file_name,exec_mode,exec_argv);
				break;
			case T_FILE:
				if(strcmp(de.name,file_name) == 0){
					if(exec_mode){
						int pid = fork();
						if(pid == 0){
							char **args = (char**)malloc(sizeof(char*) * MAXARG);
							for(int j = 0; j< MAXARG;j++){
								args[j] = (char*)malloc(sizeof(char)* MAXARG);
							}
							int i = 0;
							for(;exec_argv[i];i++) args[i] = exec_argv[i];
							args[i++] = full_path;
							args[i] = 0;
							exec(args[0],args);
							printf("exec failed\n");
							exit(1);
						}
						else{
							wait(0);
						}
					}
					else{
						printf("%s\n",full_path);
					}
				}
				break;
		}
		free(full_path); 
	}
	close(fd);
}

int main(int argc, char* argv[]){
	char * path = argv[1];
    char * pattern = argv[2];
	int exec_mode = 0;
	char **exec_argv = 0;

    int fd;
    struct stat st;

	if(argc < 3){
        printf("Error\n");
        exit(-1);
    }

    // check if the path is valid
    if((fd = open(path,O_RDONLY) )< 0){
        fprintf(2,"find: cannot open %s\n", path);
        exit(1);
    }

    if(fstat(fd, &st) < 0){
        fprintf(2,"find: cannot stat %s\n", path);
        close(fd);
        exit(1);
    }

    if(st.type != T_DIR){
        fprintf(2,"find: %s is not a directory\n" ,path);
        close(fd);
        exit(1);
    }

	if(argc >= 5 && strcmp(argv[3], "-exec") == 0){
    	exec_mode = 1;
    	exec_argv = &argv[4];  
	}

    close(fd);
    find(path,pattern,exec_mode,exec_argv);

    exit(0);
}


