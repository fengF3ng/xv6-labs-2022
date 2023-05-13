#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2) {
    char *s = "sleep: missing operand\n";
    write(2, s, strlen(s));
  }
  sleep(atoi(argv[1]));
  exit(0);
}