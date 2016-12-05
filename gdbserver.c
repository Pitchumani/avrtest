#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>

extern char *inet_ntoa (struct in_addr __in);
extern void bzero (void *__s, size_t __n);

int newsockfd;

int
run_gdbserver (int portnumber)
{
  return 0;
}

