#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/lab.h"
#include "harness/unity.h"

void setUp(void) {}

void tearDown(void) {}

/* ======================================================================== */
/* Scripted in-memory transport                                             */
/* ======================================================================== */

/**
 * A fake server. Everything the "server" will ever say is in script; reads
 * hand it out at most chunk bytes at a time (0 = no limit) so a reply can be
 * made to arrive a few bytes at a time. Everything the client sends is
 * captured in sent so tests can assert on the exact bytes.
 */
typedef struct {
  const char *script;
  size_t script_len;
  size_t pos;
  size_t chunk;
  int read_error;
  char sent[8192];
  size_t sent_len;
  size_t write_chunk;
  int write_error;
} mock_t;

static void mock_init(mock_t *m, const char *script, size_t chunk) {
  memset(m, 0, sizeof *m);
  m->script = script;
  m->script_len = strlen(script);
  m->chunk = chunk;
}

static ssize_t mock_read(void *ctx, char *buf, size_t len) {
  mock_t *m = ctx;
  if (m->read_error) {
    return -1;
  }
  size_t remaining = m->script_len - m->pos;
  if (remaining == 0) {
    return 0; /* server hung up */
  }
  size_t n = remaining < len ? remaining : len;
  if (m->chunk != 0 && n > m->chunk) {
    n = m->chunk;
  }
  memcpy(buf, m->script + m->pos, n);
  m->pos += n;
  return (ssize_t)n;
}

static ssize_t mock_write(void *ctx, const char *buf, size_t len) {
  mock_t *m = ctx;
  if (m->write_error) {
    return -1;
  }
  size_t n = len;
  if (m->write_chunk != 0 && n > m->write_chunk) {
    n = m->write_chunk;
  }
  TEST_ASSERT_TRUE_MESSAGE(m->sent_len + n < sizeof m->sent,
                           "mock capture buffer overflow");
  memcpy(m->sent + m->sent_len, buf, n);
  m->sent_len += n;
  m->sent[m->sent_len] = '\0';
  return (ssize_t)n;
}

static void session_over(smtp_session_t *s, mock_t *m, const char *script,
                         size_t chunk) {
  mock_init(m, script, chunk);
  smtp_session_init(s, mock_read, mock_write, m);
}

/* The canonical happy-path conversation from the assignment. */
static const char *const HAPPY_REPLIES[] = {
    "220 smtp.example.com ESMTP ready\r\n",
    "250 smtp.example.com\r\n",
    "250 2.1.0 Ok\r\n",
    "250 2.1.5 Ok\r\n",
    "354 End data with <CR><LF>.<CR><LF>\r\n",
    "250 2.0.0 Ok: queued\r\n",
    "221 Bye\r\n",
};
#define HAPPY_COUNT (sizeof HAPPY_REPLIES / sizeof HAPPY_REPLIES[0])

static const smtp_mail_t TEST_MAIL = {
    .helo_host = "onyx.boisestate.edu",
    .from = "me@boisestate.edu",
    .to = "you@example.com",
    .subject = "hello",
    .body = "This is the message body.\n",
};

static const char EXPECTED_SENT[] =
    "HELO onyx.boisestate.edu\r\n"
    "MAIL FROM:<me@boisestate.edu>\r\n"
    "RCPT TO:<you@example.com>\r\n"
    "DATA\r\n"
    "From: me@boisestate.edu\r\n"
    "To: you@example.com\r\n"
    "Subject: hello\r\n"
    "\r\n"
    "This is the message body.\r\n"
    ".\r\n"
    "QUIT\r\n";

/**
 * Build a server script from HAPPY_REPLIES, optionally replacing one reply
 * and/or truncating after `count` replies. The result is malloc'd.
 */
static char *build_script(size_t count, size_t replace_idx,
                          const char *replacement) {
  char *script = malloc(1024);
  TEST_ASSERT_NOT_NULL(script);
  script[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    const char *line = (i == replace_idx) ? replacement : HAPPY_REPLIES[i];
    strcat(script, line);
  }
  return script;
}

/* ======================================================================== */
/* Layer 1: smtp_parse_code                                                 */
/* ======================================================================== */

void test_parse_code_valid_forms(void) {
  TEST_ASSERT_EQUAL_INT(220, smtp_parse_code("220 smtp.example.com ready"));
  TEST_ASSERT_EQUAL_INT(250, smtp_parse_code("250-PIPELINING"));
  TEST_ASSERT_EQUAL_INT(354, smtp_parse_code("354 End data with ."));
  TEST_ASSERT_EQUAL_INT(221, smtp_parse_code("221"));
  TEST_ASSERT_EQUAL_INT(250, smtp_parse_code("250\r\n"));
  TEST_ASSERT_EQUAL_INT(250, smtp_parse_code("250\n"));
  TEST_ASSERT_EQUAL_INT(550, smtp_parse_code("550 5.1.1 no such user"));
}

