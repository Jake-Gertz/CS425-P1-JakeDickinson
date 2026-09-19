# P1 - Simple Mail Client

- Name: Jake Dickinson
- Email: jakedickinson@u.boisestate.edu
- Class: CS425-001

## Overview

`myapp` is an SMTP client written against RFC 5321 directly: it opens a TCP
socket, reads the server's greeting, then speaks `HELO`, `MAIL FROM`,
`RCPT TO`, `DATA`, the message and `QUIT` by hand, checking the status code
of every reply before it moves on. No mail library is used.

```
Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]
             [-H helo-host] <server>
```

```bash
echo "This is the message body." | \
  ./build/release/myapp -f me@boisestate.edu -t you@example.com \
    -s "hello" -H onyx.boisestate.edu -p 2525 <server>
```

The exit status is `0` when the server queues the message, `1` when the
command line is wrong and `2` when the connection or the SMTP session fails.
Running with no arguments prints the usage text and exits `0`.

## Building and Testing

```bash
make all        # debug, release, test and debug-test builds
make check      # run the Unity tests
make report     # run the tests and produce the coverage report
make leak       # run the release path (usage message) under AddressSanitizer
make leak-test  # run the unit tests under AddressSanitizer
```

## Design

`src/lab.h` is split into three layers. The split exists because the
protocol cannot be unit tested against a live mail server, so the code that
understands SMTP must never touch a socket directly.

### Layer 1 - Pure protocol helpers

Functions that take strings and return strings or status codes, with no I/O
anywhere: `smtp_parse_code` pulls the three digit code out of a reply line,
`smtp_reply_is_final` tells a `250-` continuation line from the final
`250 ` line, `smtp_build_command` builds a CRLF terminated command,
`smtp_dot_stuff` normalises line endings to CRLF and doubles any leading
period (RFC 5321 section 4.5.2), `smtp_build_data` assembles the `From`,
`To` and `Subject` headers, the blank line, the stuffed body and the
terminating `.` line, and `smtp_is_safe_value` rejects values containing a
bare CR or LF so a hostile address or subject cannot inject an extra command
or header into the session. Every one of these is tested with plain string
literals.

### Layer 2 - The session, over a swappable transport

`smtp_session_t` holds a read callback, a write callback and a context
pointer. Everything in this layer - `smtp_read_line`, `smtp_read_reply`,
`smtp_write_all`, `smtp_command` and `smtp_send_mail` - reads and writes only
through those two function pointers; it never calls `recv` or `send` itself.

`smtp_read_line` keeps a buffer and refills it only when it does not already
hold a complete line, so a reply that arrives a few bytes at a time and
several replies that arrive in one read are both handled. `smtp_read_reply`
reads lines until it sees the final one, so multi-line replies are followed
before any decision is made. `smtp_send_mail` runs the entire conversation,
requiring 220, 250, 250, 250, 354, 250 and 221 in that order, and stops at
the first reply that does not match, recording which step failed and what
the server actually said in `session.error`.

### Layer 3 - The socket transport

Thin wrappers over the system calls: `smtp_connect` resolves the server with
`getaddrinfo` and tries every returned address until `connect` succeeds,
and `smtp_socket_read` / `smtp_socket_write` wrap `recv` and `send` with the
exact signatures layer 2 expects.

### Why it is worth it

`main.c` plugs layer 3 into layer 2. The tests plug in a scripted in-memory
server instead: a string of replies handed out through the same read
callback, with everything the client sends captured through the write
callback. That lets the test suite drive the complete session and every one
of its error paths - a wrong code at each step, a server that hangs up at
each step, a reply that arrives one byte at a time, a reply that does not
fit in the buffer - with no network at all. Layer 3 is tested separately
against a listening socket on `127.0.0.1`, so the whole suite runs offline.

## Known Bugs or Issues

None known. Two behaviours worth noting:

- Outbound port 25 is blocked on many campus and home networks. If the
  connection hangs, use `-p 2525` (or `-p 587`); that is the network, not the
  client.
- A body line is terminated by either LF or CRLF on input; a lone CR that is
  not part of CRLF is passed through unchanged.

## Experience

The pieces that take the most care in this project are the ones the
assignment warns about. Reading a line is not one `recv`: the buffered
reader in `smtp_read_line` has to handle a line split across reads, several
lines in one read, and a line that never ends, without ever reading past
the current reply. Getting the three-layer split right up front made the
rest straightforward - once the session only knew about two function
pointers, every error path became a short string in a test instead of an
experiment against a live server.

The other thing that paid off was treating every error path as a normal
path for allocation: the reply text is freed on every exit from
`expect_reply`, and the fixed-size `error` buffer inside the session means
reporting a failure never allocates, so there is nothing to leak when
things go wrong.

## Analysis

`make report` shows 100% line coverage of `src/lab.c`; the only excluded
lines are `malloc`/`realloc` failure branches and the `socket()` failure
branch, all of which originate from library or system calls. `make leak`
and `make leak-test` both run clean under AddressSanitizer with leak
detection on, including the tests that exercise every failure path of the
session.
