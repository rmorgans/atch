---
name: atch
description: >
  Agent process control plane for persistent processes, locally or over SSH.
  This skill should be used when starting long-running jobs, checking process
  status, tailing logs, searching output, pushing input, or managing persistent
  sessions on local or remote hosts. For shell configuration, use shell.
---

# Atch: Agent Process Control Plane

`atch` gives agents what they lack: persistent processes they can start, leave, observe, poke, and kill — locally or over SSH — without ever needing an interactive terminal.

An agent's process control loop:
1. **Start** a process — it persists independently of the agent session
2. **Observe** — check status, tail recent output, grep the full log
3. **Push** — send input without attaching
4. **Kill** — clean shutdown when done

Every step is a single non-interactive command. No PTY required (except `attach`).

## Installation

Prebuilt static binaries (Linux) and native binaries (macOS) from GitHub releases. Auto-detects architecture:

**Local install:**
```sh
ARCH=$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')
OS=$(uname -s | tr A-Z a-z | sed 's/linux/linux/;s/darwin/darwin/')
curl -L -o /tmp/atch "https://github.com/rmorgans/atch/releases/latest/download/atch-${OS}-${ARCH}"
sudo install -m 755 /tmp/atch /usr/local/bin/atch && rm /tmp/atch
```

**Remote install (one host):**
```sh
ssh host 'ARCH=$(uname -m | sed "s/x86_64/amd64/;s/aarch64/arm64/") && curl -L -o /tmp/atch "https://github.com/rmorgans/atch/releases/latest/download/atch-linux-${ARCH}" && sudo install -m 755 /tmp/atch /usr/local/bin/atch && rm /tmp/atch'
```

**Remote install (multiple hosts):**
```sh
for host in web1 gpu-box lab3; do
    printf "%s: " "$host"
    ssh "$host" 'ARCH=$(uname -m | sed "s/x86_64/amd64/;s/aarch64/arm64/") && curl -sL -o /tmp/atch "https://github.com/rmorgans/atch/releases/latest/download/atch-linux-${ARCH}" && sudo install -m 755 /tmp/atch /usr/local/bin/atch && rm /tmp/atch && atch --version' 2>&1 || echo "FAILED"
done
```

**Build from source:** `git clone https://github.com/rmorgans/atch && cd atch && make && sudo make install`

Available platforms: `linux-amd64`, `linux-arm64`, `darwin-arm64`. Intel Mac users should build from source.

## Guidelines

- Do not hardcode `~/.cache/atch/` or any other session directory. Resolve logs with `atch log-path <session>`.
- Use `ssh -t` only for `atch attach`. All other commands work without a PTY — this is what makes them agent-friendly.
- Distinguish SSH failure from remote `atch` failure. SSH exit 255 means transport/auth failed. `atch` exit 1 means the command itself failed.
- If the remote binary is not called `atch` or not in PATH, resolve the full path first and reuse it consistently.
- The target process must be ready before pushing input. Wait for a known marker in `tail` output, or use fast-starting targets (`cat`, `sh`) for smoke tests.

## Resolving the Remote Command

Before running remote commands, confirm the binary name and path:

```sh
ATCH=$(ssh host 'command -v atch 2>/dev/null || echo /usr/local/bin/atch')
```

Then use `$ATCH` in all remote commands:

```sh
ssh host "$ATCH" list -a
ssh host "$ATCH" tail -n 50 session
ssh host "$ATCH" start job command arg1
```

All remote examples below use `atch` for readability. Replace with the resolved command when the binary is at a non-standard path or has a different name.

## Agent Workflow

```
start → tail/log-path/grep → push (if needed) → kill
```

Every step is fire-and-forget. The agent does not need to maintain a connection between steps.

### 1. Start (fire and forget)

```sh
atch start job-name command arg1 arg2            # local
ssh host atch start job-name command arg1 arg2   # remote
```

The process is detached immediately. The agent can disconnect, compact context, or do other work.

### 2. Discover (what's running?)

