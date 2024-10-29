#include "types.h"
#include "stat.h"
#include "user.h"


void test_get_set_nice(void) {
  int pid = getpid();
  
  printf(1, "Initial nice value of process with PID %d: %d\n", pid, getnice(pid));

  // Change the nice value to 15
  setnice(pid, 15);
  printf(1, "Nice value after setting to 15: %d\n", getnice(pid));

  // Try setting an invalid nice value (-1)
  if(setnice(pid, -1) == -1) {
    printf(1, "Error: invalid nice value -1\n");
  }

  // Try setting an invalid nice value (40)
  if(setnice(pid, 40) == -1) {
    printf(1, "Error: invalid nice value 40\n");
  }

  // Restore to the default nice value 20
  setnice(pid, 20);
  printf(1, "Restored nice value: %d\n", getnice(pid));
}

void test_ps(void) {
  int pid = fork();
  
  if(pid == 0) {
    // Child process
    printf(1, "Child process information:\n");
    ps(0); // print all processes
    exit();
  } else {
    // Parent process
    wait();
    printf(1, "Parent process information:\n");
    ps(getpid()); // print only parent process info
  }
  
  printf(1,"\n\n\n");
}

/*
int main(void) {
  // Test getnice and setnice system calls
  printf(1, "Testing getnice and setnice:\n");
  test_get_set_nice();
  
  // Test ps system call
  printf(1, "\nTesting ps system call:\n");
  test_ps();
  
  exit();
}
*/


int main ()
{
    printf(1, "\nBEFORE START Testing ps system call:\n");
    test_ps();
    
    int pid1 = fork();
    int a = 1;
    int range = 10000;


    if(pid1>0){

        setnice(getpid(),0);
        for(int i = 0; i< range; i++){
            for(int j = 0; j< range; j++){
            a = 9.9+9.9*a;
            }
        }
        printf(1,"ans: %d\n",a);

    }
    else if(pid1 == 0){
        setnice(getpid(),10);
        for(int i = 0; i< range; i++){
            for(int j = 0; j< range; j++){
            a = 9.9+9.9*a;
            }
        }
        printf(1,"ans: %d\n",a);
    }
    printf(1, "\nBEFORE EXIT Testing ps system call:\n");
    test_ps();
    exit();
}




/*
int main ()
{
    setnice(getpid(),5);
    int pid1 = fork();
    int a = 1;
    int range = 2000;
    
    printf(1, "\nBEFORE START Testing ps system call:\n");
    test_ps();
    if(pid1>0){
        int pid2 = fork();
        if(pid2>0){
            int pid3 = fork();

            if(pid3>0){
                wait();
                for(int i = 0; i< range; i++){
                    for(int j = 0; j< range; j++){
                        a = 9.9+9.9*a;
                    }
                }
                printf(1,"%d - ans: %d\n",getpid(),a);
            }
            else if (pid3==0){
                setnice(getpid(),15);
                for(int i = 0; i< range; i++){
                    for(int j = 0; j< range; j++){
                        a = 9.9+9.9*a;
                    }
                }
                printf(1,"%d - ans: %d\n",getpid(),a);
            }
        }
        else if(pid2 == 0){
            setnice(getpid(),10);
            for(int i = 0; i< range; i++){
                for(int j = 0; j< range; j++){
                a = 9.9+9.9*a;
                }
            }
            printf(1,"%d - ans: %d\n",getpid(),a);
        }
    }
    else if(pid1 == 0){
        setnice(getpid(),5);
        for(int i = 0; i< range; i++){
            for(int j = 0; j< range; j++){
            a = 9.9+9.9*a;
            }
        }
        printf(1,"%d - ans: %d\n",getpid(),a);
    }
    printf(1, "\nBEFORE EXIT Testing ps system call:\n");
    test_ps();
    
    
    
    exit();
}
*/