void test_parse_code_rejects_junk(void) {
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code(NULL));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code(""));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code("25"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code("2500 four digits"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code("25x Ok"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code("OK 250"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code("250:bad separator"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_code("042 leading zero"));
}

/* ======================================================================== */
/* Layer 1: smtp_reply_is_final                                             */
/* ======================================================================== */

void test_reply_is_final(void) {
  TEST_ASSERT_TRUE(smtp_reply_is_final("250 smtp.example.com"));
  TEST_ASSERT_TRUE(smtp_reply_is_final("250"));
  TEST_ASSERT_TRUE(smtp_reply_is_final("250\r\n"));
  TEST_ASSERT_FALSE(smtp_reply_is_final("250-smtp.example.com"));
  TEST_ASSERT_FALSE(smtp_reply_is_final("250-"));
  TEST_ASSERT_FALSE(smtp_reply_is_final("garbage"));
  TEST_ASSERT_FALSE(smtp_reply_is_final(NULL));
}

/* ======================================================================== */
/* Layer 1: smtp_build_command                                              */
/* ======================================================================== */

void test_build_command_each_verb(void) {
  char *line;

  line = smtp_build_command(SMTP_CMD_HELO, "onyx.boisestate.edu");
  TEST_ASSERT_EQUAL_STRING("HELO onyx.boisestate.edu\r\n", line);
  free(line);

  line = smtp_build_command(SMTP_CMD_MAIL, "me@boisestate.edu");
  TEST_ASSERT_EQUAL_STRING("MAIL FROM:<me@boisestate.edu>\r\n", line);
  free(line);

  line = smtp_build_command(SMTP_CMD_RCPT, "you@example.com");
  TEST_ASSERT_EQUAL_STRING("RCPT TO:<you@example.com>\r\n", line);
  free(line);

  line = smtp_build_command(SMTP_CMD_DATA, NULL);
  TEST_ASSERT_EQUAL_STRING("DATA\r\n", line);
  free(line);

  line = smtp_build_command(SMTP_CMD_QUIT, "ignored");
  TEST_ASSERT_EQUAL_STRING("QUIT\r\n", line);
  free(line);
}

void test_build_command_rejects_bad_input(void) {
  TEST_ASSERT_NULL(smtp_build_command(SMTP_CMD_HELO, NULL));
  TEST_ASSERT_NULL(smtp_build_command(SMTP_CMD_MAIL, NULL));
  TEST_ASSERT_NULL(smtp_build_command(SMTP_CMD_RCPT, NULL));
  TEST_ASSERT_NULL(smtp_build_command((smtp_cmd_t)99, "x"));
}

/* ======================================================================== */
/* Layer 1: smtp_dot_stuff                                                  */
/* ======================================================================== */

void test_dot_stuff_plain_body(void) {
  char *out = smtp_dot_stuff("line one\nline two\n");
  TEST_ASSERT_EQUAL_STRING("line one\r\nline two\r\n", out);
  free(out);
}

void test_dot_stuff_doubles_leading_dot(void) {
  char *out = smtp_dot_stuff(".\n..\n.hidden\nnot.hidden\n");
  TEST_ASSERT_EQUAL_STRING("..\r\n...\r\n..hidden\r\nnot.hidden\r\n", out);
  free(out);
}

void test_dot_stuff_adds_missing_final_newline(void) {
  char *out = smtp_dot_stuff("no newline at end");
  TEST_ASSERT_EQUAL_STRING("no newline at end\r\n", out);
  free(out);

  out = smtp_dot_stuff(".");
  TEST_ASSERT_EQUAL_STRING("..\r\n", out);
  free(out);
}

void test_dot_stuff_keeps_existing_crlf(void) {
  char *out = smtp_dot_stuff("a\r\nb\r\n.c\r\n");
  TEST_ASSERT_EQUAL_STRING("a\r\nb\r\n..c\r\n", out);
  free(out);

  /* A lone CR that is not part of CRLF is left alone. */
  out = smtp_dot_stuff("a\rb\n");
  TEST_ASSERT_EQUAL_STRING("a\rb\r\n", out);
  free(out);
}

void test_dot_stuff_empty_and_null(void) {
  char *out = smtp_dot_stuff("");
  TEST_ASSERT_EQUAL_STRING("", out);
  free(out);

  out = smtp_dot_stuff("\n\n");
  TEST_ASSERT_EQUAL_STRING("\r\n\r\n", out);
  free(out);

  TEST_ASSERT_NULL(smtp_dot_stuff(NULL));
}

/* ======================================================================== */
/* Layer 1: smtp_build_data                                                 */
/* ======================================================================== */

void test_build_data_layout(void) {
  char *payload = smtp_build_data("me@boisestate.edu", "you@example.com",
                                  "hello", "This is the message body.\n");
  TEST_ASSERT_EQUAL_STRING("From: me@boisestate.edu\r\n"
                           "To: you@example.com\r\n"
                           "Subject: hello\r\n"
                           "\r\n"
                           "This is the message body.\r\n"
                           ".\r\n",
                           payload);
  free(payload);
}

void test_build_data_stuffs_body_and_allows_empty_subject(void) {
  char *payload = smtp_build_data("a@b", "c@d", "", ".starts with dot");
  TEST_ASSERT_EQUAL_STRING("From: a@b\r\n"
                           "To: c@d\r\n"
                           "Subject: \r\n"
                           "\r\n"
                           "..starts with dot\r\n"
                           ".\r\n",
                           payload);
  free(payload);

  payload = smtp_build_data("a@b", "c@d", "empty body", "");
  TEST_ASSERT_EQUAL_STRING("From: a@b\r\nTo: c@d\r\nSubject: empty body\r\n"
                           "\r\n.\r\n",
                           payload);
  free(payload);
}

void test_build_data_rejects_null(void) {
  TEST_ASSERT_NULL(smtp_build_data(NULL, "c@d", "s", "b"));
  TEST_ASSERT_NULL(smtp_build_data("a@b", NULL, "s", "b"));
  TEST_ASSERT_NULL(smtp_build_data("a@b", "c@d", NULL, "b"));
  TEST_ASSERT_NULL(smtp_build_data("a@b", "c@d", "s", NULL));
}

/* ======================================================================== */
/* Layer 1: smtp_is_safe_value                                              */
/* ======================================================================== */

void test_is_safe_value(void) {
  TEST_ASSERT_TRUE(smtp_is_safe_value("you@example.com"));
  TEST_ASSERT_TRUE(smtp_is_safe_value(""));
  TEST_ASSERT_TRUE(smtp_is_safe_value("a subject with spaces and .dots"));
  TEST_ASSERT_FALSE(smtp_is_safe_value("you@example.com\r\nRCPT TO:<x@y>"));
  TEST_ASSERT_FALSE(smtp_is_safe_value("subject\nBcc: someone@else"));
  TEST_ASSERT_FALSE(smtp_is_safe_value("bare\rcr"));
  TEST_ASSERT_FALSE(smtp_is_safe_value(NULL));
}

/* ======================================================================== */
/* Layer 1: smtp_read_stream                                                */
/* ======================================================================== */

static FILE *stream_with(const char *content, size_t len) {
  FILE *fp = tmpfile();
  TEST_ASSERT_NOT_NULL(fp);
  TEST_ASSERT_EQUAL_size_t(len, fwrite(content, 1, len, fp));
  rewind(fp);
  return fp;
}

void test_read_stream_small(void) {
  FILE *fp = stream_with("hello\nworld\n", 12);
  char *out = smtp_read_stream(fp);
  TEST_ASSERT_EQUAL_STRING("hello\nworld\n", out);
  free(out);
  fclose(fp);
}

void test_read_stream_empty(void) {
  FILE *fp = stream_with("", 0);
  char *out = smtp_read_stream(fp);
  TEST_ASSERT_EQUAL_STRING("", out);
  free(out);
  fclose(fp);
}

void test_read_stream_grows_buffer(void) {
  /* Larger than the initial 4096 byte buffer, so realloc must happen. */
  size_t len = 10000;
  char *big = malloc(len + 1);
  TEST_ASSERT_NOT_NULL(big);
  for (size_t i = 0; i < len; i++) {
    big[i] = (char)('a' + (i % 26));
  }
  big[len] = '\0';

  FILE *fp = stream_with(big, len);
  char *out = smtp_read_stream(fp);
  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_EQUAL_size_t(len, strlen(out));
  TEST_ASSERT_EQUAL_STRING(big, out);
  free(out);
  free(big);
  fclose(fp);
}

void test_read_stream_errors(void) {
  TEST_ASSERT_NULL(smtp_read_stream(NULL));

  /* Reading from a write-only stream fails and sets the error flag. */
  FILE *fp = fopen("/dev/null", "w");
  TEST_ASSERT_NOT_NULL(fp);
  TEST_ASSERT_NULL(smtp_read_stream(fp));
  fclose(fp);
}

/* ======================================================================== */
/* Layer 2: smtp_session_init                                               */
/* ======================================================================== */

void test_session_init(void) {
  smtp_session_t s;
  mock_t m;
  memset(&s, 0xff, sizeof s);
  smtp_session_init(&s, mock_read, mock_write, &m);
  TEST_ASSERT_EQUAL_PTR(mock_read, s.read);
  TEST_ASSERT_EQUAL_PTR(mock_write, s.write);
  TEST_ASSERT_EQUAL_PTR(&m, s.ctx);
  TEST_ASSERT_EQUAL_size_t(0, s.len);
  TEST_ASSERT_EQUAL_STRING("", s.error);

  smtp_session_init(NULL, mock_read, mock_write, &m); /* must not crash */
}

/* ======================================================================== */
/* Layer 2: smtp_read_line                                                  */
/* ======================================================================== */

void test_read_line_several_lines_in_one_read(void) {
  smtp_session_t s;
  mock_t m;
  char line[SMTP_LINE_MAX];
  session_over(&s, &m, "220 hello\r\n250 second\r\n\r\n\nbare lf\n", 0);

  TEST_ASSERT_EQUAL_INT(9, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("220 hello", line);
  TEST_ASSERT_EQUAL_INT(10, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("250 second", line);
  TEST_ASSERT_EQUAL_INT(0, smtp_read_line(&s, line, sizeof line)); /* CRLF */
  TEST_ASSERT_EQUAL_STRING("", line);
  TEST_ASSERT_EQUAL_INT(0, smtp_read_line(&s, line, sizeof line)); /* LF */
  TEST_ASSERT_EQUAL_STRING("", line);
  TEST_ASSERT_EQUAL_INT(7, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("bare lf", line);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_read_line(&s, line, sizeof line));
}

void test_read_line_split_across_reads(void) {
  smtp_session_t s;
  mock_t m;
  char line[SMTP_LINE_MAX];

  /* One byte per read. */
  session_over(&s, &m, "220 hello\r\n250 ok\r\n", 1);
  TEST_ASSERT_EQUAL_INT(9, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("220 hello", line);
  TEST_ASSERT_EQUAL_INT(6, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("250 ok", line);

  /* Chunks that straddle the CRLF. */
  session_over(&s, &m, "220 hello\r\n250 ok\r\n", 4);
  TEST_ASSERT_EQUAL_INT(9, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("220 hello", line);
  TEST_ASSERT_EQUAL_INT(6, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("250 ok", line);
}

void test_read_line_hangup_mid_line(void) {
  smtp_session_t s;
  mock_t m;
  char line[SMTP_LINE_MAX];
  session_over(&s, &m, "220 no newline here", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("server closed the connection", s.error);
}

void test_read_line_read_error(void) {
  smtp_session_t s;
  mock_t m;
  char line[SMTP_LINE_MAX];
  session_over(&s, &m, "220 hello\r\n", 0);
  m.read_error = 1;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("read from server failed", s.error);
}

void test_read_line_too_long_for_buffer(void) {
  smtp_session_t s;
  mock_t m;
  char line[SMTP_LINE_MAX];
  char *script = malloc(SMTP_LINE_MAX + 100);
  TEST_ASSERT_NOT_NULL(script);
  memset(script, 'x', SMTP_LINE_MAX + 99);
  script[SMTP_LINE_MAX + 99] = '\0';
  memcpy(script, "250 ", 4);

  session_over(&s, &m, script, 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_LINE, smtp_read_line(&s, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("reply line longer than 1024 bytes", s.error);

  /* Same thing arriving a few bytes at a time. */
  session_over(&s, &m, script, 7);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_LINE, smtp_read_line(&s, line, sizeof line));
  free(script);
}

void test_read_line_bad_arguments(void) {
  smtp_session_t s;
  mock_t m;
  char line[SMTP_LINE_MAX];
  session_over(&s, &m, "220 hello\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_read_line(NULL, line, sizeof line));
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_read_line(&s, NULL, sizeof line));
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_read_line(&s, line, 10));
}

/* ======================================================================== */
/* Layer 2: smtp_read_reply                                                 */
/* ======================================================================== */

void test_read_reply_single_line(void) {
  smtp_session_t s;
  mock_t m;
  char *text = NULL;
  session_over(&s, &m, "220 smtp.example.com ESMTP ready\r\n", 0);
  TEST_ASSERT_EQUAL_INT(220, smtp_read_reply(&s, &text));
  TEST_ASSERT_EQUAL_STRING("220 smtp.example.com ESMTP ready", text);
  free(text);
}

void test_read_reply_multi_line(void) {
  smtp_session_t s;
  mock_t m;
  char *text = NULL;
  session_over(&s, &m,
               "250-smtp.example.com\r\n"
               "250-PIPELINING\r\n"
               "250 SIZE 10240000\r\n"
               "354 next reply\r\n",
               0);
  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(&s, &text));
  TEST_ASSERT_EQUAL_STRING("250-smtp.example.com\n250-PIPELINING\n250 SIZE 10240000",
                           text);
  free(text);

  /* The following reply is untouched. */
  TEST_ASSERT_EQUAL_INT(354, smtp_read_reply(&s, &text));
  TEST_ASSERT_EQUAL_STRING("354 next reply", text);
  free(text);
}

void test_read_reply_multi_line_bytes_at_a_time(void) {
  smtp_session_t s;
  mock_t m;
  char *text = NULL;
  session_over(&s, &m, "250-a\r\n250-b\r\n250 c\r\n", 2);
  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(&s, &text));
  TEST_ASSERT_EQUAL_STRING("250-a\n250-b\n250 c", text);
  free(text);
}

void test_read_reply_without_text(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "250-a\r\n250 b\r\n", 0);
  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(&s, NULL));
}

void test_read_reply_malformed_line(void) {
  smtp_session_t s;
  mock_t m;
  char *text = (char *)0x1;
  session_over(&s, &m, "hello there\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTO, smtp_read_reply(&s, &text));
  TEST_ASSERT_NULL(text);
  TEST_ASSERT_EQUAL_STRING("malformed reply from server: \"hello there\"",
                           s.error);
}

void test_read_reply_code_changes_mid_reply(void) {
  smtp_session_t s;
  mock_t m;
  char *text = NULL;
  session_over(&s, &m, "250-first\r\n550 second\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTO, smtp_read_reply(&s, &text));
  TEST_ASSERT_NULL(text);
  TEST_ASSERT_EQUAL_STRING(
      "reply code changed from 250 to 550 mid-reply: \"550 second\"",
      s.error);
}

void test_read_reply_hangup_in_continuation(void) {
  smtp_session_t s;
  mock_t m;
  char *text = NULL;
  session_over(&s, &m, "250-first\r\n250-second\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_read_reply(&s, &text));
  TEST_ASSERT_NULL(text);
}

void test_read_reply_line_too_long(void) {
  smtp_session_t s;
  mock_t m;
  char *text = NULL;
  char *script = malloc(SMTP_LINE_MAX * 2);
  TEST_ASSERT_NOT_NULL(script);
  memset(script, 'y', SMTP_LINE_MAX * 2 - 1);
  script[SMTP_LINE_MAX * 2 - 1] = '\0';
  memcpy(script, "250-", 4);
  session_over(&s, &m, script, 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_LINE, smtp_read_reply(&s, &text));
  TEST_ASSERT_NULL(text);
  free(script);
}

void test_read_reply_bad_arguments(void) {
  char *text = (char *)0x1;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_read_reply(NULL, &text));
  TEST_ASSERT_NULL(text);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_read_reply(NULL, NULL));
}

/* ======================================================================== */
/* Layer 2: smtp_write_all                                                  */
/* ======================================================================== */

void test_write_all_single_write(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_write_all(&s, "HELO x\r\n", 8));
  TEST_ASSERT_EQUAL_STRING("HELO x\r\n", m.sent);
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_write_all(&s, "", 0));
  TEST_ASSERT_EQUAL_size_t(8, m.sent_len);
}

void test_write_all_short_writes(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "", 0);
  m.write_chunk = 3;
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_write_all(&s, "MAIL FROM:<a@b>\r\n", 17));
  TEST_ASSERT_EQUAL_STRING("MAIL FROM:<a@b>\r\n", m.sent);
}

void test_write_all_error(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "", 0);
  m.write_error = 1;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_write_all(&s, "QUIT\r\n", 6));
  TEST_ASSERT_EQUAL_STRING("write to server failed", s.error);
  TEST_ASSERT_EQUAL_size_t(0, m.sent_len);
}

void test_write_all_bad_arguments(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_write_all(NULL, "x", 1));
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_write_all(&s, NULL, 1));
}

