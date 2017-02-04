#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Globals */
int fd;
void* ptr;
#define TESTFILE "testfile"
#define TESTSIZE (64*1024*1024)

#define TIMESTAMP __builtin_readcyclecounter

static void
client(void){
    unsigned int loops = 0;
    volatile uint64_t* const buf = ptr;

    for(;;){
        for(;;){
            if(buf[0]) break;
        }
        buf[1] = TIMESTAMP();
        memset((void*)&buf[2], 1, TESTSIZE - (2*sizeof(uint64_t)));
        msync(ptr, TESTSIZE, MS_SYNC);

        for(;;){
            if(!buf[0]) break;
        }
        buf[1] = 0;
        msync((void*)&buf[1], sizeof(uint64_t), MS_SYNC);
        if(!(loops % 1000)){
            printf("Done @ %d\n",loops);
        }
        loops++;
    }
}

static void
initiator(void){
    unsigned int loops = 0;
    volatile uint64_t* const buf = ptr;
    uint64_t ping;
    uint64_t pong;
    for(;;){
        ping = TIMESTAMP();
        buf[0] = ping;
        for(;;){
            pong = buf[1];
            if(pong) break;
        }
        /* Check content */
retry:
        for(int i = 2; i!=(TESTSIZE/sizeof(uint64_t)); i++){
            if(!buf[i]){
                //printf("Retry @ %d:%d\n",loops,i);
                goto retry;
            }
        }
        for(int i = 2; i!=(TESTSIZE/sizeof(uint64_t)); i++){
            buf[i] = 0;
        }
        buf[0] = 0;
        msync(ptr, TESTSIZE, MS_SYNC);

        for(;;){
            if(!buf[1]) break;
        }

        if(!(loops % 1000)){
            printf("Done @ %d, %ld => %ld = %ld\n",loops,
                   ping, pong, pong - ping);
        }
        loops++;
    }
}

static void
mapfd(void){
    ptr = mmap(NULL, TESTSIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
               fd, 0);
    if(ptr == MAP_FAILED){
        printf("mmap() failed %d\n",errno);
        exit(1);
    }
}

int
main(int ac, char** av){
    int r;
    /* First, try to create TESTFILE, */
    r = open(TESTFILE, O_RDWR|  O_EXCL | O_CREAT | O_TRUNC,
             S_IRWXU | S_IRWXG | S_IRWXO);
    if(r>0){
        /* Client */
        fd = r;

        /* Setup file */
        r = ftruncate(fd, TESTSIZE);
        if(r<0){
            printf("ftruncate() failed %d\n", errno);
            exit(1);
        }

        mapfd();

        /* Fill as 0 */
        memset(ptr, 0, TESTSIZE);
        msync(ptr, TESTSIZE, MS_SYNC);

        client();

    }else{
        /* Initiator, open existing file */
        r = open(TESTFILE, O_RDWR);
        if(r<0){
            printf("client open() failed %d\n", errno);
            exit(1);
        }
        fd = r;
        mapfd();
        initiator();
    }
    return 0;
}
