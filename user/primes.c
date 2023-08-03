#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

const int N = 35;


void proc(int lfd){ //接收来自左管道的数据
    int index, buf;
    int rfd[2];
    
    //埃氏筛素数法，选取每轮传入数的第一个为筛选数
    //将该轮管道筛选数读入到 index 中
    //2 -> 3 -> 5 -> 7...
    if (read(lfd, &index, sizeof(int))){ //将待筛选数的第一个作为 `index` 
        printf("prime %d\n", index);
    }
    else exit(0);

    pipe(rfd);

    int fpid = fork();
    if (fpid == 0){
        close(rfd[1]);

        proc(rfd[0]); //将当前管道的读端传给右管道

        close(rfd[0]);
        exit(0);
    }
    else if(fpid > 0){
        close(rfd[0]);
        while (read(lfd, &buf, sizeof(int))){
            if (buf % index != 0){
                write(rfd[1], &buf, sizeof(int));
            }
        }
        close(rfd[1]);

        wait(0);
        exit(0);
    }
}


void main(){
    //main process
    int fd[2];
    pipe(fd);

    int fpid = fork();
    if (fpid == 0){
        close(fd[1]);

        proc(fd[0]); //将当前管道的读端传给右管道

        close(fd[0]);
        exit(0);
    }
    else if (fpid > 0){
        close(fd[0]);
        for (int i = 2; i <= N; i ++){
            write(fd[1], &i, sizeof(int));
        }
        close(fd[1]);

        wait(0);
        exit(0);
    }
}