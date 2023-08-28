#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void xexec(char *x_argv[]){
    int fpid, status;
    if ((fpid = fork()) == 0){
        exec(x_argv[0], x_argv);
        exit(0);
    }
    wait(&status);
    return;     // 这里不能写 exit, 不然主进程就直接退出了
}


int main(int argc, char *argv[]){
    if (argc < 2){
        // does it count all of these args within ehco to | to xargs?
        fprintf(2, "Usage: xargs <cmd> ...\n");
        exit(1);
    }
    if (argc > MAXARG + 1){
        fprintf(2, "Usage: too manny args\n");
        exit(1);
    }

    char *x_args[MAXARG];
    memset(x_args, 0, MAXARG);  // char[] 未经初始化，是random的

    for (int i = 1; i < argc; i ++){
        x_args[i - 1] = argv[i];
    }

    char buf[8*MAXARG], c;
    memset(buf, 0, sizeof(buf));

    int c_count = 0;
    
    while (read(0, &c, 1) > 0){
       if (c != '\n'){
            buf[c_count ++] = c;

            continue;
       }
       else{
            if (c_count != 0){
                buf[c_count] = '\0';
    
                x_args[argc - 1] = buf;
                xexec(x_args);
                
                c_count = 0;
                
                continue;
            }
        }
        fprintf(2, "Error: Unknown error occurred\n");
        exit(1);
    }
    
    // for (int i = 0; i < sizeof(x_args) / 8; i ++){
    //     printf("%s\n",x_args[i]);
    // }
 
    exit(0);
}