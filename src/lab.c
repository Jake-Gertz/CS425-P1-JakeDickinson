#include "lab.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Linux has MSG_NOSIGNAL; fall back to 0 elsewhere (main also ignores
 * SIGPIPE, so a hangup can never kill the client either way). */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ======================================================================== */
/* Small private helpers                                                    */
/* ======================================================================== */

/**
 * Concatenate n strings into one freshly malloc'd string.
 */
static char *join_strings(const char *const *parts, size_t n)
{
  size_t total = 0;
  for (size_t i = 0; i < n; i++)
  {
    total += strlen(parts[i]);
  }

  char *out = malloc(total + 1);
  if (out == NULL) // GCOVR_EXCL_START
  {
    return NULL;
  } // GCOVR_EXCL_STOP

  size_t pos = 0;
  for (size_t i = 0; i < n; i++)
  {
    size_t len = strlen(parts[i]);
    memcpy(out + pos, parts[i], len);
    pos += len;
  }
  out[pos] = '\0';
  return out;
}

/**
 * printf into the session's error buffer.
 */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void session_error(smtp_session_t *s, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s->error, sizeof s->error, fmt, ap);
  va_end(ap);
}

/**
 * Turn "reason" into "step: reason" in place so the caller knows which
 * part of the conversation failed.
 */
static void prefix_error(smtp_session_t *s, const char *step)
{
  char reason[SMTP_ERR_MAX];
  memcpy(reason, s->error, sizeof reason);
  session_error(s, "%s: %s", step, reason);
}

/**
 * printf into an optional caller supplied buffer (used by layer 3).
 */
#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void set_error(char *err, size_t errlen, const char *fmt, ...)
{
  if (err == NULL || errlen == 0)
  {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errlen, fmt, ap);
  va_end(ap);
}

/* ======================================================================== */
/* Layer 1: pure protocol helpers                                           */
/* ======================================================================== */

int smtp_parse_code(const char *line)
{
  if (line == NULL)
  {
    return -1;
  }

  /* isdigit('\0') is false, so a short string stops the scan safely. */
  for (int i = 0; i < 3; i++)
  {
    if (!isdigit((unsigned char)line[i]))
    {
      return -1;
    }
  }

  /* RFC 5321 reply codes are 2xx..5xx; anything with a leading 0 is junk. */
  if (line[0] == '0')
  {
    return -1;
  }

  char sep = line[3];
  if (sep != ' ' && sep != '-' && sep != '\r' && sep != '\n' && sep != '\0')
  {
    return -1;
  }

  return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
}

bool smtp_reply_is_final(const char *line)
{
  if (smtp_parse_code(line) < 0)
  {
    return false;
  }
  /* smtp_parse_code guaranteed line[3] is one of " -\r\n\0". */
  return line[3] != '-';
}

char *smtp_build_command(smtp_cmd_t cmd, const char *arg)
{
  /* Built by concatenation rather than a format table so the compiler can
   * still check every format string in this file (-Wformat=2). */
  const char *prefix;
  const char *suffix;
  bool needs_arg;

  switch (cmd)
  {
  case SMTP_CMD_HELO:
    prefix = "HELO ";
    suffix = "\r\n";
    needs_arg = true;
    break;
  case SMTP_CMD_MAIL:
    prefix = "MAIL FROM:<";
    suffix = ">\r\n";
    needs_arg = true;
    break;
  case SMTP_CMD_RCPT:
    prefix = "RCPT TO:<";
    suffix = ">\r\n";
    needs_arg = true;
    break;
  case SMTP_CMD_DATA:
    prefix = "DATA";
    suffix = "\r\n";
    needs_arg = false;
    break;
  case SMTP_CMD_QUIT:
    prefix = "QUIT";
    suffix = "\r\n";
    needs_arg = false;
    break;
  default:
    return NULL;
  }

  if (needs_arg)
  {
    if (arg == NULL)
    {
      return NULL;
    }
    const char *parts[] = {prefix, arg, suffix};
    return join_strings(parts, 3);
  }

  const char *parts[] = {prefix, suffix};
  return join_strings(parts, 2);
}

