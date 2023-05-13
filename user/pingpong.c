#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 1) {
    char *s = "ping pong: missing operand\n";
    write(2, s, strlen(s));
  }

  int ping[2], pong[2];

  pipe(ping);
  pipe(pong);

  char buf[32];

  if(fork() == 0) {
    close(ping[1]);
    close(pong[0]);
    write(pong[1], "pong", 4);
    read(ping[0], buf, 32);
    close(ping[0]);
    close(pong[1]);
  } else {
    close(ping[0]);
    close(pong[1]);
    write(ping[1], "ping", 4);
    read(pong[0], buf, 32);
    close(ping[1]);
    close(pong[0]);
  }

  wait(0);
  printf("%d: received %s\n", getpid(), buf);
  exit(0);
}