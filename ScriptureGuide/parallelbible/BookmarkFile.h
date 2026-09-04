/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef BOOKMARK_FILE_H
#define BOOKMARK_FILE_H

#include <GraphicsDefs.h>
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
// than one -- see issue #55's own reasoning. VerseListFile, the class
// that implemented the old shape, has since been deleted: issue #59
// (import/export between the two) is off the table because that format
// never reached a real user, so there was nothing left for it to read.
//
// What this buys, once a reference IS a file: Tracker becomes a free
// browser for a collection (sortable by whatever attributes below expose,
// no custom widget needed for that alone); copying a reference between
// collections is a file copy; tags are BFS attributes Tracker's own Find
// panel already understands, once each reference has its own file to tag.
//
// Attributes are authoritative -- reference, versification and locale
// (SG:reference/SG:versification/SG:locale) are read from there first,
// same as Position/Code/Tags always have been. The file's plain-text
// content is a human-readable *mirror* of those same attributes, kept
// in sync on every Save() but never itself trusted while the attributes
// are present. It exists for exactly the case attributes can't cover:
// a copy off BFS (git, a zip, another filesystem) loses every
// attribute, so SetTo() falls back to parsing the content when
// SG:reference is missing or empty -- and then writes the attributes
// back from what it just recovered, so a file only needs that fallback
// once.
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
// #44: "#rrggbb" <-> rgb_color, shared by BookmarkFile and whatever
// builds the palette. Free functions rather than members because the
// colour also has to be read back out of a folder's own attributes.
bool			ParseHighlightColor(const char* value, rgb_color& outColor);
BString			FormatHighlightColor(rgb_color color);


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
			// The same testament+book+chapter+verse sort key already
			// computed and written as the SG:code attribute (for
			// Tracker's own column sorting, see EnsureMimeTypeRegistered())
			// -- a plain lexicographic sort of this string equals true
			// Bible order, unlike a plain string sort of Reference()
			// itself (which would put e.g. "Genesis 10:1" before
			// "Genesis 2:1"). Recomputed on demand rather than cached,
			// so it can't silently go stale relative to Reference()/
			// Versification()/Locale() after SetReference().
			BString			Code() const;

			// The chapter part of Code() -- testament + book + chapter,
			// without the verse. Comparing two of these answers "same
			// chapter?" without re-parsing either reference under a
			// locale it may not have been written in, which is why
			// highlight lookup and removal both work this way. Empty
			// when the reference does not parse at all.
			BString			ChapterCode() const;

			// Same, for a reference that is not (yet) a bookmark --
			// a document's current key, say.
			static BString	ChapterCodeFor(const char* reference,
										const char* versification,
										const char* locale);

			// #44: optional highlight attributes. A bookmark carrying
			// them marks a stretch INSIDE one verse, in one specific
			// module, rather than the whole verse -- SpanModule() empty
			// means "no span, the whole verse". Offsets are CHARACTER
			// offsets into that verse's own normal-form text (see
			// BibleTextDocument::VersePositionAt()), and SpanText() is
			// the text they covered when it was written, kept so a
			// module update that shifts the offsets can be healed
			// against it rather than silently mislocating the mark.
			//
			// Deliberately additional attributes on the existing
			// bookmark type, not a second file type: Haiku MIME types
			// are strictly two-level with no attribute inheritance, so a
			// "sub-type that inherits bookmark attributes" is not a
			// thing that exists, and maintaining two parallel attr_info
			// blocks would be worse anyway.
			const char*		SpanModule() const
									{ return fSpanModule.String(); }
			int32			SpanStart() const
									{ return fSpanStart; }
			int32			SpanEnd() const
									{ return fSpanEnd; }
			// The LAST verse a span covers. Equal to the bookmark's own
			// verse for the ordinary single-verse case; greater when the
			// highlight runs across a contiguous range, in which case
			// SpanStart() is an offset into the first verse and
			// SpanEnd() one into the last, with everything between
			// covered whole. Stored so a range stays ONE bookmark --
			// splitting it into one file per verse would make the
			// obvious next step, turning it into a verse-list entry like
			// "1. Mose 1:4-10", impossible to reconstruct.
			int32			SpanEndVerse() const
									{ return fSpanEndVerse; }
			const char*		SpanText() const
									{ return fSpanText.String(); }
			// True only for a highlight tied to ONE translation's own
			// character offsets. A highlight with a colour but no span
			// is verse-wide and shows in every column -- what dragging a
			// selection across columns produces (#44).
			bool			HasSpan() const
									{ return !fSpanModule.IsEmpty()
										&& fSpanEnd > fSpanStart; }
			void			SetSpan(const char* module, int32 start,
									int32 end, const char* text,
									int32 endVerse = 0);

			// The highlight colour, stored on the bookmark itself rather
			// than implied by which folder it sits in, so a later
			// "highlight manager" offering custom colours needs no
			// migration. HasColor() is false for an ordinary bookmark.
			bool			HasColor() const
									{ return fHasColor; }
			rgb_color		Color() const
									{ return fColor; }
			void			SetColor(rgb_color color);

			void			SetReference(const char* referenceLine);
			// Companions to SetReference() -- Code()/NavigationKey() both
			// re-parse the reference under THIS bookmark's own locale and
			// versification, so setting a reference without them yields a
			// key that only parses by luck (a localized book name under
			// the default English locale simply fails).
			void			SetVersification(const char* versification);
			void			SetLocale(const char* locale);
			void			SetPosition(int32 position);
			void			SetTags(const char* tags);

			// Writes content and attributes back to Path(). SetTo() or
			// CreateNew() must have set a path first.
			status_t		Save();

			// Deletes the file itself -- the counterpart to
			// VerseListFile::RemoveLine(), just at file granularity now
			// instead of a line within a shared file.
			status_t		Remove();

			// settings/scriptureguide/verselists/ -- deliberately one
			// level shallower than VerseListFile::ListsDirectory()'s own
			// settings/scriptureguide/library/verselists/: nothing has
			// shipped with real bookmark files under the old path yet
			// (#55 hadn't reached any real user before this changed), so
			// there was nothing to preserve by keeping the extra
			// "library" segment around.
			static BString	RootDirectory();

			// #44: RootDirectory()/Highlights -- one sub-folder per
			// colour, each an ordinary collection. Created on demand.
			static BString	HighlightsDirectory();

			// Every highlight in every colour folder, one level deep.
			// Only bookmarks carrying BOTH a span and a colour are
			// returned, so anything else that ends up in there is
			// ignored rather than half-interpreted.
			static std::vector<BookmarkFile>	ListHighlights();

			// #44: the folder highlights of `color` belong in, created
			// on demand. Matched by the folder's OWN SG:colorvalue
			// attribute, never by its name, so a colour folder can be
			// renamed freely -- to "Was Gott für mich getan hat", say,
			// with its own Description.txt -- and the next highlight of
			// that colour still lands in it instead of resurrecting a
			// fresh "Red" beside it. `fallbackName` only names a folder
			// that has to be created.
			static BString	HighlightFolderForColor(rgb_color color,
											const char* fallbackName);

			// #44: every colour folder that exists, with the name the
			// user gave it and the colour it collects. What an
			// "on/off per colour" menu is built from, so the menu shows
			// whatever categories actually exist rather than the six the
			// palette happens to offer.
			struct HighlightCategory {
				BString		name;
				BString		path;
				rgb_color	color;
			};
			static std::vector<HighlightCategory>	ListHighlightCategories();

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

			// #44, all optional -- see the accessors above.
			BString			fSpanModule;
			int32			fSpanStart;
			int32			fSpanEnd;
			int32			fSpanEndVerse;
			BString			fSpanText;
			bool			fHasColor;
			rgb_color		fColor;
};

#endif // BOOKMARK_FILE_H