/* ======================================================================== */
/* Layer 2: smtp_command                                                    */
/* ======================================================================== */

void test_command_success(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "250-hello\r\n250 ok\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_command(&s, "HELO x\r\n", 250));
  TEST_ASSERT_EQUAL_STRING("HELO x\r\n", m.sent);
  TEST_ASSERT_EQUAL_STRING("", s.error);
}

void test_command_wrong_code_reports_reply(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "550 5.1.1 <x@y>: Recipient address rejected\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_STATUS,
                        smtp_command(&s, "RCPT TO:<x@y>\r\n", 250));
  TEST_ASSERT_EQUAL_STRING(
      "expected 250, server replied: 550 5.1.1 <x@y>: Recipient address rejected",
      s.error);
}

void test_command_wrong_code_multi_line_reply(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "421-too busy\r\n421 try later\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_STATUS, smtp_command(&s, "DATA\r\n", 354));
  TEST_ASSERT_EQUAL_STRING(
      "expected 354, server replied: 421-too busy\n421 try later", s.error);
}

void test_command_write_failure(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "250 ok\r\n", 0);
  m.write_error = 1;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_command(&s, "HELO x\r\n", 250));
}

void test_command_hangup_before_reply(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_command(&s, "HELO x\r\n", 250));
  TEST_ASSERT_EQUAL_STRING("HELO x\r\n", m.sent);
}