```sh
atch list -a                                     # local
ssh host atch list -a                            # remote
```

Use `list -a` to include exited sessions. Treat `(no sessions)` as valid empty state.

### 3. Observe (non-interactive)

**Recent output:**
```sh
atch tail -n 50 session                          # local
ssh host atch tail -n 50 session                 # remote
```

**Deep search (full log) — two-step form:**
```sh
# Local:
grep -n "pattern" "$(atch log-path session)"

# Remote (two-step to avoid quoting issues):
log_path=$(ssh host atch log-path session)
ssh host grep -n "pattern" "$log_path"
```

The compact remote one-liner works but is fragile with user-supplied patterns:
```sh
ssh host 'grep -n "pattern" "$(atch log-path session)"'
```

**Follow mode (keeps connection open):**
```sh
atch tail -f session
ssh host atch tail -f session
```

### 4. Push Input (non-interactive)

```sh
printf "command\n" | atch push session           # local
printf "command\n" | ssh host atch push session   # remote
```

### 5. Kill

```sh
atch kill session                                # graceful SIGTERM → SIGKILL
atch kill -f session                             # immediate SIGKILL
ssh host atch kill session                       # remote
```

### 6. Attach (interactive — rare for agents)

```sh
ssh -t host atch attach session
```

This is the only command that needs a PTY. Agents should prefer tail/push over attach.

## Error Recovery

**`command not found`** — atch is not in PATH on the target host:
```sh
ssh host 'command -v atch || ls /usr/local/bin/atch 2>/dev/null'
```
If found at a non-standard path, use the full path for all subsequent commands.

**`session does not exist`** — the session was never started or has been killed:
- Check `atch list -a` for exited sessions with logs still available
- Use `atch log-path session` to check if the log persists

**`no log for session`** — the session was started with `-C 0` (logging disabled) or the log was cleared:
- No recovery — both `log-path` and `tail` read the same on-disk log file. If it doesn't exist, neither command can help.
- Attach interactively (`ssh -t host atch attach session`) to observe a running session that has no log.

**SSH exit 255** — SSH transport failure, not an atch error:
- Check connectivity: `ssh -o ConnectTimeout=3 host echo ok`
- Check auth: look for `Permission denied` in stderr
- Do not confuse with `atch` exit 1

**Process not reading push input** — the target hasn't started reading yet:
- Check readiness: `atch tail session` should show a prompt or startup marker
- Retry after a delay, or use a faster-starting target

## Multi-Host Fan-Out

Use SSH config as the host registry.

```sh
for host in web1 gpu-box lab3; do
    printf "=== %s ===\n" "$host"
    output=$(ssh -o ConnectTimeout=3 "$host" atch list -a 2>&1)
    rc=$?
    if [ $rc -ne 0 ]; then
        printf "  [error: %s]\n" "$output"
    elif [ -z "$output" ] || [ "$output" = "(no sessions)" ]; then
        printf "  (no sessions)\n"
    else
        printf "%s\n" "$output"
    fi
done
```

## Output Format

When reporting session status to the user, summarize as:

```
[host] session-name — running (2h 15m) | last output: "..."
[host] session-name — exited | log available
[host] (no sessions)
[host] [error: connection timed out]
```

When reporting search results, include the host, session, and matching lines:

```
[host:session] line 42: ERROR: out of memory
[host:session] line 108: WARN: retry limit reached
```

## Why This Works for Agents

- **No persistent connection needed** — start a process, disconnect, come back later
- **Every command is non-interactive** — no menus, no prompts, no escape sequences
- **Observation without attachment** — tail and log-path give full visibility without a PTY
- **Input without attachment** — push sends keystrokes without a terminal session
- **Named sessions** — agents can reason about `(host, session)` pairs across turns
- **Exit codes are meaningful** — agents can branch on success/failure programmatically

## Path Contract

Never hardcode `~/.cache/atch/`. The session directory depends on the binary name and `$HOME` on the remote host. Always use `atch log-path`, `atch list`, and `atch tail` to resolve paths.
