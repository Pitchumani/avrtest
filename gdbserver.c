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

int newsockfd, process = 0;
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
write_general_registers ()
{
  gdb_reply ("");
  return OK;
}

void
read_memory ()
{
}

void
read_general_registers (void)
{
}

void
write_memory ()
{
}

void
resume_execution ()
{
}

struct target_sim_features {
  int id;
  char *feature;
  char *support;
  int len;
} features[] = {
{0, "multiprocess", "-", 13},
{1, "swbreak", "+", 8},
{2, "hwbreak", "-", 8},
{3, "qRelocInsn", "-", 11}};

void
respond_packet (const char *response)
{
  char response_buf[BUFSIZE] = "";
  int count = 0;
  int checksum = 0;

  response_buf[count++] = '$';

  while (response)
  {
    checksum += (unsigned char)*response;
    response_buf[count++] = *response;
    response++;
  }
  response_buf[count++] = '#';
  response_buf[count++] = hex_digits[((checksum >> 4) & 0xf)];
  response_buf[count++] = hex_digits[(checksum & 0xff)];

  write (newsockfd, response_buf, count);
}

enum query_type {
  qATTACHED,
  qSUPPORTED,
  qSYMBOL,
  qTSTATUS,
  qXFER,
  qFTHREADINFO,
  qSTHREADINFO,
  qUNKNOWN
};

enum query_type
get_query_type ()
{
  int i = 1, j = 0;
  char ch;
  char qstr[20]="";
  while (buffer [i])
  {
    if ((buffer[i] == ':') || (buffer[i] == ';') || (buffer[i] == ','))
      break;
    qstr[j++] = buffer[i++];
  }

  if (!strncmp (qstr, "Supported", 9))
    return qSUPPORTED;
  if (!strncmp (qstr, "TStatus", 7))
    return qTSTATUS;
  if (!strncmp (qstr, "Attached", 8))
    return qATTACHED;
  if (!strncmp (qstr, "fThreadInfo", 11))
    return qFTHREADINFO;
  if (!strncmp (qstr, "sThreadInfo", 11))
    return qSTHREADINFO;
  if (!strncmp (qstr, "Symbol", 6))
    return qSYMBOL;
  if (!strncmp (qstr, "Xfer", 4))
    return qXFER;

  return qUNKNOWN;
}

int
process_set_packets ()
{
  gdb_reply ("");
  return OK;
}

int
process_query_packets ()
{
  /*
    'qSupported [:gdbfeature [;gdbfeature]... ]'
   reply:
     '' or 'stubfeature [;stubfeature]...'
  */
  enum query_type qtype = get_query_type ();
  fprintf (stderr, "Process query packets... (%d)\n", qtype);
  switch (qtype)
  {
    case qATTACHED:
      if (process)
      {
        gdb_reply ("1");
        gslog ("Attached to existing process");
      }
      else
      {
        gdb_reply ("0");
        gslog ("Create new process");
        process = 1;
      }
      return OK;
    case qSUPPORTED:
      gdb_reply ("PacketSize=255");
      gslog ("Sent features supported.");
      return OK;
    case qFTHREADINFO:
      gdb_reply ("m1");
      return OK;
    case qSTHREADINFO:
      gdb_reply ("1");
      return OK;
    case qSYMBOL:
      gdb_reply ("");
      gslog ("sent empty reply.");
      return OK;
    case qTSTATUS:
      gdb_reply ("");//tunknown:0");//gdb_reply ("T0"); //gdb_reply ("tnotrun:0");
      gslog ("Sent reply that trace is presently not running."); //gslog ("Sent reply no trace run yet.");
      return OK;
    case qXFER:
      gdb_reply ("");
      gslog ("sent empty reply.");
      return OK;
    default:
      return NOTOK;
  }
}

void
set_thread ()
{
  switch (buffer[1])
  {
    case 'g':
    case 'G':
      gdb_reply ("OK");
      break;
    case 'm':
    case 'M':
      gdb_reply ("");
      break;
    case 'c':
      gdb_reply ("E99");  // not supported
      break;
  }
}

int
gdb_process_packets ()
{
  char ch = buffer[0];

  switch (ch)
  {
    case 'c': // 'c [addr]'
      // Continue at addr, which is the address to resume. If addr is omitted, resume at current address.
      resume_execution ();
      break;

    case 'g':
      // 'g'
      // ‘XX...’ or 'E NN'
      read_general_registers ();
      break;

    case 'G':
      // 'G XX...'
      // OK or E NN
      write_general_registers ();
      break;

    case 'H':
      // H op thread-id
      //  op - m,M,g,G
      set_thread ();
    case 'm': // 'm addr,length'
      // Read length addressable memory units starting at address addr
      read_memory ();
      break;

    case 'M': // 'M addr,length:XX...'
      // Write length addressable memory units starting at address addr
      write_memory ();
      break;

    case 'q':
      process_query_packets ();
      break;

    case 'Q':
      process_set_packets ();
      break;

    case 's':
      break;

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

