#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    int pp1[2],pp2[2];
    pipe(pp1);
    pipe(pp2);
    int pid =  fork();
    if (pid == 0){ //child process

     //P -> C pipe  
        pid = getpid();
        char buf[1];
        close(pp1[1]);
        read(pp1[0], buf, 1);
        printf("%d: received ping\n", pid);    

    //C -> P pipe
        close(pp2[0]);
        write(pp2[1], buf, 1);
    }

    else { // parent process
    //P -> C pipe
        pid = getpid();
        close(pp1[0]);
        write(pp1[1], "8", 1);

    //C -> P pipe
        char buf[1];
        close(pp2[1]);
        read(pp2[0], buf, 1);
        printf("%d: received pong\n", pid);
    }
    
    exit(0);
}