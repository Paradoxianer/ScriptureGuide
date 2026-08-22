/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef BOOKMARK_FILE_H
#define BOOKMARK_FILE_H

#include <String.h>
#include <SupportDefs.h>

#include <vector>


// One reference, kept as one small plain-text file (#55) -- the "People
// file" pattern a real end user's own workflow described: instead of one
// file holding a title plus a whole list of references (VerseListFile,
// #47), each reference is its own file, and a folder of them IS the list.
// A subfolder is a nested list ("collection" below, to avoid overloading
// "list" for both the folder and what used to be VerseListFile's single
// file).
//
// Why this replaces VerseListFile rather than living alongside it: a
// second, competing structured format in the same library folder is worse
// than one -- see issue #55's own reasoning. VerseListFile itself is left
// in place (issue #59, import/export between the two shapes, still needs
// it to read an old-style whole-list file), it is just no longer what
// SGVerseListWindow writes going forward.
//
// What this buys, once a reference IS a file: Tracker becomes a free
// browser for a collection (sortable by whatever attributes below expose,
// no custom widget needed for that alone); copying a reference between
// collections is a file copy; tags are BFS attributes Tracker's own Find
// panel already understands, once each reference has its own file to tag.
//
// Content vs. attributes follows VerseListFile's own established split:
// the reference, its declared versification AND its declared locale live
// in the file's content (portable off BFS, matching the same #46-safety
// reasoning VerseListFile's own header comment already gives);
// Position/Code/Tags are attribute-only -- none of the three are needed
// to know WHAT reference this is, only how to sort/find it within its
// collection, so losing them on a non-BFS copy is an acceptable
// degradation the actual reference text never has.
//
// The reference is stored in whatever locale was active when it was
// written (e.g. German "Johannes 3:12-16"), NOT forced into English --
// a real user's own point: a German reader should see German book names
// on disk, in Tracker, everywhere, not an English-only canonical form.
// Locale is recorded alongside specifically so that stays portable: a
// bookmark's own reference has to be re-parsed with ITS OWN locale, not
// whatever locale happens to be active on whichever system opens it
// later (confirmed empirically that VerseKey::setText() fails outright
// on a localized book name under the wrong locale, or none at all) --
// see SGVerseListWindow::_NavigateToRow(), the one place this actually
// gets re-parsed back into a key.
class BookmarkFile {
public:
							BookmarkFile();

			// Reads an existing bookmark file: content plus attributes.
			status_t		SetTo(const char* path);

			// Creates a new bookmark file inside `collectionPath`, named
			// after the sanitized reference text, with a numbered-
			// collision fallback identical to VerseListFile::CreateNew()'s
			// own. Writes immediately. `locale` is whatever produced
			// `referenceLine`'s own book name (a BLanguage::Code(), e.g.
			// "de") -- empty means English/no locale, VerseKey's own
			// default.
			status_t		CreateNew(const char* collectionPath,
										const char* referenceLine,
										const char* versification,
										const char* locale,
										int32 position);

			const char*		Path() const
									{ return fPath.String(); }
			const char*		Reference() const
									{ return fReference.String(); }
			const char*		Versification() const
									{ return fVersification.String(); }
			const char*		Locale() const
									{ return fLocale.String(); }
			int32			Position() const
									{ return fPosition; }

			// Reference(), re-parsed under this bookmark's OWN recorded
			// Locale() and re-rendered with none set at all (English/
			// ASCII, recognized regardless of whatever locale is
			// CURRENTLY active -- confirmed empirically). What a
			// navigation message (SG_BIBLE) should carry, never
			// Reference() directly: BookFromKey()/ChapterFromKey()/
			// VerseFromKey() (LogosMainWindow.cpp's JumpToKey(), what
			// SG_BIBLE ultimately calls) always parse under the CURRENT
			// system locale, with no way to pass a different one in, so
			// this is the one form guaranteed to still work regardless
			// of whether that matches the locale this bookmark was
			// actually written in. Reference() itself if it doesn't
			// parse at all (should not normally happen -- Reference()
			// is exactly what CreateNew()/Save() wrote in the first
			// place).
			BString			NavigationKey() const;
			// Comma-separated -- deliberately a plain string, not a
			// std::vector<BString>, since the tag-management/filter UI
			// (separate issue) that would need structured access doesn't
			// exist yet; present in the format from day one so that UI
			// won't need a schema migration when it lands.
			const char*		Tags() const
									{ return fTags.String(); }

			void			SetReference(const char* referenceLine);
			void			SetPosition(int32 position);
			void			SetTags(const char* tags);

			// Writes content and attributes back to Path(). SetTo() or
			// CreateNew() must have set a path first.
			status_t		Save();

			// Deletes the file itself -- the counterpart to
			// VerseListFile::RemoveLine(), just at file granularity now
			// instead of a line within a shared file.
			status_t		Remove();

			// settings/scriptureguide/library/verselists/ -- same
			// physical directory VerseListFile::ListsDirectory() already
			// used, kept as-is so a user's existing collection folders
			// keep working rather than needing to move anything.
			static BString	RootDirectory();

			// Every bookmark file directly inside `collectionPath` (NOT
			// recursive -- a nested collection's own bookmarks are not
			// walked), sorted by their SG:position attribute (a file
			// missing it, e.g. dropped in from outside, sorts after every
			// file that has one, in filename order). Read fresh each
			// call, never cached, since Tracker or another window could
			// always have added or removed a file in the meantime.
			static std::vector<BString>	ListBookmarkPaths(
										const char* collectionPath);

			// The immediate subfolders of `parentPath` (defaulting to
			// RootDirectory() when NULL) -- one per collection, sorted the
			// way Tracker's own Name column would read. One level only,
			// deliberately, matching VerseListFile::ListCollectionNames()'s
			// own established scope: a collection's own nested
			// subfolders are reachable through Open's file panel, not the
			// cascading navigation menu.
			static std::vector<BString>	ListCollectionNames(
										const char* parentPath = NULL);

			// Creates `parentPath`/name (sanitized, numbered on
			// collision, same as CreateNew()'s own file-naming), and
			// returns the resulting path. Empty on failure.
			static BString	CreateCollection(const char* parentPath,
										const char* name);

			// Installs the MIME type in the database if it is not there
			// yet -- short/long description, this application as the
			// preferred handler, the Bookmark.hvif ribbon icon (see
			// ScriptureGuide.rdef's "bookmark_icon" resource), and the
			// attribute info that makes Position/Code/Tags show up as
			// Tracker columns. Idempotent, same as VerseListFile's own.
			static void		EnsureMimeTypeRegistered();

			static const char*	kMimeType;
			// The sibling file inside a collection folder that holds the
			// collection's own description -- a plain, uncontrolled text
			// file rather than a bookmark file with no reference set, per
			// the design discussion on #55: a bare text file isn't really
			// a second *structured* format, just the null case, and stays
			// editable with any plain text editor if this app isn't handy.
			static const char*	kDescriptionFileName;

private:
			BString			fPath;
			BString			fReference;
			BString			fVersification;
			BString			fLocale;
			int32			fPosition;
			BString			fTags;
};

#endif // BOOKMARK_FILE_H