char *smtp_dot_stuff(const char *body)
{
  if (body == NULL)
  {
    return NULL;
  }

  /* Worst case every byte doubles (LF -> CRLF, "." -> "..") plus a final
   * CRLF and the terminator. */
  size_t len = strlen(body);
  char *out = malloc(len * 2 + 3);
  if (out == NULL) // GCOVR_EXCL_START
  {
    return NULL;
  } // GCOVR_EXCL_STOP

  size_t pos = 0;
  bool line_start = true;
  for (size_t i = 0; i < len; i++)
  {
    char c = body[i];

    if (c == '\r' && body[i + 1] == '\n')
    {
      /* Already CRLF: let the '\n' branch below emit the pair. */
      continue;
    }
    if (c == '\n')
    {
      out[pos++] = '\r';
      out[pos++] = '\n';
      line_start = true;
      continue;
    }
    if (line_start && c == '.')
    {
      out[pos++] = '.';
    }
    out[pos++] = c;
    line_start = false;
  }

  /* A body that does not end in a newline still needs one before ".". */
  if (!line_start)
  {
    out[pos++] = '\r';
    out[pos++] = '\n';
  }
  out[pos] = '\0';
  return out;
}

char *smtp_build_data(const char *from, const char *to, const char *subject,
                      const char *body)
{
  if (from == NULL || to == NULL || subject == NULL || body == NULL)
  {
    return NULL;
  }

  char *stuffed = smtp_dot_stuff(body);
  if (stuffed == NULL) // GCOVR_EXCL_START
  {
    return NULL;
  } // GCOVR_EXCL_STOP

  const char *parts[] = {"From: ", from,    "\r\nTo: ",   to,
                         "\r\nSubject: ",   subject,      "\r\n\r\n",
                         stuffed,           ".\r\n"};
  char *payload = join_strings(parts, sizeof parts / sizeof parts[0]);
  free(stuffed);
  return payload;
}

bool smtp_is_safe_value(const char *value)
{
  if (value == NULL)
  {
    return false;
  }
  return strpbrk(value, "\r\n") == NULL;
}

char *smtp_read_stream(FILE *fp)
{
  if (fp == NULL)
  {
    return NULL;
  }

  size_t cap = 4096;
  size_t len = 0;
  char *buf = malloc(cap);
  if (buf == NULL) // GCOVR_EXCL_START
  {
    return NULL;
  } // GCOVR_EXCL_STOP

  for (;;)
  {
    size_t n = fread(buf + len, 1, cap - len - 1, fp);
    len += n;
    if (len == cap - 1)
    {
      cap *= 2;
      char *bigger = realloc(buf, cap);
      if (bigger == NULL) // GCOVR_EXCL_START
      {
        free(buf);
        return NULL;
      } // GCOVR_EXCL_STOP
      buf = bigger;
      continue;
    }
    if (n == 0)
    {
      break;
    }
  }

  if (ferror(fp))
  {
    free(buf);
    return NULL;
  }

  buf[len] = '\0';
  return buf;
}

/* ======================================================================== */
/* Layer 2: the session                                                     */
/* ======================================================================== */

void smtp_session_init(smtp_session_t *s, smtp_read_fn read,
                       smtp_write_fn write, void *ctx)
{
  if (s == NULL)
  {
    return;
  }
  s->read = read;
  s->write = write;
  s->ctx = ctx;
  s->len = 0;
  s->error[0] = '\0';
}

