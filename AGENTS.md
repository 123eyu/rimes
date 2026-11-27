# Repository instructions for coding agents

## Release authorization

- When the owner asks to release a version, that request authorizes the complete release: the necessary branch and PR work, remote CI, tags, signing, public GitHub Release, and App Store upload and submission within the requested scope. Do not ask again for approval at each stage.
- Keep the technical release gates, immutable tags and assets, credentials, and user data protected. Report actual checks and store-review status accurately; request user action only for genuinely unavailable access, information, or an unresolved decision.

## Contribution attribution

- Add a `Co-authored-by` trailer to a commit when an AI coding agent materially designed, implemented, tested, or reviewed that commit.
- Use only a stable public identity that has been verified against the provider or its GitHub account. Do not invent names or email addresses.
- For Codex, use `Co-authored-by: Codex <267193182+codex@users.noreply.github.com>`.
- Credit temporary child or review agents through their parent agent unless the child has a persistent public identity of its own.
- Do not credit an agent that did not participate in the commit.
- Do not rewrite already-pushed history solely to add attribution unless a maintainer explicitly requests it.
- Keep the public agent list and attribution policy in `CONTRIBUTORS.md` current when a new coding-agent family materially contributes.
