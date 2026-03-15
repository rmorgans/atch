# Remote Process Control for Agents

**Date:** 2026-03-15
**Status:** Design — partially implemented
**Last updated:** 2026-03-15

## What's done

Atch on the fork (`rmorgans/atch`) now has the full command surface needed for remote process control. All upstream PRs (from 3 contributors) plus our own are merged into `fork/main`.

### Commands available (current build)

| Command | Purpose | PR |
|---------|---------|-----|
| `start <session> [cmd...]` | Create session, detached | upstream |
| `attach <session>` | Strict attach (no log replay for dead sessions) | #26 |
| `new <session> [cmd...]` | Create and attach | upstream |
| `list [-a]` | List sessions (-a includes exited) | upstream |
| `tail [-f] [-n N] <session>` | Print last N lines of log | upstream |
| `log-path <session>` | Print resolved log file path | #27 |
| `push <session>` | Pipe stdin into running session | upstream |
| `kill [-f] <session>` | Stop session | upstream |
| `clear [<session>]` | Truncate session log | upstream |
| `current` | Print current session name | upstream |

### Fixes landed on fork/main

| PR | Fix | Author |
|----|-----|--------|
| #9 | PID ancestry walk replaces stale ATCH_SESSION check | DonaldoDes |
| #10 | Log replay capped to 128KB | DonaldoDes |
| #11 | MSG_DETACH sent on detach key press | DonaldoDes |
| #12 | umask(0177) before bind prevents S_IXUSR race | DonaldoDes |
| #13 | Man page, make install, fork constitution | DonaldoDes |
| #16 | Log cap drift fix | ricardovfreixo |
| #22 | Retry partial writes in packet and push paths | rmorgans |
| #23 | Defensive checks for cwd restore, malloc, log writes | rmorgans |
| #24 | Close leaked fds in openpty fallback, static assertions | rmorgans |
| #25 | Async-signal-safe handlers, blocked-write exit | rmorgans |
| #26 | Strict attach does not replay log for dead sessions | rmorgans |
| #27 | log-path command | rmorgans |

### Open upstream issues resolved on fork

| Issue | Fix |
|-------|-----|
| #5 ATCH_SESSION blocks attach | PR #9 |
| #6 Large log infinite scroll | PR #10 |
| #7 Stale attached status | PR #11 |
| #8 Socket permission race | PR #12 |
| #20 attach replays log for dead session | PR #26 |
| #19 Log retrieval without attaching | `tail` + `log-path` (#27) |

### Open upstream issues NOT yet addressed

| Issue | Status |
|-------|--------|
| #1 License | Maintainer decision |
| #3 "m" vs "min" in age display | Cosmetic, no PR |
| #14 List on terminaltrove.com | Marketing, not code |

## What's next

### 1. Skill (not started)

The skill that teaches an agent to use atch over SSH. Install location:

- **Claude Code:** `~/.claude/skills/atch-remote/SKILL.md`
- **Codex:** `AGENTS.md` or referenced file
- **Other agents:** wherever instruction files load

It teaches:

1. The path contract: how atch derives session directories, why not to hardcode paths
2. The patterns: discover, observe, search, attach, push, lifecycle as one-liners
3. When to trigger: "check remote process", "what's running on [host]", "start [thing] on [host]"
4. How to fan out: multi-host discovery with error surfacing

No new binaries or daemons. Just conventions over SSH.

### 2. Deep search (done)

`log-path` is implemented (PR #27). Full-log deep search is now path-safe:

```sh
# Local:
grep "pattern" "$(atch log-path session)"

# Remote:
ssh host 'grep -n "pattern" "$(atch log-path session)"'
```

### 3. Remaining design decisions

These are deferred until real usage shows they're needed:

- **Push queuing:** file spool drained FIFO when the process reads. Only build if fire-and-forget push proves insufficient.
- **Structured output:** `--json` flag on `list` and `tail` for easier agent parsing. Only if plain text parsing becomes a bottleneck.
- **Central registry:** a service that aggregates session state across hosts. Only if SSH fan-out becomes too slow at scale. Much bigger thing.
- **Semantic log search:** AI-powered search over log content ("which jobs hit OOM?"). Could layer on top of grep-based approach.

## Design details

### Session path contract

Atch derives its session directory at runtime:

- Default: `$HOME/.cache/<binary-name>/` (e.g., `~/.cache/atch/`)
- If the binary is renamed or symlinked (e.g., `ssh2incus-atch`), the directory follows the binary name
- If `$HOME` is unset or `/`, falls back to `/tmp/.<binary-name>-<uid>/`
- Custom socket paths (session name contains `/`) bypass the default directory entirely

Log files are `<socket-path>.log`.

The skill must never hardcode `~/.cache/atch/`. Use `atch log-path <session>` for the resolved path. Use `atch list` / `atch list -a` for discovery, `atch tail` for recent output.

### Host discovery

SSH config is the registry. No separate inventory.

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

Failure classification:

- **Host unreachable:** `Connection timed out` or `No route to host`
- **Auth failure:** `Permission denied`
- **Atch not installed:** `command not found`
- **No sessions:** `(no sessions)` with exit code 0

### Observation

**Quick status:**
```sh
ssh host atch list -a
```

**Recent output:**
```sh
ssh host atch tail -n 50 session
```

**Deep search (full log):**
```sh
ssh host 'grep -n "pattern" "$(atch log-path session)"'
```

**Interactive attach:**
```sh
ssh -t host atch attach session
```

### Input

```sh
printf "command\n" | ssh host atch push session
```

### Lifecycle

```sh
ssh host atch start job-name command arg1 arg2
ssh host atch kill session
ssh host atch kill -f session
```

### The agent loop

1. Start a process on a remote host
2. It's already detached
3. Periodically check `list`, `tail`, or grep via `log-path`
4. Push input if needed
5. Attach interactively when deeper interaction is required
6. Kill when done