void test_command_bad_arguments(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "250 ok\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_command(NULL, "HELO x\r\n", 250));
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_command(&s, NULL, 250));
}

/* ======================================================================== */
/* Layer 2: smtp_send_mail                                                  */
/* ======================================================================== */

void test_send_mail_happy_path(void) {
  smtp_session_t s;
  mock_t m;
  char *script = build_script(HAPPY_COUNT, HAPPY_COUNT, NULL);
  session_over(&s, &m, script, 0);

  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING(EXPECTED_SENT, m.sent);
  TEST_ASSERT_EQUAL_STRING("", s.error);
  free(script);
}

void test_send_mail_replies_a_few_bytes_at_a_time(void) {
  smtp_session_t s;
  mock_t m;
  char *script = build_script(HAPPY_COUNT, HAPPY_COUNT, NULL);

  session_over(&s, &m, script, 1);
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING(EXPECTED_SENT, m.sent);

  session_over(&s, &m, script, 5);
  m.write_chunk = 2;
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING(EXPECTED_SENT, m.sent);
  free(script);
}

void test_send_mail_all_replies_in_one_read(void) {
  /* A server that pipelines its whole script into one read burst. */
  smtp_session_t s;
  mock_t m;
  char *script = build_script(HAPPY_COUNT, HAPPY_COUNT, NULL);
  session_over(&s, &m, script, 0);
  /* Force the entire script into the buffer on the first read. */
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_size_t(strlen(script), m.pos);
  free(script);
}

