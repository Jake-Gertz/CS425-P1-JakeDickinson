# Submission Report

- Submission generated at 09/19/2026 at 21:52:42

- Machine info: Linux runnervmlun5p 6.17.0-1022-azure #22-Ubuntu SMP Mon Jul 27 17:24:03 UTC 2026 x86_64 x86_64 x86_64 GNU/Linux

## Note to Students

Please read this report carefully before submission.
Ensure that all sections are complete and accurate.
Look for any errors in the build or test outputs.
If you find any issues, correct them before submitting.
Post any questions on the class discussion board for help.


---

## README

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

---


## Build Output

This section was generated by running `make all` in the project root directory.

```bash
make[1]: Entering directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
mkdir -p build/debug
cc -g -O0 -DDEBUG -fno-omit-frame-pointer -fsanitize=address -c src/main.c -o build/debug/main.c.o
mkdir -p build/debug
cc -g -O0 -DDEBUG -fno-omit-frame-pointer -fsanitize=address -c src/lab.c -o build/debug/lab.c.o
cc -g -O0 -DDEBUG -fno-omit-frame-pointer -fsanitize=address build/debug/main.c.o build/debug/lab.c.o -o build/debug/myapp_d -fsanitize=address
make[1]: Leaving directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
make[1]: Entering directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
mkdir -p build/release
cc -Wall -Wextra -O2 -fPIE -MMD -MP -Wformat -Wformat=2 -Wconversion -Wsign-conversion -Wimplicit-fallthrough -fstack-protector-strong -Werror=format-security -Werror=implicit -Werror=incompatible-pointer-types -Werror=int-conversion -c src/main.c -o build/release/main.c.o
mkdir -p build/release
cc -Wall -Wextra -O2 -fPIE -MMD -MP -Wformat -Wformat=2 -Wconversion -Wsign-conversion -Wimplicit-fallthrough -fstack-protector-strong -Werror=format-security -Werror=implicit -Werror=incompatible-pointer-types -Werror=int-conversion -c src/lab.c -o build/release/lab.c.o
cc -Wall -Wextra -O2 -fPIE -MMD -MP -Wformat -Wformat=2 -Wconversion -Wsign-conversion -Wimplicit-fallthrough -fstack-protector-strong -Werror=format-security -Werror=implicit -Werror=incompatible-pointer-types -Werror=int-conversion build/release/main.c.o build/release/lab.c.o -o build/release/myapp 
make[1]: Leaving directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
make[1]: Entering directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
mkdir -p build/tests
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c src/main.c -o build/tests/main.c.o
mkdir -p build/tests
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c src/lab.c -o build/tests/lab.c.o
mkdir -p build/tests/
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c tests/lab-test.c -o build/tests/lab-test.c.o
mkdir -p build/tests/harness/
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c tests/harness/unity.c -o build/tests/harness/unity.c.o
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage build/tests/main.c.o build/tests/lab.c.o build/tests/lab-test.c.o build/tests/harness/unity.c.o -o build/tests/myapp_t -fprofile-arcs -ftest-coverage
make[1]: Leaving directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
make[1]: Entering directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
mkdir -p build/debug-test
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c src/main.c -o build/debug-test/main.c.o
mkdir -p build/debug-test
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c src/lab.c -o build/debug-test/lab.c.o
mkdir -p build/debug-test/
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c tests/lab-test.c -o build/debug-test/lab-test.c.o
mkdir -p build/debug-test/harness/
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c tests/harness/unity.c -o build/debug-test/harness/unity.c.o
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address build/debug-test/main.c.o build/debug-test/lab.c.o build/debug-test/lab-test.c.o build/debug-test/harness/unity.c.o -o build/debug-test/myapp_td -fsanitize=address
make[1]: Leaving directory '/home/runner/work/CS425-P1-JakeDickinson/CS425-P1-JakeDickinson'
Builds completed. You can run the application with: ./build/release/myapp
You can run the debug build with: ./build/debug/myapp_d
You can run the test build with: ./build/tests/myapp_t
You can run the debug-test build with: ./build/debug-test/myapp_td
```

