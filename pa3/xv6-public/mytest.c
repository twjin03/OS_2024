#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "syscall.h"


#define MMAPBASE 0x40000000 //pa3

int main(int argc, char **argv){
int size = 8192;
int fd = open("README", O_RDONLY);
printf(1,"freemem now is %d\n",freemem());
char* text = (char*)mmap(0, size, PROT_READ, MAP_POPULATE, fd, 0);
printf(1, "mmap return is: %d\n", text);
printf(1,"freemem now is %d\n",freemem());
int ret = munmap(0 + MMAPBASE);
printf(1, "munmap return is: %d\n", ret);
printf(1,"freemem now is %d\n",freemem());
printf(1,"\n");
  return 0;
}