# Branches of the vietnt/goose_gfx fork

This fork carries the gfx changes the Rome port needs on top of
[aardappel/goose_gfx](https://github.com/aardappel/goose_gfx).

- `rome`, the default branch, is upstream `main` plus our commits. All of
  our work is committed here. Programs pin it by commit hash, for example
  `import gfx from "github.com/vietnt/goose_gfx@<full hash>"`.
- `main` is an exact mirror of upstream `main`. Nothing is committed to it;
  it only moves to upstream's head.

## Syncing with upstream

Programs pin commits of `rome` by hash, so before rewriting it, tag its old
head; the tag keeps every pinned commit fetchable.

```sh
git remote add upstream https://github.com/aardappel/goose_gfx   # once
git fetch upstream
git push origin upstream/main:main                               # move the mirror
git push origin origin/rome:refs/tags/archive/rome-<date>        # keep pinned commits reachable
git checkout rome && git rebase upstream/main
python3 test/api_check.py
python3 test/run_gfx_tests.py --require-gpu --validate --goose <goose built from vietnt/goose rome>
git push --force-with-lease origin rome
```

When upstream has nothing new, `rome` is already on top of it and needs no
rebase. A new release of the compiler is tagged in vietnt/goose (see its
`docs/rome-fork.md`); this repository has no releases of its own.
