#ifndef LAB_H
#define LAB_H

/**
 * @file lab.h
 * @brief A minimal SMTP (RFC 5321) client, split into three layers.
 *
 *  1. Pure protocol helpers: strings in, strings or status codes out.
 *     Nothing in this layer performs I/O, so every function can be unit
 *     tested with plain string literals.
 *
 *  2. The session: reads lines and whole replies, sends commands, checks
 *     status codes and runs the full HELO..QUIT conversation. All of its
 *     reading and writing goes through a pair of function pointers plus a
 *     context pointer, so the transport underneath can be a real socket or
 *     a scripted in-memory server used by the tests.
 *
 *  3. The socket transport: thin wrappers over getaddrinfo, connect, recv
 *     and send that satisfy the two callbacks from layer 2.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Shared constants                                                         */
/* ------------------------------------------------------------------------ */

/** Largest single reply line (including CRLF) the session will accept. */
#define SMTP_LINE_MAX 1024

/** Size of the human readable error buffer inside an smtp_session_t. */
#define SMTP_ERR_MAX 512

/** Default port used when none is supplied on the command line. */
#define SMTP_DEFAULT_PORT "25"

/** Default host name sent with HELO when none is supplied. */
#define SMTP_DEFAULT_HELO "localhost"

/**
 * Status returned by the session functions. Zero always means success and
 * every failure is negative so callers can test `< 0`.
 */
enum {
  SMTP_OK = 0,          /**< Everything went as expected. */
  SMTP_ERR_EOF = -1,    /**< The peer closed the connection. */
  SMTP_ERR_IO = -2,     /**< A read or write callback reported an error. */
  SMTP_ERR_LINE = -3,   /**< A reply line does not fit in SMTP_LINE_MAX. */
  SMTP_ERR_PROTO = -4,  /**< A reply was malformed (bad or mismatched code). */
  SMTP_ERR_STATUS = -5, /**< A reply carried an unexpected status code. */
  SMTP_ERR_ARG = -6     /**< A NULL or otherwise invalid argument. */
};

/* ------------------------------------------------------------------------ */
/* Layer 1: pure protocol helpers (no I/O)                                  */
/* ------------------------------------------------------------------------ */

/** Commands the client knows how to build. */
typedef enum {
  SMTP_CMD_HELO, /**< HELO <arg> */
  SMTP_CMD_MAIL, /**< MAIL FROM:<arg> */
  SMTP_CMD_RCPT, /**< RCPT TO:<arg> */
  SMTP_CMD_DATA, /**< DATA (arg ignored) */
  SMTP_CMD_QUIT  /**< QUIT (arg ignored) */
} smtp_cmd_t;

/**
 * @brief Extract the three digit status code from a reply line.
 *
 * A valid line starts with exactly three ASCII digits followed by a space,
 * a hyphen, a CR, a LF or the end of the string.
 *
 * @param line One reply line, with or without its trailing CRLF.
 * @return The code (100..999) or -1 when the line is NULL or malformed.
 */
int smtp_parse_code(const char *line);

/**
 * @brief Decide whether a reply line is the last line of its reply.
 *
 * Per RFC 5321 section 4.2.1 every line of a multi-line reply except the
 * last has a hyphen right after the code; the last one has a space (or,
 * for a bare code, nothing at all).
 *
 * @param line One reply line, with or without its trailing CRLF.
 * @return true when the line ends the reply, false for a continuation line
 *         or a malformed line.
 */
bool smtp_reply_is_final(const char *line);

/**
 * @brief Build one CRLF terminated command line.
 *
 * @param cmd Which command to build.
 * @param arg The argument (host for HELO, address for MAIL and RCPT). It is
 *            ignored for DATA and QUIT and must not be NULL otherwise.
 * @return A malloc'd string such as "MAIL FROM:<a@b>\r\n", or NULL when the
 *         command is unknown or a required argument is missing. The caller
 *         frees it.
 */
char *smtp_build_command(smtp_cmd_t cmd, const char *arg);

/**
 * @brief Dot stuff a message body and normalise its line endings.
 *
 * Every line is terminated with CRLF (bare LF and CRLF are both accepted on
 * input), a line that begins with '.' gets a second '.' in front as required
 * by RFC 5321 section 4.5.2, and the result always ends with CRLF so that
 * the terminating "." line can follow it directly.
 *
 * @param body The raw body, possibly empty.
 * @return A malloc'd stuffed body or NULL when body is NULL. The caller
 *         frees it.
 */
char *smtp_dot_stuff(const char *body);

/**
 * @brief Build the complete DATA payload.
 *
 * The payload is the From, To and Subject headers, a blank line, the dot
 * stuffed body and the "." line that ends the message.
 *
 * @param from    Envelope sender, used for the From header.
 * @param to      Envelope recipient, used for the To header.
 * @param subject Subject line; an empty string produces an empty subject.
 * @param body    Raw body, dot stuffed by this function.
 * @return A malloc'd payload or NULL when any argument is NULL. The caller
 *         frees it.
 */
char *smtp_build_data(const char *from, const char *to, const char *subject,
                      const char *body);

/**
 * @brief Check that a value is safe to place on a single protocol line.
 *
 * Addresses, subjects and HELO hosts are placed verbatim on a line, so a
 * value containing a bare CR or LF would let the caller inject an extra SMTP
 * command or mail header. Such values must be rejected before use.
 *
 * @param value The string to check.
 * @return true when value is non-NULL and contains neither '\r' nor '\n'.
 */
bool smtp_is_safe_value(const char *value);