---

## Coverage Report

This section was generated by running `make report` in the project root directory.

```bash
tests/lab-test.c:1060:test_parse_code_valid_forms:PASS
tests/lab-test.c:1061:test_parse_code_rejects_junk:PASS
tests/lab-test.c:1062:test_reply_is_final:PASS
tests/lab-test.c:1063:test_build_command_each_verb:PASS
tests/lab-test.c:1064:test_build_command_rejects_bad_input:PASS
tests/lab-test.c:1065:test_dot_stuff_plain_body:PASS
tests/lab-test.c:1066:test_dot_stuff_doubles_leading_dot:PASS
tests/lab-test.c:1067:test_dot_stuff_adds_missing_final_newline:PASS
tests/lab-test.c:1068:test_dot_stuff_keeps_existing_crlf:PASS
tests/lab-test.c:1069:test_dot_stuff_empty_and_null:PASS
tests/lab-test.c:1070:test_build_data_layout:PASS
tests/lab-test.c:1071:test_build_data_stuffs_body_and_allows_empty_subject:PASS
tests/lab-test.c:1072:test_build_data_rejects_null:PASS
tests/lab-test.c:1073:test_is_safe_value:PASS
tests/lab-test.c:1074:test_read_stream_small:PASS
tests/lab-test.c:1075:test_read_stream_empty:PASS
tests/lab-test.c:1076:test_read_stream_grows_buffer:PASS
tests/lab-test.c:1077:test_read_stream_errors:PASS
tests/lab-test.c:1080:test_session_init:PASS
tests/lab-test.c:1081:test_read_line_several_lines_in_one_read:PASS
tests/lab-test.c:1082:test_read_line_split_across_reads:PASS
tests/lab-test.c:1083:test_read_line_hangup_mid_line:PASS
tests/lab-test.c:1084:test_read_line_read_error:PASS
tests/lab-test.c:1085:test_read_line_too_long_for_buffer:PASS
tests/lab-test.c:1086:test_read_line_bad_arguments:PASS
tests/lab-test.c:1087:test_read_reply_single_line:PASS
tests/lab-test.c:1088:test_read_reply_multi_line:PASS
tests/lab-test.c:1089:test_read_reply_multi_line_bytes_at_a_time:PASS
tests/lab-test.c:1090:test_read_reply_without_text:PASS
tests/lab-test.c:1091:test_read_reply_malformed_line:PASS
tests/lab-test.c:1092:test_read_reply_code_changes_mid_reply:PASS
tests/lab-test.c:1093:test_read_reply_hangup_in_continuation:PASS
tests/lab-test.c:1094:test_read_reply_line_too_long:PASS
tests/lab-test.c:1095:test_read_reply_bad_arguments:PASS
tests/lab-test.c:1096:test_write_all_single_write:PASS
tests/lab-test.c:1097:test_write_all_short_writes:PASS
tests/lab-test.c:1098:test_write_all_error:PASS
tests/lab-test.c:1099:test_write_all_bad_arguments:PASS
tests/lab-test.c:1100:test_command_success:PASS
tests/lab-test.c:1101:test_command_wrong_code_reports_reply:PASS
tests/lab-test.c:1102:test_command_wrong_code_multi_line_reply:PASS
tests/lab-test.c:1103:test_command_write_failure:PASS
tests/lab-test.c:1104:test_command_hangup_before_reply:PASS
tests/lab-test.c:1105:test_command_bad_arguments:PASS
tests/lab-test.c:1106:test_send_mail_happy_path:PASS
tests/lab-test.c:1107:test_send_mail_replies_a_few_bytes_at_a_time:PASS
tests/lab-test.c:1108:test_send_mail_all_replies_in_one_read:PASS
tests/lab-test.c:1109:test_send_mail_follows_multi_line_replies:PASS
tests/lab-test.c:1110:test_send_mail_dot_stuffs_body:PASS
tests/lab-test.c:1111:test_send_mail_each_wrong_status_code:PASS
tests/lab-test.c:1112:test_send_mail_server_hangs_up_mid_session:PASS
tests/lab-test.c:1113:test_send_mail_hangup_names_the_step:PASS
tests/lab-test.c:1114:test_send_mail_write_failure:PASS
tests/lab-test.c:1115:test_send_mail_malformed_greeting:PASS
tests/lab-test.c:1116:test_send_mail_bad_arguments:PASS
tests/lab-test.c:1119:test_connect_and_talk_over_loopback:PASS
tests/lab-test.c:1120:test_full_session_over_real_socket:PASS
tests/lab-test.c:1121:test_connect_refused:PASS
tests/lab-test.c:1122:test_connect_unresolvable:PASS
tests/lab-test.c:1123:test_connect_bad_arguments:PASS
tests/lab-test.c:1124:test_socket_callbacks_reject_null:PASS
tests/lab-test.c:1125:test_socket_callbacks_report_errors:PASS

-----------------------
62 Tests 0 Failures 0 Ignored 
OK
./build/tests/myapp_t
tests/lab-test.c:1060:test_parse_code_valid_forms:PASS
tests/lab-test.c:1061:test_parse_code_rejects_junk:PASS
tests/lab-test.c:1062:test_reply_is_final:PASS
tests/lab-test.c:1063:test_build_command_each_verb:PASS
tests/lab-test.c:1064:test_build_command_rejects_bad_input:PASS
tests/lab-test.c:1065:test_dot_stuff_plain_body:PASS
tests/lab-test.c:1066:test_dot_stuff_doubles_leading_dot:PASS
tests/lab-test.c:1067:test_dot_stuff_adds_missing_final_newline:PASS
tests/lab-test.c:1068:test_dot_stuff_keeps_existing_crlf:PASS
tests/lab-test.c:1069:test_dot_stuff_empty_and_null:PASS
tests/lab-test.c:1070:test_build_data_layout:PASS
tests/lab-test.c:1071:test_build_data_stuffs_body_and_allows_empty_subject:PASS
tests/lab-test.c:1072:test_build_data_rejects_null:PASS
tests/lab-test.c:1073:test_is_safe_value:PASS
tests/lab-test.c:1074:test_read_stream_small:PASS
tests/lab-test.c:1075:test_read_stream_empty:PASS
tests/lab-test.c:1076:test_read_stream_grows_buffer:PASS
tests/lab-test.c:1077:test_read_stream_errors:PASS
tests/lab-test.c:1080:test_session_init:PASS
tests/lab-test.c:1081:test_read_line_several_lines_in_one_read:PASS
tests/lab-test.c:1082:test_read_line_split_across_reads:PASS
tests/lab-test.c:1083:test_read_line_hangup_mid_line:PASS
tests/lab-test.c:1084:test_read_line_read_error:PASS
tests/lab-test.c:1085:test_read_line_too_long_for_buffer:PASS
tests/lab-test.c:1086:test_read_line_bad_arguments:PASS
tests/lab-test.c:1087:test_read_reply_single_line:PASS
tests/lab-test.c:1088:test_read_reply_multi_line:PASS
tests/lab-test.c:1089:test_read_reply_multi_line_bytes_at_a_time:PASS
tests/lab-test.c:1090:test_read_reply_without_text:PASS
tests/lab-test.c:1091:test_read_reply_malformed_line:PASS
tests/lab-test.c:1092:test_read_reply_code_changes_mid_reply:PASS
tests/lab-test.c:1093:test_read_reply_hangup_in_continuation:PASS
tests/lab-test.c:1094:test_read_reply_line_too_long:PASS
tests/lab-test.c:1095:test_read_reply_bad_arguments:PASS
tests/lab-test.c:1096:test_write_all_single_write:PASS
tests/lab-test.c:1097:test_write_all_short_writes:PASS
tests/lab-test.c:1098:test_write_all_error:PASS
tests/lab-test.c:1099:test_write_all_bad_arguments:PASS
tests/lab-test.c:1100:test_command_success:PASS
tests/lab-test.c:1101:test_command_wrong_code_reports_reply:PASS
tests/lab-test.c:1102:test_command_wrong_code_multi_line_reply:PASS
tests/lab-test.c:1103:test_command_write_failure:PASS
tests/lab-test.c:1104:test_command_hangup_before_reply:PASS
tests/lab-test.c:1105:test_command_bad_arguments:PASS
tests/lab-test.c:1106:test_send_mail_happy_path:PASS
tests/lab-test.c:1107:test_send_mail_replies_a_few_bytes_at_a_time:PASS
tests/lab-test.c:1108:test_send_mail_all_replies_in_one_read:PASS
tests/lab-test.c:1109:test_send_mail_follows_multi_line_replies:PASS
tests/lab-test.c:1110:test_send_mail_dot_stuffs_body:PASS
tests/lab-test.c:1111:test_send_mail_each_wrong_status_code:PASS
tests/lab-test.c:1112:test_send_mail_server_hangs_up_mid_session:PASS
tests/lab-test.c:1113:test_send_mail_hangup_names_the_step:PASS
tests/lab-test.c:1114:test_send_mail_write_failure:PASS
tests/lab-test.c:1115:test_send_mail_malformed_greeting:PASS
tests/lab-test.c:1116:test_send_mail_bad_arguments:PASS
tests/lab-test.c:1119:test_connect_and_talk_over_loopback:PASS
tests/lab-test.c:1120:test_full_session_over_real_socket:PASS
tests/lab-test.c:1121:test_connect_refused:PASS
tests/lab-test.c:1122:test_connect_unresolvable:PASS
tests/lab-test.c:1123:test_connect_bad_arguments:PASS
tests/lab-test.c:1124:test_socket_callbacks_reject_null:PASS
tests/lab-test.c:1125:test_socket_callbacks_report_errors:PASS

-----------------------
62 Tests 0 Failures 0 Ignored 
OK
mkdir -p ./build/report/html
mkdir -p ./build/report/txt
gcovr -r . --html --html-details --exclude-directories build/tests/harness --exclude '.*main\.c$' --exclude '.*test\.c$' -o ./build/report/html/coverage_report.html
(INFO) Reading coverage data...

(INFO) Writing coverage report...

gcovr -r . --txt                 --exclude-directories build/tests/harness --exclude '.*main\.c$' --exclude '.*test\.c$'
(INFO) Reading coverage data...

(INFO) Writing coverage report...

------------------------------------------------------------------------------
                           GCC Code Coverage Report
Directory: .
------------------------------------------------------------------------------
File                                       Lines     Exec  Cover   Missing
------------------------------------------------------------------------------
src/lab.c                                    315      315   100%
------------------------------------------------------------------------------
TOTAL                                        315      315   100%
------------------------------------------------------------------------------
```

