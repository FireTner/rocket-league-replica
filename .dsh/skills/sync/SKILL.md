---
name: sync
description: Commit local changes, push to origin, and clone all data including gitignored assets (decomp/, arena_assets/, collision_meshes/, submodule vendor file) to the second machine firetner@192.168.0.109. Use when the user says /sync or asks to commit, push and sync everything to the other machine.
---

# /sync — commit, push, and clone everything to machine B

Machine A (this repo): `/home/cachyfire/Projects/RocketLeagueLaptop`
Machine B (remote): `firetner@192.168.0.109:/home/firetner/Projects/RocketLeague`

## Step 0 — repo rules that override everything

- **Never commit inside `third_party/RocketSim`** (vendored, stays pristine —
  repo rule 2). Its only local dirt is the untracked `A1_VENDOR.txt`; sync it
  as a plain file, never `git add` it.
- **Never commit Psyonix-owned data.** `decomp/` and `arena_assets/` are
  gitignored user-local inputs — they travel by rsync, never by git.
- rsync is merge-only: **never `--delete`**.

## Step 1 — commit

1. `git status --short --branch --untracked-files=all` and `git diff --stat`.
2. If anything outside the submodule is dirty, commit it with a Conventional
   Commits message (`feat(scope): ...`).
3. `modified: third_party/RocketSim (untracked content)` alone means there is
   nothing to commit — say so and move on.

## Step 2 — push

`git push`. `Everything up-to-date` is a fine outcome; report the HEAD hash.

## Step 3 — authenticate to machine B

Key auth is **not** set up. Probe first:

```sh
ssh -o BatchMode=yes -o ConnectTimeout=8 firetner@192.168.0.109 'echo OK'
```

If that fails, ask the user for the SSH password (`ask_user_question`, one
question). Never write the password to disk. `sshpass` is NOT installed —
authenticate every ssh/rsync via a temp askpass helper:

```sh
printf '#!/bin/sh\nprintf "%%s" "$SSHPASS"\n' > /tmp/askpass.sh && chmod 700 /tmp/askpass.sh
export SSHPASS='<password>' SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force
setsid ssh -o StrictHostKeyChecking=accept-new firetner@192.168.0.109 '<cmd>'
```

`setsid` (no controlling tty) is what forces ssh to fall back to
SSH_ASKPASS. rsync inherits it through `-e 'ssh -o StrictHostKeyChecking=accept-new'`.

## Step 4 — rsync data (merge, no --delete)

From the repo root. No trailing slashes on multi-source dirs — trailing
slashes would merge the dirs' *contents* into dest instead of creating the
dirs:

```sh
setsid rsync -az --exclude='__pycache__/' --exclude='*.pyc' decomp arena_assets collision_meshes 'firetner@192.168.0.109:/home/firetner/Projects/RocketLeague/' -e 'ssh -o StrictHostKeyChecking=accept-new'
setsid rsync -az 'third_party/RocketSim/A1_VENDOR.txt' 'firetner@192.168.0.109:/home/firetner/Projects/RocketLeague/third_party/RocketSim/' -e 'ssh -o StrictHostKeyChecking=accept-new'
```

Deliberately excluded: `build/` (machine-specific binaries — rebuild with
`make all` on B), `render_pacing.log`, `__pycache__` / `*.pyc`.

## Step 5 — checksum verify (required, not optional)

```sh
(find decomp arena_assets collision_meshes -type f -not -name '*.pyc' -not -path '*__pycache__*' | LC_ALL=C sort | xargs md5sum; md5sum third_party/RocketSim/A1_VENDOR.txt) | LC_ALL=C sort -k2 > /tmp/sums_local.txt
setsid ssh -o StrictHostKeyChecking=accept-new firetner@192.168.0.109 'cd /home/firetner/Projects/RocketLeague && (find decomp arena_assets collision_meshes -type f -not -name "*.pyc" -not -path "*__pycache__*" | LC_ALL=C sort | xargs md5sum; md5sum third_party/RocketSim/A1_VENDOR.txt) | LC_ALL=C sort -k2' > /tmp/sums_remote.txt
diff /tmp/sums_local.txt /tmp/sums_remote.txt && echo CHECKSUMS-IDENTICAL
```

Sort with `LC_ALL=C sort -k2` on BOTH sides (locale sort-order differences
cause false diffs). File counts must match.

## Step 6 — advance machine B's checkout

Machine B's clone sits behind origin after the push. Fast-forward it:

```sh
setsid ssh -o StrictHostKeyChecking=accept-new firetner@192.168.0.109 'git -C /home/firetner/Projects/RocketLeague pull --ff-only'
```

Never force-push or reset machine B.

## Step 7 — clean up secrets

```sh
shred -u /tmp/askpass.sh /tmp/sums_local.txt /tmp/sums_remote.txt 2>/dev/null || rm -f /tmp/askpass.sh /tmp/sums_local.txt /tmp/sums_remote.txt
unset SSHPASS
```

Report: commit hash pushed, synced file count + `CHECKSUMS-IDENTICAL`, and
machine B's HEAD after the pull.
