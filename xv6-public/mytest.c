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
}


int main(void) {
  // Test getnice and setnice system calls
  printf(1, "Testing getnice and setnice:\n");
  test_get_set_nice();
  
  // Test ps system call
  printf(1, "\nTesting ps system call:\n");
  test_ps();
  
  exit();
}