void test_send_mail_follows_multi_line_replies(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m,
               "220-smtp.example.com ESMTP\r\n"
               "220 ready\r\n"
               "250-smtp.example.com\r\n"
               "250-PIPELINING\r\n"
               "250 SIZE 10240000\r\n"
               "250 2.1.0 Ok\r\n"
               "250 2.1.5 Ok\r\n"
               "354 End data with <CR><LF>.<CR><LF>\r\n"
               "250 2.0.0 Ok: queued\r\n"
               "221-closing\r\n"
               "221 Bye\r\n",
               3);
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING(EXPECTED_SENT, m.sent);
}

void test_send_mail_dot_stuffs_body(void) {
  smtp_session_t s;
  mock_t m;
  char *script = build_script(HAPPY_COUNT, HAPPY_COUNT, NULL);
  session_over(&s, &m, script, 0);

  smtp_mail_t mail = TEST_MAIL;
  mail.subject = "";
  mail.body = ".\n.. two\nplain";
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_send_mail(&s, &mail));
  TEST_ASSERT_NOT_NULL(strstr(m.sent, "Subject: \r\n\r\n"
                                      "..\r\n... two\r\nplain\r\n.\r\nQUIT\r\n"));
  free(script);
}

/**
 * Each wrong status code in the sequence, one at a time. The client must
 * stop right there: nothing after the failing step may be sent.
 */
