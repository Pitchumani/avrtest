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

#define BUFSIZE 255
#define OK		0
#define NOTOK	1

int newsockfd;
char buffer [BUFSIZE];
char rbuffer [BUFSIZE];

void error (const char *msg) {
  perror (msg);
  exit (1);
}
void gslog (const char *msg) {
  fprintf (stderr, msg);
  fprintf (stderr, "\n");
}

char hex_digits[] = "0123456789ABCDEF";

unsigned char
hex2dec (char c)
{
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  return c - '0';
}

void
gdb_reply (char *rstr)
{
  int i = 1;
  int lchksum = 0;
  if (rstr == NULL) return;
  bzero (rbuffer, BUFSIZE);
  rbuffer [0] = '$';
  while (*rstr != '\0')
  {
    rbuffer [i++] = *rstr;
    lchksum += (unsigned char)*rstr;
    rstr++;
  }
  rbuffer[i++] = '#';
  rbuffer[i++] = hex_digits[(lchksum >> 4) & 0xf];
  rbuffer[i++] = hex_digits[lchksum & 0xf];

  fprintf (stderr, "Sending packet: %s\n", rbuffer);
  write (newsockfd, rbuffer, i);
}

void
resend_packet (void)
{
}

int
gdb_process_notifications ()
{
  return OK;
}

int
gdb_process_packets ()
{
  char ch = buffer[0];

  switch (ch)
  {
    case 'q':
    default:
      gdb_reply ("");
      break;
  }
  return OK;
}


int
run_gdbserver (int portnumber)
{
  int sockfd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_len;

  sockfd = socket (AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    error ("Error opening socket");

  bzero((char *) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons (portnumber);

  if (bind (sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    error ("Error in binding");

  listen (sockfd, 5);
  client_len = sizeof(client_addr);

  printf ("Wait for gdb connection on port %d\n", portnumber);
  newsockfd = accept (sockfd, (struct sockaddr*)&client_addr, &client_len);
  if (newsockfd < 0)
    error ("Error on accept");
  close (sockfd);

  printf ("Remote debugging from host %s\n", inet_ntoa (client_addr.sin_addr));

  bzero (buffer, BUFSIZE);
  while (1) {
    char c = '\0';
    int len = 0;
    unsigned char lchksum = 0, chksum_got;
    bzero (buffer, BUFSIZE);
    read (newsockfd, &c, 1);
    if (c == '\0')
      continue;

    switch (c)
    {
      case '$':
        read (newsockfd, &c, 1);
        while (c != '#')
        {
          buffer[len++] = c;
          lchksum += c;
          read (newsockfd, &c, 1);
        }
        read (newsockfd, &c, 1);
        chksum_got = hex2dec (c) << 4;
        read (newsockfd, &c, 1);
        chksum_got |= hex2dec (c);
        // Ask gdb to resend packet if checksum not correct
        if (chksum_got != lchksum)
        {
          fprintf (stderr, "Packet error. Checksum 0x%X. expected 0x%X\n", chksum_got, lchksum);
          gslog ("Request to resend packet");
          write (newsockfd, "-", 1);
        }
        else
        {
          fprintf (stderr, "Packet received (%s). checksum: 0x%X\n", buffer, chksum_got);
          // Acknowledge that packet received
          write (newsockfd, "+", 1);

          // process packets
          gdb_process_packets ();
        }
        break;

      case '%':
        // acknowledge
        write (newsockfd, "+", 1);
        gdb_process_notifications ();
        break;

      case '+':
        /* Do nothing for acknowledgement */
        break;

      case '-':
        resend_packet ();
        break;

      default:
        break;
    }

  }

  return OK;
}

