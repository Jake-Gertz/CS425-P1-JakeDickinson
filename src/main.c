#include "lab.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef TEST
#define main main_exclude
#endif

/* Exit statuses required by the assignment. */
#define EXIT_QUEUED 0
#define EXIT_USAGE 1
#define EXIT_SESSION 2

static void usage(FILE *out)
{
  fputs("Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
        "             [-H helo-host] <server>\n"
        "\n"
        "  -f <from>        envelope sender, for example you@example.com\n"
        "  -t <to>          envelope recipient\n"
        "  -s <subject>     subject line (default: empty)\n"
        "  -b <body>        message body (default: read from stdin)\n"
        "  -p <port>        port or service name (default: 25)\n"
        "  -H <helo-host>   host name sent with HELO (default: localhost)\n"
        "  <server>         host name or address of the mail server\n",
        out);
}

/**
 * Reject a value that would let the caller inject a command or header.
 */
static int check_value(const char *what, const char *value)
{
  if (!smtp_is_safe_value(value))
  {
    fprintf(stderr, "myapp: %s must not contain a CR or LF character\n", what);
    return -1;
  }
  return 0;
}

int main(int argc, char *argv[])
{
  /* No arguments at all is a request for help, not a mistake. */
  if (argc == 1)
  {
    usage(stdout);
    return EXIT_QUEUED;
  }

  const char *from = NULL;
  const char *to = NULL;
  const char *subject = "";
  const char *body = NULL;
  const char *port = SMTP_DEFAULT_PORT;
  const char *helo_host = SMTP_DEFAULT_HELO;

  int opt;
  while ((opt = getopt(argc, argv, "f:t:s:b:p:H:")) != -1)
  {
    switch (opt)
    {
    case 'f':
      from = optarg;
      break;
    case 't':
      to = optarg;
      break;
    case 's':
      subject = optarg;
      break;
    case 'b':
      body = optarg;
      break;
    case 'p':
      port = optarg;
      break;
    case 'H':
      helo_host = optarg;
      break;
    default: /* '?' for an unknown option or a missing argument */
      usage(stderr);
      return EXIT_USAGE;
    }
  }

  if (optind != argc - 1)
  {
    fprintf(stderr, "myapp: expected exactly one <server> argument\n");
    usage(stderr);
    return EXIT_USAGE;
  }
  const char *server = argv[optind];

  if (from == NULL || to == NULL)
  {
    fprintf(stderr, "myapp: -f <from> and -t <to> are required\n");
    usage(stderr);
    return EXIT_USAGE;
  }

  if (check_value("-f <from>", from) < 0 || check_value("-t <to>", to) < 0 ||
      check_value("-s <subject>", subject) < 0 ||
      check_value("-H <helo-host>", helo_host) < 0)
  {
    return EXIT_USAGE;
  }

  /* The body comes from stdin unless -b was given. */
  char *stdin_body = NULL;
  if (body == NULL)
  {
    stdin_body = smtp_read_stream(stdin);
    if (stdin_body == NULL)
    {
      fprintf(stderr, "myapp: failed to read the message body from stdin\n");
      return EXIT_USAGE;
    }
    body = stdin_body;
  }

  /* A server that hangs up mid-write must produce an error, not a signal. */
  signal(SIGPIPE, SIG_IGN);

  char err[SMTP_ERR_MAX];
  int fd = smtp_connect(server, port, err, sizeof err);
  if (fd < 0)
  {
    fprintf(stderr, "myapp: %s\n", err);
    free(stdin_body);
    return EXIT_SESSION;
  }

  smtp_session_t session;
  smtp_session_init(&session, smtp_socket_read, smtp_socket_write, &fd);

  smtp_mail_t mail;
  mail.helo_host = helo_host;
  mail.from = from;
  mail.to = to;
  mail.subject = subject;
  mail.body = body;

  int rc = smtp_send_mail(&session, &mail);

  smtp_close(fd);
  free(stdin_body);

  if (rc < 0)
  {
    fprintf(stderr, "myapp: %s\n", session.error);
    return EXIT_SESSION;
  }

  printf("Message from %s to %s queued by %s\n", from, to, server);
  return EXIT_QUEUED;
}
