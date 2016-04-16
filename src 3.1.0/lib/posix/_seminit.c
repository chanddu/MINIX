#include <lib.h>
#define seminit	_seminit
#include <unistd.h>

PUBLIC pid_t seminit(start_value)
int start_value;
{
  message m;
  m.m1_i1 = start_value;
  return(_syscall(MM, SEMINIT, &m));
}