/**
 * @brief Read an entire stream into a NUL terminated string.
 *
 * Used to read the message body from stdin when -b is not given.
 *
 * @param fp An open stream positioned where reading should begin.
 * @return A malloc'd string with the stream contents (possibly empty), or
 *         NULL when fp is NULL or a read error occurs. The caller frees it.
 */
char *smtp_read_stream(FILE *fp);

/* ------------------------------------------------------------------------ */
/* Layer 2: the session, over a swappable transport                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Read callback: fill buf with up to len bytes.
 * @return Bytes read (> 0), 0 when the peer closed, or -1 on error.
 */
typedef ssize_t (*smtp_read_fn)(void *ctx, char *buf, size_t len);

/**
 * @brief Write callback: send up to len bytes from buf.
 * @return Bytes written (> 0) or -1 on error.
 */
typedef ssize_t (*smtp_write_fn)(void *ctx, const char *buf, size_t len);

/** All state for one SMTP conversation. */
typedef struct {
  smtp_read_fn read;   /**< Transport read callback. */
  smtp_write_fn write; /**< Transport write callback. */
  void *ctx;           /**< Opaque pointer handed to both callbacks. */
  char buf[SMTP_LINE_MAX]; /**< Bytes received but not yet consumed. */
  size_t len;              /**< Number of valid bytes in buf. */
  char error[SMTP_ERR_MAX]; /**< Human readable reason for the last failure. */
} smtp_session_t;

/** Everything needed to send one message. */
typedef struct {
  const char *helo_host; /**< Host name announced with HELO. */
  const char *from;      /**< Envelope sender. */
  const char *to;        /**< Envelope recipient. */
  const char *subject;   /**< Subject header. */
  const char *body;      /**< Raw body; dot stuffed by the session. */
} smtp_mail_t;

/**
 * @brief Initialise a session over the given transport.
 *
 * @param s     Session to initialise.
 * @param read  Read callback.
 * @param write Write callback.
 * @param ctx   Context pointer passed to both callbacks.
 */
void smtp_session_init(smtp_session_t *s, smtp_read_fn read,
                       smtp_write_fn write, void *ctx);

/**
 * @brief Read one line from the transport.
 *
 * The session keeps a buffer that is refilled only when it does not already
 * hold a complete line, so a line split across several reads and several
 * lines arriving in one read are both handled. The line is returned without
 * its terminating CRLF (a bare LF is tolerated as a terminator).
 *
 * @param s      The session.
 * @param out    Destination for the NUL terminated line.
 * @param outlen Size of out; must be at least SMTP_LINE_MAX.
 * @return The line length (>= 0), or SMTP_ERR_EOF, SMTP_ERR_IO,
 *         SMTP_ERR_LINE or SMTP_ERR_ARG.
 */
int smtp_read_line(smtp_session_t *s, char *out, size_t outlen);

/**
 * @brief Read a complete reply, following continuation lines.
 *
 * @param s    The session.
 * @param text When non-NULL receives a malloc'd copy of the full reply text,
 *             lines joined with "\n", which the caller frees. It is set to
 *             NULL on failure.
 * @return The status code (100..999) or a negative SMTP_ERR_* value.
 */
int smtp_read_reply(smtp_session_t *s, char **text);

/**
 * @brief Write the whole buffer, looping over short writes.
 *
 * @param s    The session.
 * @param data Bytes to send.
 * @param len  Number of bytes.
 * @return SMTP_OK, SMTP_ERR_IO or SMTP_ERR_ARG.
 */
int smtp_write_all(smtp_session_t *s, const char *data, size_t len);

/**
 * @brief Send one command (or the DATA payload) and check the reply code.
 *
 * On any failure s->error describes what happened, including the reply the
 * server actually sent when the code was not the expected one.
 *
 * @param s        The session.
 * @param line     The bytes to send, already CRLF terminated.
 * @param expected The status code the protocol requires at this point.
 * @return SMTP_OK, or a negative SMTP_ERR_* value.
 */
int smtp_command(smtp_session_t *s, const char *line, int expected);

/**
 * @brief Run the whole conversation: greeting, HELO, MAIL FROM, RCPT TO,
 *        DATA, the message and QUIT.
 *
 * The client stops at the first reply that is not what the protocol
 * expects; s->error then explains which step failed and what the server
 * said.
 *
 * @param s    The session, already initialised over a connected transport.
 * @param mail The message to send. Every field must be non-NULL.
 * @return SMTP_OK when the server queued the message, otherwise a negative
 *         SMTP_ERR_* value.
 */
int smtp_send_mail(smtp_session_t *s, const smtp_mail_t *mail);

/* ------------------------------------------------------------------------ */
/* Layer 3: the socket transport                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Resolve host/port with getaddrinfo and connect a TCP socket.
 *
 * Every address returned by the resolver is tried in turn.
 *
 * @param host   Host name or address of the server.
 * @param port   Port number or service name.
 * @param err    Buffer that receives a reason on failure (may be NULL).
 * @param errlen Size of err.
 * @return A connected socket descriptor, or -1 on failure.
 */
int smtp_connect(const char *host, const char *port, char *err, size_t errlen);

/**
 * @brief smtp_read_fn implementation over recv().
 * @param ctx Pointer to the int socket descriptor.
 */
ssize_t smtp_socket_read(void *ctx, char *buf, size_t len);

/**
 * @brief smtp_write_fn implementation over send().
 * @param ctx Pointer to the int socket descriptor.
 */
ssize_t smtp_socket_write(void *ctx, const char *buf, size_t len);

/**
 * @brief Close a socket returned by smtp_connect.
 * @param fd The descriptor; negative values are ignored.
 */
void smtp_close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* LAB_H */