void test_send_mail_each_wrong_status_code(void) {
  static const struct {
    const char *reply;
    const char *step;
    const char *sent_up_to; /* the last command that should have been sent */
  } cases[HAPPY_COUNT] = {
      {"554 5.3.2 no service here\r\n", "greeting: expected 220", ""},
      {"501 5.5.4 bad HELO\r\n", "HELO: expected 250", "HELO onyx.boisestate.edu\r\n"},
      {"550 5.1.8 sender rejected\r\n", "MAIL FROM: expected 250",
       "MAIL FROM:<me@boisestate.edu>\r\n"},
      {"550 5.1.1 no such user\r\n", "RCPT TO: expected 250",
       "RCPT TO:<you@example.com>\r\n"},
      {"451 4.3.0 try again later\r\n", "DATA: expected 354", "DATA\r\n"},
      {"552 5.2.2 too much mail data\r\n", "message: expected 250",
       "body.\r\n.\r\n"},
      {"500 5.5.1 unexpected\r\n", "QUIT: expected 221", "QUIT\r\n"},
  };

  for (size_t i = 0; i < HAPPY_COUNT; i++) {
    smtp_session_t s;
    mock_t m;
    char *script = build_script(HAPPY_COUNT, i, cases[i].reply);
    session_over(&s, &m, script, 0);

    TEST_ASSERT_EQUAL_INT(SMTP_ERR_STATUS, smtp_send_mail(&s, &TEST_MAIL));

    /* The error names the step, the expected code and the real reply. */
    TEST_ASSERT_NOT_NULL(strstr(s.error, cases[i].step));
    TEST_ASSERT_NOT_NULL(strstr(s.error, "server replied: "));
    TEST_ASSERT_EQUAL_STRING_LEN(cases[i].reply,
                                 strstr(s.error, "server replied: ") + 16,
                                 strlen(cases[i].reply) - 2);

    /* Nothing was sent after the failing step. */
    size_t sent_expected = (size_t)(strstr(EXPECTED_SENT, cases[i].sent_up_to) -
                                    EXPECTED_SENT) +
                           strlen(cases[i].sent_up_to);
    if (cases[i].sent_up_to[0] == '\0') {
      sent_expected = 0;
    }
    TEST_ASSERT_EQUAL_size_t(sent_expected, m.sent_len);
    TEST_ASSERT_EQUAL_STRING_LEN(EXPECTED_SENT, m.sent, sent_expected);
    free(script);
  }
}

/**
 * The server hangs up after each reply in turn.
 */
void test_send_mail_server_hangs_up_mid_session(void) {
  for (size_t count = 0; count < HAPPY_COUNT; count++) {
    smtp_session_t s;
    mock_t m;
    char *script = build_script(count, HAPPY_COUNT, NULL);
    session_over(&s, &m, script, 0);

    TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_send_mail(&s, &TEST_MAIL));
    TEST_ASSERT_NOT_NULL(strstr(s.error, "server closed the connection"));
    free(script);
  }
}

void test_send_mail_hangup_names_the_step(void) {
  smtp_session_t s;
  mock_t m;
  char *script = build_script(3, HAPPY_COUNT, NULL); /* dies after MAIL */
  session_over(&s, &m, script, 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING("RCPT TO: server closed the connection", s.error);
  free(script);

  session_over(&s, &m, "", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING("greeting: server closed the connection", s.error);
}

void test_send_mail_write_failure(void) {
  smtp_session_t s;
  mock_t m;
  char *script = build_script(HAPPY_COUNT, HAPPY_COUNT, NULL);
  session_over(&s, &m, script, 0);
  m.write_error = 1;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING("HELO: write to server failed", s.error);
  free(script);
}

void test_send_mail_malformed_greeting(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "Welcome to my server\r\n", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTO, smtp_send_mail(&s, &TEST_MAIL));
  TEST_ASSERT_EQUAL_STRING(
      "greeting: malformed reply from server: \"Welcome to my server\"",
      s.error);
  TEST_ASSERT_EQUAL_size_t(0, m.sent_len);
}

void test_send_mail_bad_arguments(void) {
  smtp_session_t s;
  mock_t m;
  session_over(&s, &m, "", 0);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(NULL, &TEST_MAIL));
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(&s, NULL));

  smtp_mail_t mail;

  mail = TEST_MAIL;
  mail.helo_host = NULL;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(&s, &mail));
  mail = TEST_MAIL;
  mail.from = NULL;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(&s, &mail));
  mail = TEST_MAIL;
  mail.to = NULL;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(&s, &mail));
  mail = TEST_MAIL;
  mail.subject = NULL;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(&s, &mail));
  mail = TEST_MAIL;
  mail.body = NULL;
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_send_mail(&s, &mail));

  TEST_ASSERT_EQUAL_size_t(0, m.sent_len);
}

/* ======================================================================== */
/* Layer 3: the socket transport (loopback only, no external network)       */
/* ======================================================================== */

/**
 * Open a listening TCP socket on 127.0.0.1 on an ephemeral port and return
 * it; the chosen port is written to port_str.
 */