int smtp_read_line(smtp_session_t *s, char *out, size_t outlen)
{
  if (s == NULL || out == NULL || outlen < SMTP_LINE_MAX)
  {
    return SMTP_ERR_ARG;
  }

  for (;;)
  {
    /* Hand back a line if the buffer already holds a complete one. */
    char *nl = memchr(s->buf, '\n', s->len);
    if (nl != NULL)
    {
      size_t line_len = (size_t)(nl - s->buf); /* without the '\n' */
      size_t copy = line_len;
      if (copy > 0 && s->buf[copy - 1] == '\r')
      {
        copy--;
      }
      memcpy(out, s->buf, copy);
      out[copy] = '\0';

      size_t consumed = line_len + 1;
      memmove(s->buf, s->buf + consumed, s->len - consumed);
      s->len -= consumed;
      return (int)copy;
    }

    /* No complete line and no room to read more: the line is too long. */
    if (s->len >= sizeof s->buf)
    {
      session_error(s, "reply line longer than %d bytes", SMTP_LINE_MAX);
      return SMTP_ERR_LINE;
    }

    ssize_t n = s->read(s->ctx, s->buf + s->len, sizeof s->buf - s->len);
    if (n == 0)
    {
      session_error(s, "server closed the connection");
      return SMTP_ERR_EOF;
    }
    if (n < 0)
    {
      session_error(s, "read from server failed");
      return SMTP_ERR_IO;
    }
    s->len += (size_t)n;
  }
}

/**
 * Append line (plus a '\n' separator when acc is not empty) to *acc.
 */
static int append_line(char **acc, size_t *acc_len, const char *line,
                       size_t line_len)
{
  size_t extra = (*acc_len > 0) ? 1 : 0;
  char *bigger = realloc(*acc, *acc_len + extra + line_len + 1);
  if (bigger == NULL) // GCOVR_EXCL_START
  {
    return -1;
  } // GCOVR_EXCL_STOP
  *acc = bigger;
  if (extra)
  {
    (*acc)[*acc_len] = '\n';
  }
  memcpy(*acc + *acc_len + extra, line, line_len);
  *acc_len += extra + line_len;
  (*acc)[*acc_len] = '\0';
  return 0;
}

int smtp_read_reply(smtp_session_t *s, char **text)
{
  if (text != NULL)
  {
    *text = NULL;
  }
  if (s == NULL)
  {
    return SMTP_ERR_ARG;
  }

  char line[SMTP_LINE_MAX];
  char *acc = NULL;
  size_t acc_len = 0;
  int code = -1;

  for (;;)
  {
    int n = smtp_read_line(s, line, sizeof line);
    if (n < 0)
    {
      free(acc);
      return n;
    }

    int this_code = smtp_parse_code(line);
    if (this_code < 0)
    {
      session_error(s, "malformed reply from server: \"%s\"", line);
      free(acc);
      return SMTP_ERR_PROTO;
    }
    if (code < 0)
    {
      code = this_code;
    }
    else if (this_code != code)
    {
      session_error(s, "reply code changed from %d to %d mid-reply: \"%s\"",
                    code, this_code, line);
      free(acc);
      return SMTP_ERR_PROTO;
    }

    if (text != NULL)
    {
      if (append_line(&acc, &acc_len, line, (size_t)n) < 0) // GCOVR_EXCL_START
      {
        free(acc);
        session_error(s, "out of memory");
        return SMTP_ERR_IO;
      } // GCOVR_EXCL_STOP
    }

    if (smtp_reply_is_final(line))
    {
      break;
    }
  }

  if (text != NULL)
  {
    *text = acc;
  }
  return code;
}

int smtp_write_all(smtp_session_t *s, const char *data, size_t len)
{
  if (s == NULL || data == NULL)
  {
    return SMTP_ERR_ARG;
  }

  while (len > 0)
  {
    ssize_t n = s->write(s->ctx, data, len);
    if (n <= 0)
    {
      session_error(s, "write to server failed");
      return SMTP_ERR_IO;
    }
    data += n;
    len -= (size_t)n;
  }
  return SMTP_OK;
}

/**
 * Read one reply and require it to carry the expected code.
 */
static int expect_reply(smtp_session_t *s, int expected)
{
  char *text = NULL;
  int code = smtp_read_reply(s, &text);
  if (code < 0)
  {
    return code;
  }

  if (code != expected)
  {
    session_error(s, "expected %d, server replied: %s", expected, text);
    free(text);
    return SMTP_ERR_STATUS;
  }

  free(text);
  return SMTP_OK;
}

int smtp_command(smtp_session_t *s, const char *line, int expected)
{
  if (s == NULL || line == NULL)
  {
    return SMTP_ERR_ARG;
  }

  int rc = smtp_write_all(s, line, strlen(line));
  if (rc < 0)
  {
    return rc;
  }
  return expect_reply(s, expected);
}