---

## Address Sanitizer Report

This section was generated by running `make leak-test` in the project root directory.

```bash
tests/lab-test.c:1060:test_parse_code_valid_forms:PASS
tests/lab-test.c:1061:test_parse_code_rejects_junk:PASS
tests/lab-test.c:1062:test_reply_is_final:PASS
tests/lab-test.c:1063:test_build_command_each_verb:PASS
tests/lab-test.c:1064:test_build_command_rejects_bad_input:PASS
tests/lab-test.c:1065:test_dot_stuff_plain_body:PASS
tests/lab-test.c:1066:test_dot_stuff_doubles_leading_dot:PASS
tests/lab-test.c:1067:test_dot_stuff_adds_missing_final_newline:PASS
tests/lab-test.c:1068:test_dot_stuff_keeps_existing_crlf:PASS
tests/lab-test.c:1069:test_dot_stuff_empty_and_null:PASS
tests/lab-test.c:1070:test_build_data_layout:PASS
tests/lab-test.c:1071:test_build_data_stuffs_body_and_allows_empty_subject:PASS
tests/lab-test.c:1072:test_build_data_rejects_null:PASS
tests/lab-test.c:1073:test_is_safe_value:PASS
tests/lab-test.c:1074:test_read_stream_small:PASS
tests/lab-test.c:1075:test_read_stream_empty:PASS
tests/lab-test.c:1076:test_read_stream_grows_buffer:PASS
tests/lab-test.c:1077:test_read_stream_errors:PASS
tests/lab-test.c:1080:test_session_init:PASS
tests/lab-test.c:1081:test_read_line_several_lines_in_one_read:PASS
tests/lab-test.c:1082:test_read_line_split_across_reads:PASS
tests/lab-test.c:1083:test_read_line_hangup_mid_line:PASS
tests/lab-test.c:1084:test_read_line_read_error:PASS
tests/lab-test.c:1085:test_read_line_too_long_for_buffer:PASS
tests/lab-test.c:1086:test_read_line_bad_arguments:PASS
tests/lab-test.c:1087:test_read_reply_single_line:PASS
tests/lab-test.c:1088:test_read_reply_multi_line:PASS
tests/lab-test.c:1089:test_read_reply_multi_line_bytes_at_a_time:PASS
tests/lab-test.c:1090:test_read_reply_without_text:PASS
tests/lab-test.c:1091:test_read_reply_malformed_line:PASS
tests/lab-test.c:1092:test_read_reply_code_changes_mid_reply:PASS
tests/lab-test.c:1093:test_read_reply_hangup_in_continuation:PASS
tests/lab-test.c:1094:test_read_reply_line_too_long:PASS
tests/lab-test.c:1095:test_read_reply_bad_arguments:PASS
tests/lab-test.c:1096:test_write_all_single_write:PASS
tests/lab-test.c:1097:test_write_all_short_writes:PASS
tests/lab-test.c:1098:test_write_all_error:PASS
tests/lab-test.c:1099:test_write_all_bad_arguments:PASS
tests/lab-test.c:1100:test_command_success:PASS
tests/lab-test.c:1101:test_command_wrong_code_reports_reply:PASS
tests/lab-test.c:1102:test_command_wrong_code_multi_line_reply:PASS
tests/lab-test.c:1103:test_command_write_failure:PASS
tests/lab-test.c:1104:test_command_hangup_before_reply:PASS
tests/lab-test.c:1105:test_command_bad_arguments:PASS
tests/lab-test.c:1106:test_send_mail_happy_path:PASS
tests/lab-test.c:1107:test_send_mail_replies_a_few_bytes_at_a_time:PASS
tests/lab-test.c:1108:test_send_mail_all_replies_in_one_read:PASS
tests/lab-test.c:1109:test_send_mail_follows_multi_line_replies:PASS
tests/lab-test.c:1110:test_send_mail_dot_stuffs_body:PASS
tests/lab-test.c:1111:test_send_mail_each_wrong_status_code:PASS
tests/lab-test.c:1112:test_send_mail_server_hangs_up_mid_session:PASS
tests/lab-test.c:1113:test_send_mail_hangup_names_the_step:PASS
tests/lab-test.c:1114:test_send_mail_write_failure:PASS
tests/lab-test.c:1115:test_send_mail_malformed_greeting:PASS
tests/lab-test.c:1116:test_send_mail_bad_arguments:PASS
tests/lab-test.c:1119:test_connect_and_talk_over_loopback:PASS
tests/lab-test.c:1120:test_full_session_over_real_socket:PASS
tests/lab-test.c:1121:test_connect_refused:PASS
tests/lab-test.c:1122:test_connect_unresolvable:PASS
tests/lab-test.c:1123:test_connect_bad_arguments:PASS
tests/lab-test.c:1124:test_socket_callbacks_reject_null:PASS
tests/lab-test.c:1125:test_socket_callbacks_report_errors:PASS

-----------------------
62 Tests 0 Failures 0 Ignored 
OK
```

---

## Src Files
### lab.c

```c

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

```

### lab.h

```c

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

```

### main.c

```c

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

```

## Tests Files
### lab-test.c

```c

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

```

## Scripts Files
Report generated on 09/19/2026 at 21:52:44


---

## End of Report

SHA-256 Hash of the report: 0ed6ea34fdccc24172cd66da3e547140e844406d077d06f7e2afaf5bf76bfe49

Do not edit the generated report. Any changes will be reported as academic dishonesty

---
## GitHub Info
- GitHub repo name: Jake-Gertz/CS425-P1-JakeDickinson
- The repository visibility is public.
- The workflow was triggered by Jake-Gertz
