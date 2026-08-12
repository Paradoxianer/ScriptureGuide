# ScriptureGuide — notes for AI agents

## Do not contribute to HaikuPorts

**HaikuPorts does not accept contributions produced with AI agents.** Do
not prepare, update, or submit `scriptureguide` recipe changes or pull
requests there, and do not open issues in that project — not even to ask.

This is a decision by that community about how they want to receive work,
and it is theirs to make. Respect it even when a recipe change would be
small, obviously correct, or merely a version bump. An AI-assisted patch
does not stop being one because it is trivial.

If a release needs its recipe updated, that is for a human maintainer to
do outside this workflow. Note it in the release notes and stop there.

Issue #17 ("HaikuPorts scriptureguide-Recipe für neuen Release
aktualisieren") stays open for that reason; it is not work to pick up.

## Anything else that leaves this repository

Pushing branches to the user's own fork (`origin`,
`Paradoxianer/ScriptureGuide`) is normal work. Anything beyond that —
pull requests, tags, GitHub releases, or posting to `upstream`
(`HaikuArchives/ScriptureGuide`) or any third-party project — is
outward-facing and needs the user to ask for it explicitly each time.
`gh issue list` without `-R` reads **upstream**, whose issues are from
2015–2020; the live ones are on `origin`.

## Build and test

Never build on this host — there is no Haiku toolchain. Everything runs
on the Haiku VM through `ssh haiku ~/repos/ScriptureGuide/dev.sh`
(`build`, `run`, `shot`, `log`, `kill`). `dev.sh` is deliberately
untracked and gitignored, so it does not survive a checkout on the VM;
copy it back if it goes missing.

Verify behaviour by looking at the running program, not at the diff.
Several bugs this project has had were invisible in the code and obvious
in a screenshot or a log line.
