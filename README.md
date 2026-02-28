# atch

`atch` is a small C utility that lets you attach and detach terminal sessions,
similar to the detach feature of `screen/tmux` — but without the terminal emulation,
multiple windows, or other overhead.

**The key property of `atch` is transparency.** It does not interpose a
terminal emulator between you and your program. The raw byte stream flows
directly from the pty to your terminal, exactly as if you had run the program
in a plain shell. This means:

- **Mouse works.** Mouse reporting, click events, and drag sequences pass
  through unmodified. No escape-sequence re-encoding, no `set -g mouse on`,
  no fighting with terminfo databases.
- **Scroll works.** Programs that use the alternate screen buffer, or that
  emit their own scroll sequences, behave identically inside and outside an
  `atch` session. There is nothing to configure.
- **Colors and graphics work.** True-color, sixel, kitty graphics, OSC
  sequences — all pass through untouched.
- **`$TERM` is unchanged.** `atch` does not set or override your terminal
  type. The program sees exactly the same `$TERM` your shell uses.

In contrast, `tmux` and `screen` implement their own terminal emulators.
They re-encode the output stream, which frequently breaks mouse support,
scroll behavior, and newer terminal features unless you find and apply the
right obscure configuration knob — and then remember it on every new machine.
With `atch` there is nothing to remember, because there is nothing in the way.

When a program runs inside an `atch` session it is protected from the
controlling terminal. You can detach from it, disconnect, and re-attach later
from the same or a different terminal, and the program keeps running
undisturbed.

## Features

- Attach and detach from running programs
- Multiple clients can attach to the same session simultaneously
- **No terminal emulation** — raw output stream is passed through unchanged
- Sessions persist across disconnects
- Scrollback replay on re-attach — see prior output when you reconnect
- Push stdin directly to a running session (`-p`)
- List all sessions with liveness status (`-l`)
- Prevents accidental recursive self-attach
- Tiny and auditable

## Building

```sh
make
```

## Usage

```
atch -a <session> <options>
atch -A <session> <options> [command...]
atch -c <session> <options> [command...]
atch -n <session> <options> [command...]
atch -N <session> <options> [command...]
atch -p <session>
atch -l
atch -i
```

Sessions are identified by name. A bare name (no `/`) is stored as a socket
under `~/.cache/atch/`. A name containing `/` is used as-is as a filesystem path.

If no command is given, `$SHELL` is used.

## Modes

| Flag | Description |
|------|-------------|
| `-a` | Attach to an existing session. |
| `-A` | Attach to a session, or create it (running the command) if it doesn't exist. |
| `-c` | Create a new session and attach to it. |
| `-n` | Create a new session and run the command detached (atch exits immediately). |
| `-N` | Like `-n`, but atch stays in the foreground instead of daemonizing. |
| `-p` | Copy stdin verbatim to the session (no detach-character scanning). |
| `-k` | Gracefully stop a session (SIGTERM, then SIGKILL after 5 s if needed). |
| `-l` | List all sessions in the session directory, with liveness status. |
| `-i` | Print the current session name and exit 0 if inside a session; exit 1 silently if not. |

## Options

| Flag | Description |
|------|-------------|
| `-e <char>` | Set the detach character. Accepts `^X` notation. Default: `^\`. |
| `-E` | Disable the detach character entirely. |
| `-r <method>` | Redraw method on attach: `none`, `ctrl_l`, or `winch` (default). |
| `-R <method>` | Clear method on attach: `none` or `move`. |
| `-z` | Disable suspend-key (`^Z`) processing (pass it to the program instead). |
| `-q` | Suppress informational messages. |
| `-t` | Disable VT100 assumptions. |

Use `--` to separate atch options from command arguments that start with `-`:

```sh
atch -c mysession -- grep -r foo /var/log
```

## Examples

**Start a shell session named `work` and attach to it:**
```sh
atch -c work
```

**Start a specific command in a named session:**
```sh
atch -c build -- make -j4
```

**Attach to an existing session, creating it if needed:**
```sh
atch -A work
```

**Detach** from a running session: press `^\` (Ctrl-\\). The session and its
program keep running.

**Re-attach** later:
```sh
atch -a work
```

**Run a command fully detached (no terminal needed):**
```sh
atch -n daemon myserver
```

**Send keystrokes to a running session:**
```sh
printf 'ls -la\n' | atch -p work
```

**Use a custom detach character:**
```sh
atch -a work -e '^A'
```

**List all sessions:**
```sh
atch -l
```

## Session storage

By default, session sockets are stored in `~/.cache/atch/`. The directory is
created automatically, including `~/.cache` if it does not yet exist. If
`$HOME` is unset, sockets are stored in `/tmp/.atch-<uid>/`.

To use a custom path, include a `/` in the session name:

```sh
atch -c /tmp/mysession
```

`atch` sets the `ATCH_SESSION` environment variable inside each session to the
socket path. This prevents recursive self-attach and can be used by shell
prompts or scripts to detect whether they are running inside an `atch` session.

Use `atch -i` to check from a script or prompt:

```sh
# exit code: 0 inside a session, 1 outside
atch -i && echo "inside session: $(atch -i)"

# shell prompt example (bash/zsh PS1)
PS1='$(atch -i 2>/dev/null && echo "[$(atch -i)] ")$ '
```

Sessions can be nested: running `atch -c inner` from within a session is
allowed. `ATCH_SESSION` is set only in the child process of each master, so
the outer session's shell always retains its own value — the self-attach
protection works correctly at all nesting levels.

## Scrollback

When re-attaching to a session, `atch` replays the last 128 KB of output so
you can see what happened while you were away. No configuration needed — it
works automatically.

The scrollback buffer is in-memory in the master process. It is lost only if
the master exits (which ends the session anyway). To adjust the buffer size,
build with:

```sh
make CFLAGS="-DSCROLLBACK_SIZE=$((256*1024))"
```

The value must be a power of two.

## License

GPL. Based on dtach by Ned T. Crigler.