/**
 * Build one command, send it and check the reply, labelling any error with
 * the command's name.
 */
static int run_step(smtp_session_t *s, const char *name, smtp_cmd_t cmd,
                    const char *arg, int expected)
{
  char *line = smtp_build_command(cmd, arg);
  if (line == NULL) // GCOVR_EXCL_START
  {
    session_error(s, "%s: out of memory", name);
    return SMTP_ERR_IO;
  } // GCOVR_EXCL_STOP

  int rc = smtp_command(s, line, expected);
  free(line);
  if (rc < 0)
  {
    prefix_error(s, name);
  }
  return rc;
}

int smtp_send_mail(smtp_session_t *s, const smtp_mail_t *mail)
{
  if (s == NULL || mail == NULL || mail->helo_host == NULL ||
      mail->from == NULL || mail->to == NULL || mail->subject == NULL ||
      mail->body == NULL)
  {
    return SMTP_ERR_ARG;
  }

  /* The server speaks first: verify the greeting before sending anything. */
  int rc = expect_reply(s, 220);
  if (rc < 0)
  {
    prefix_error(s, "greeting");
    return rc;
  }

  rc = run_step(s, "HELO", SMTP_CMD_HELO, mail->helo_host, 250);
  if (rc < 0)
  {
    return rc;
  }
  rc = run_step(s, "MAIL FROM", SMTP_CMD_MAIL, mail->from, 250);
  if (rc < 0)
  {
    return rc;
  }
  rc = run_step(s, "RCPT TO", SMTP_CMD_RCPT, mail->to, 250);
  if (rc < 0)
  {
    return rc;
  }
  rc = run_step(s, "DATA", SMTP_CMD_DATA, NULL, 354);
  if (rc < 0)
  {
    return rc;
  }

  char *payload = smtp_build_data(mail->from, mail->to, mail->subject,
                                  mail->body);
  if (payload == NULL) // GCOVR_EXCL_START
  {
    session_error(s, "message: out of memory");
    return SMTP_ERR_IO;
  } // GCOVR_EXCL_STOP
  rc = smtp_command(s, payload, 250);
  free(payload);
  if (rc < 0)
  {
    prefix_error(s, "message");
    return rc;
  }

  return run_step(s, "QUIT", SMTP_CMD_QUIT, NULL, 221);
}

/* ======================================================================== */
/* Layer 3: the socket transport                                            */
/* ======================================================================== */

int smtp_connect(const char *host, const char *port, char *err, size_t errlen)
{
  if (host == NULL || port == NULL)
  {
    set_error(err, errlen, "server host and port are required");
    return -1;
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6, whatever the name has */
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *results = NULL;
  int gai = getaddrinfo(host, port, &hints, &results);
  if (gai != 0)
  {
    set_error(err, errlen, "cannot resolve %s:%s: %s", host, port,
              gai_strerror(gai));
    return -1;
  }

  int fd = -1;
  int last_errno = 0;
  for (struct addrinfo *ai = results; ai != NULL; ai = ai->ai_next)
  {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) // GCOVR_EXCL_START
    {
      last_errno = errno;
      continue;
    } // GCOVR_EXCL_STOP
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
    {
      break;
    }
    last_errno = errno;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(results);

  if (fd < 0)
  {
    set_error(err, errlen, "cannot connect to %s:%s: %s", host, port,
              strerror(last_errno));
  }
  return fd;
}

ssize_t smtp_socket_read(void *ctx, char *buf, size_t len)
{
  if (ctx == NULL || buf == NULL)
  {
    return -1;
  }
  return recv(*(const int *)ctx, buf, len, 0);
}

ssize_t smtp_socket_write(void *ctx, const char *buf, size_t len)
{
  if (ctx == NULL || buf == NULL)
  {
    return -1;
  }
  return send(*(const int *)ctx, buf, len, MSG_NOSIGNAL);
}

void smtp_close(int fd)
{
  if (fd >= 0)
  {
    close(fd);
  }
}
