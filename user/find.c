#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// char* get_filename(char *path) {
//     char *p;
//     for (p=path+strlen(path); p>=path && *p != '/'; p--);
//     p++;
//     return p;
// }

void find(char const *path, char const *name)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){  // readonly
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    fprintf(2, "Usage: find fir file\n");
    exit(1);

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path); // buf = path:
    p = buf + strlen(buf); // p = path[-1] = '\0'
    *p++ = '/';          // path[-1] = '/'  p++
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ); //buf = path + / + de.name
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      if (st.type == T_DIR){
        find(buf, name);
      }
      else if(st.type == T_FILE){
        if (strcmp(de.name, name) == 0){
          printf("%s\n", buf);
        }
        continue;
      }
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc < 3){
    fprintf(2, "Usage: wrong args\n");
    exit(1);
  }
  char const *path = argv[1];
  char const *name = argv[2];

  find(path, name);
  exit(0);
}