static int listen_on_loopback(char *port_str, size_t port_len) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(listener >= 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr *)&addr, sizeof addr));
  TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));

  socklen_t len = sizeof addr;
  TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr *)&addr, &len));
  snprintf(port_str, port_len, "%u", (unsigned)ntohs(addr.sin_port));
  return listener;
}

void test_connect_and_talk_over_loopback(void) {
  char port[16];
  int listener = listen_on_loopback(port, sizeof port);

  char err[SMTP_ERR_MAX] = "";
  int client = smtp_connect("127.0.0.1", port, err, sizeof err);
  TEST_ASSERT_TRUE_MESSAGE(client >= 0, err);
  TEST_ASSERT_EQUAL_STRING("", err);

  int server = accept(listener, NULL, NULL);
  TEST_ASSERT_TRUE(server >= 0);

  /* Server -> client through the read callback. */
  TEST_ASSERT_EQUAL_INT(11, (int)send(server, "220 ready\r\n", 11, 0));
  char buf[64];
  TEST_ASSERT_EQUAL_INT(11, (int)smtp_socket_read(&client, buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING_LEN("220 ready\r\n", buf, 11);

  /* Client -> server through the write callback. */
  TEST_ASSERT_EQUAL_INT(6, (int)smtp_socket_write(&client, "QUIT\r\n", 6));
  TEST_ASSERT_EQUAL_INT(6, (int)recv(server, buf, sizeof buf, 0));
  TEST_ASSERT_EQUAL_STRING_LEN("QUIT\r\n", buf, 6);

  /* A closed peer reads as EOF (0), which the session reports as a hangup. */
  close(server);
  TEST_ASSERT_EQUAL_INT(0, (int)smtp_socket_read(&client, buf, sizeof buf));

  smtp_close(client);
  close(listener);
}

void test_full_session_over_real_socket(void) {
  /* The socket transport plugged into layer 2, against a loopback server
   * that answers from the happy script. */
  char port[16];
  int listener = listen_on_loopback(port, sizeof port);

  int client = smtp_connect("localhost", port, NULL, 0);
  if (client < 0) {
    /* Some sandboxes resolve localhost only to ::1; fall back to the IP. */
    client = smtp_connect("127.0.0.1", port, NULL, 0);
  }
  TEST_ASSERT_TRUE(client >= 0);
  int server = accept(listener, NULL, NULL);
  TEST_ASSERT_TRUE(server >= 0);

  smtp_session_t s;
  smtp_session_init(&s, smtp_socket_read, smtp_socket_write, &client);

  /* Drive the fake server step by step from this same thread. */
  char buf[1024];
  TEST_ASSERT_TRUE(send(server, HAPPY_REPLIES[0], strlen(HAPPY_REPLIES[0]), 0) > 0);
  char *text = NULL;
  TEST_ASSERT_EQUAL_INT(220, smtp_read_reply(&s, &text));
  free(text);

  TEST_ASSERT_TRUE(send(server, HAPPY_REPLIES[1], strlen(HAPPY_REPLIES[1]), 0) > 0);
  TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_command(&s, "HELO test\r\n", 250));
  TEST_ASSERT_EQUAL_INT(11, (int)recv(server, buf, sizeof buf, 0));
  TEST_ASSERT_EQUAL_STRING_LEN("HELO test\r\n", buf, 11);

  close(server);
  TEST_ASSERT_EQUAL_INT(SMTP_ERR_EOF, smtp_command(&s, "QUIT\r\n", 221));

  smtp_close(client);
  close(listener);
}

void test_connect_refused(void) {
  char port[16];
  int listener = listen_on_loopback(port, sizeof port);
  close(listener); /* nobody is listening there any more */

  char err[SMTP_ERR_MAX] = "";
  TEST_ASSERT_EQUAL_INT(-1, smtp_connect("127.0.0.1", port, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING_LEN("cannot connect to 127.0.0.1:", err, 28);
  TEST_ASSERT_NOT_NULL(strstr(err, "refused"));

  /* Callers that do not want a reason may pass no buffer. */
  TEST_ASSERT_EQUAL_INT(-1, smtp_connect("127.0.0.1", port, NULL, 0));
}

void test_connect_unresolvable(void) {
  char err[SMTP_ERR_MAX] = "";
  /* An unknown service name fails inside getaddrinfo without touching DNS. */
  TEST_ASSERT_EQUAL_INT(
      -1, smtp_connect("127.0.0.1", "no-such-service-xyz", err, sizeof err));
  TEST_ASSERT_EQUAL_STRING_LEN("cannot resolve 127.0.0.1:no-such-service-xyz: ",
                               err, 46);
  TEST_ASSERT_TRUE(strlen(err) > 46);
}

void test_connect_bad_arguments(void) {
  char err[SMTP_ERR_MAX] = "";
  TEST_ASSERT_EQUAL_INT(-1, smtp_connect(NULL, "25", err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("server host and port are required", err);
  err[0] = '\0';
  TEST_ASSERT_EQUAL_INT(-1, smtp_connect("localhost", NULL, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("server host and port are required", err);
  TEST_ASSERT_EQUAL_INT(-1, smtp_connect(NULL, NULL, NULL, 0));

  /* A zero length buffer is left untouched. */
  err[0] = '\0';
  TEST_ASSERT_EQUAL_INT(-1, smtp_connect(NULL, "25", err, 0));
  TEST_ASSERT_EQUAL_STRING("", err);
}

void test_socket_callbacks_reject_null(void) {
  int fd = 0;
  char buf[4];
  TEST_ASSERT_EQUAL_INT(-1, (int)smtp_socket_read(NULL, buf, sizeof buf));
  TEST_ASSERT_EQUAL_INT(-1, (int)smtp_socket_read(&fd, NULL, sizeof buf));
  TEST_ASSERT_EQUAL_INT(-1, (int)smtp_socket_write(NULL, buf, sizeof buf));
  TEST_ASSERT_EQUAL_INT(-1, (int)smtp_socket_write(&fd, NULL, sizeof buf));
}

void test_socket_callbacks_report_errors(void) {
  int pair[2];
  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
  close(pair[1]);

  /* Writing to a peer that has gone away fails instead of raising SIGPIPE. */
  TEST_ASSERT_EQUAL_INT(-1, (int)smtp_socket_write(&pair[0], "x", 1));
  smtp_close(pair[0]);

  /* A descriptor that is not open at all. */
  int bad = -1;
  char buf[4];
  TEST_ASSERT_EQUAL_INT(-1, (int)smtp_socket_read(&bad, buf, sizeof buf));

  smtp_close(-1); /* must be a harmless no-op */
}

/* ======================================================================== */

int main(void) {
  UNITY_BEGIN();

  /* Layer 1 */
  RUN_TEST(test_parse_code_valid_forms);
  RUN_TEST(test_parse_code_rejects_junk);
  RUN_TEST(test_reply_is_final);
  RUN_TEST(test_build_command_each_verb);
  RUN_TEST(test_build_command_rejects_bad_input);
  RUN_TEST(test_dot_stuff_plain_body);
  RUN_TEST(test_dot_stuff_doubles_leading_dot);
  RUN_TEST(test_dot_stuff_adds_missing_final_newline);
  RUN_TEST(test_dot_stuff_keeps_existing_crlf);
  RUN_TEST(test_dot_stuff_empty_and_null);
  RUN_TEST(test_build_data_layout);
  RUN_TEST(test_build_data_stuffs_body_and_allows_empty_subject);
  RUN_TEST(test_build_data_rejects_null);
  RUN_TEST(test_is_safe_value);
  RUN_TEST(test_read_stream_small);
  RUN_TEST(test_read_stream_empty);
  RUN_TEST(test_read_stream_grows_buffer);
  RUN_TEST(test_read_stream_errors);

  /* Layer 2 */
  RUN_TEST(test_session_init);
  RUN_TEST(test_read_line_several_lines_in_one_read);
  RUN_TEST(test_read_line_split_across_reads);
  RUN_TEST(test_read_line_hangup_mid_line);
  RUN_TEST(test_read_line_read_error);
  RUN_TEST(test_read_line_too_long_for_buffer);
  RUN_TEST(test_read_line_bad_arguments);
  RUN_TEST(test_read_reply_single_line);
  RUN_TEST(test_read_reply_multi_line);
  RUN_TEST(test_read_reply_multi_line_bytes_at_a_time);
  RUN_TEST(test_read_reply_without_text);
  RUN_TEST(test_read_reply_malformed_line);
  RUN_TEST(test_read_reply_code_changes_mid_reply);
  RUN_TEST(test_read_reply_hangup_in_continuation);
  RUN_TEST(test_read_reply_line_too_long);
  RUN_TEST(test_read_reply_bad_arguments);
  RUN_TEST(test_write_all_single_write);
  RUN_TEST(test_write_all_short_writes);
  RUN_TEST(test_write_all_error);
  RUN_TEST(test_write_all_bad_arguments);
  RUN_TEST(test_command_success);
  RUN_TEST(test_command_wrong_code_reports_reply);
  RUN_TEST(test_command_wrong_code_multi_line_reply);
  RUN_TEST(test_command_write_failure);
  RUN_TEST(test_command_hangup_before_reply);
  RUN_TEST(test_command_bad_arguments);
  RUN_TEST(test_send_mail_happy_path);
  RUN_TEST(test_send_mail_replies_a_few_bytes_at_a_time);
  RUN_TEST(test_send_mail_all_replies_in_one_read);
  RUN_TEST(test_send_mail_follows_multi_line_replies);
  RUN_TEST(test_send_mail_dot_stuffs_body);
  RUN_TEST(test_send_mail_each_wrong_status_code);
  RUN_TEST(test_send_mail_server_hangs_up_mid_session);
  RUN_TEST(test_send_mail_hangup_names_the_step);
  RUN_TEST(test_send_mail_write_failure);
  RUN_TEST(test_send_mail_malformed_greeting);
  RUN_TEST(test_send_mail_bad_arguments);

  /* Layer 3 */
  RUN_TEST(test_connect_and_talk_over_loopback);
  RUN_TEST(test_full_session_over_real_socket);
  RUN_TEST(test_connect_refused);
  RUN_TEST(test_connect_unresolvable);
  RUN_TEST(test_connect_bad_arguments);
  RUN_TEST(test_socket_callbacks_reject_null);
  RUN_TEST(test_socket_callbacks_report_errors);

  return UNITY_END();
}
