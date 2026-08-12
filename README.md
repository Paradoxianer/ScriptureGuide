ScriptureGuide
=======================================

ScriptureGuide (formerly Be-Logos) is a Bible study program for
[Haiku](https://www.haiku-os.org/), licensed under the GPLv2 and built on
the [SWORD library](http://www.crosswire.org/sword/). It ships with a
companion package manager, ScriptureGuideManager, for downloading Bibles,
commentaries, dictionaries, and other SWORD modules.

Current release: **1.2.0** (test release, see [CHANGELOG.md](CHANGELOG.md)).

Features
--------

- Any number of Bible, commentary, or personal-notes columns shown side by
  side. Columns can be freely linked into "chains" that scroll and navigate
  together, or split off to browse independently -- each chain keeps its
  own book/chapter/verse and its own scrollbar.
- Drag and drop of selected verses between columns, to the Desktop, or to
  other applications, and back again.
- A single Go to / Search field that jumps straight to a typed reference or
  runs a search, whichever the text turns out to be.
- Cross-reference detection in commentary text (e.g. "see Mt 16:18"),
  clickable to jump straight there.
- Strong's number highlighting with click-to-look-up.
- A Dictionary/Lexicon window for installed SWORD dictionary modules.
- Personal notes per verse, saved automatically.
- Structured export of a comparison ("Copy Comparison") as plain text,
  tab-separated, Markdown, or HTML.
- Column layout (which modules are open, notes, and linking) is restored
  automatically the next time you start the app.
- Translated interface (German among other languages) with locale-aware
  verse references (e.g. `Johannes 3, 16` vs. `John 3:16`).
- A Book Manager (Program menu -- Book Manager…) to download and remove
  Bibles, commentaries, and dictionaries, with a consent warning before any
  network access.

See `App/docs/index.html` for a fuller, illustrated manual, and
`App/README.htm` for the short in-app welcome page.

Building
--------

ScriptureGuide only builds on Haiku. From a checkout:

```
cd ScriptureGuide && make && make bindcatalogs
cd ../ScriptureGuideManager && make
```

`make bindcatalogs` embeds the translated `locales/*.catkeys` into the
binary -- without it the interface stays in English regardless of the
system locale. Both binaries end up in `App/`.

If you'd rather not build by hand, `install.sh` at the repo root
downloads a tagged release, builds both apps, and installs them to
`~/apps/ScriptureGuide` with Desktop launcher symlinks -- handy for
testing on a machine that isn't covered by an official HaikuDepot package
build yet (see `App/INSTALL.htm` for details and requirements, including
the SWORD library).

Tests live under `ScriptureGuide/tests/` (`make` there builds and runs a
small headless regression suite).

`awk`, `unzip`, and `wget` are required by the Book Manager until a more
suitable method of downloading books is available.
