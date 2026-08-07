/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef BIBLE_TEXT_DOCUMENT_H
#define BIBLE_TEXT_DOCUMENT_H

#include <map>
#include <vector>

#include <SupportDefs.h>

#include <swmodule.h>
#include <versekey.h>

#include "CharacterStyle.h"
#include "ParagraphStyle.h"
#include "TextDocument.h"

using namespace sword;


// One Paragraph per verse. The paragraph index therefore doubles as the
// verse's position within the chapter (see ParagraphIndexForVerse());
// this is what allows a VerseAligner to match up verses across columns
// that each hold their own BibleTextDocument.
//
// Two BibleTextDocument instances can wrap the SAME SWModule* at once --
// e.g. the same translation open in two independently-scrolled column
// chains (see issue #12) -- even though SWModule itself has exactly one,
// mutable "current key" shared by however many documents happen to point
// at it. This class never trusts that shared state to still reflect ITS
// OWN position by the time it's read: fKeyText is this document's own,
// independent record of "where I am," updated on every SetKey()/
// SetChapter()/NextChapter()/PrevChapter() and consulted (never
// fModule->getKeyText()) by _Rebuild()/Key()/BookName()/Chapter()/
// Verse(). fModule's own key is treated as a write-only scratch value:
// set immediately before every read that needs it, never assumed to
// still hold whatever THIS document last set once another document
// sharing the module may have run in between.
class BibleTextDocument : public TextDocument {
public:
								// Does not take ownership of module; the
								// module must already have its render
								// filters attached and must outlive this
								// object. singleVerse, if > 0, seeds
								// fSingleVerse (see SetSingleVerse())
								// before the constructor's own initial
								// _Rebuild() runs -- constructing straight
								// into single-verse mode this way, instead
								// of via a separate SetSingleVerse() call
								// right after, skips that first _Rebuild()
								// ever rendering the WHOLE chapter only to
								// immediately re-render just one verse.
								// Confirmed live as more than cosmetic: a
								// notes column builds one of these PER
								// VERSE, so for a long chapter (Psalm 119,
								// 176 verses) that wasted whole-chapter
								// render happened 176 times over.
								BibleTextDocument(SWModule* module,
									int singleVerse = 0);
	virtual						~BibleTextDocument();

			const char*			Key() const;
			const char*			BookName() const;
			int					Chapter() const;
			int					Verse() const;

			status_t			SetKey(const char* key);
			status_t			SetChapter(const char* book, int chapter);
			status_t			NextChapter();
			status_t			PrevChapter();

			SWModule*			Module() const
									{ return fModule; }

			void				SetShowVerseNumbers(bool show);
			bool				ShowVerseNumbers() const
									{ return fShowVerseNumbers; }

			// Whether Strong's-tagged words (#27) get their own
			// clickable span at all -- when off, _Rebuild() skips
			// FindStrongsWordsInText() entirely rather than finding
			// spans and not styling them, so a verse renders as one
			// plain span exactly like it did before #27 existed. Same
			// idea for cross-reference detection (#28) and
			// SetShowCrossReferences()/ShowCrossReferences() below.
			// Both default to on, matching this app's behavior before
			// either had a way to be turned off.
			void				SetShowStrongsNumbers(bool show);
			bool				ShowStrongsNumbers() const
									{ return fShowStrongsNumbers; }

			void				SetShowCrossReferences(bool show);
			bool				ShowCrossReferences() const
									{ return fShowCrossReferences; }

			// Family/size apply to both verse text and verse
			// numbers; verse numbers keep their bold weight
			// regardless. The family is overridden for Greek/
			// Hebrew modules (see _EffectiveFont() in the .cpp) --
			// `font`'s size is what actually sticks in that case.
			void				SetBaseFont(const BFont& font);

			// When true (the default), verses with no rendered text are
			// left out entirely. The notes column needs the opposite: an
			// always-present, possibly-empty paragraph per verse so every
			// verse has a row the user can click into and type a note.
			void				SetSkipEmptyVerses(bool skip);
			bool				SkipEmptyVerses() const
									{ return fSkipEmptyVerses; }

			// When > 0, _Rebuild() renders ONLY this one verse of the
			// current chapter (still located via fKeyText's book/chapter --
			// the verse component of fKeyText itself is irrelevant once
			// this is set) instead of the whole chapter, and this becomes
			// the only paragraph the document ever has. 0 (the default)
			// means the normal whole-chapter behavior every other caller
			// relies on is unchanged.
			void				SetSingleVerse(int verse);
			int					SingleVerse() const
									{ return fSingleVerse; }

			// When true, every verse's paragraph ends with an explicit
			// "\n" span. Off by default: a Bible/Commentary column's
			// document is never edited, and its flat character offsets
			// feed verse-range lookups and Strong's/cross-reference
			// links, so it stays free of characters that aren't in the
			// module's own text.
			//
			// The notes column needs it on, because the vendored
			// TextDocument editing engine assumes it. TextDocument::
			// _Remove() decides "the line break between two paragraphs
			// was just removed, merge them" from the removal ending
			// exactly at a paragraph's end (see its own comment there).
			// In a document whose paragraphs carry no trailing "\n", that
			// condition is true whenever the user deletes a paragraph's
			// LAST CHARACTER -- so backspacing away the final letter of a
			// note silently merged that verse's paragraph with the next
			// verse's, destroying the one-paragraph-per-verse invariant
			// the gutter, VerseAligner and note saving all depend on.
			// With the "\n" present, that same condition means what the
			// engine intended, and the only offsets that can merge two
			// paragraphs are the "\n" itself -- which NotesDisplayView's
			// KeyDown() guard blocks outright.
			//
			// ParagraphLayout treats a trailing "\n" as ending the line
			// it is already on and adds no extra empty line after it
			// (verified in its _Init() line loop), so this costs no
			// visible height.
			void				SetParagraphsEndWithNewline(bool enabled);

			// Which kinds of Strong's number this document should render
			// as clickable links: only those a dictionary is actually
			// installed for (see HasStrongsDictionary() in
			// SwordBackend.h). Both default to true, which keeps every
			// caller that doesn't care behaving exactly as before.
			//
			// The point is not to save a failed lookup -- it is that a
			// styled, clickable word is a promise. On a system with only
			// a Greek dictionary installed, every Strong's-tagged word in
			// the whole Old Testament looked like a link and led nowhere
			// (confirmed: 1. Mose 1:1 offers H430, H853, H7225, H1254,
			// H8064 and H776, none of them resolvable).
			void				SetResolvableStrongsPrefixes(bool greek,
									bool hebrew);

			// -1 if the verse is not part of the currently loaded chapter
			int32				ParagraphIndexForVerse(int verse) const;
			int					VerseForParagraphIndex(int32 index) const;

			// True if this verse's paragraph is a continuation of the
			// same linked commentary entry as the verse before it (see
			// #10's linked-entry fix in _Rebuild()) -- i.e. this verse
			// has no real text of its own here, just a share of an
			// entry that actually covers a whole range starting at some
			// earlier verse. VerseAligner (#10) uses this to align a
			// whole linked span as one group instead of forcing all of
			// that span's height onto the one verse where the real text
			// landed. False for a verse not in this document at all.
			bool				IsLinkedToPrevious(int verse) const;

			// Text offset range covering verses startVerse..endVerse
			// (inclusive) -- for a TextDocumentView::SetSelection() call
			// to highlight a verse jumped to from search (see #22).
			// false if either end isn't part of the currently loaded
			// chapter, leaving start/end untouched.
			bool				TextRangeForVerseRange(int startVerse,
									int endVerse, int32& start,
									int32& end) const;

			// Verse references embedded in this column's own rendered
			// text -- e.g. a commentary citing "(Mt 16:18)" -- are
			// detected (see FindReferencesInText(), #28) and rendered as
			// a distinctly-styled sub-span of the verse's text, not a
			// separate paragraph or verse of their own. documentOffset
			// is a plain character offset into this TextDocument (the
			// same space TextDocumentView::TextOffsetAt() and
			// TextRangeForVerseRange() already use); outKey is the
			// normalized VerseKey text ready for SetKey(), untouched if
			// this returns false.
			bool				ReferenceLinkAt(int32 documentOffset,
									BString& outKey) const;

			// Same idea as ReferenceLinkAt(), for a word SWORD tagged
			// with a Strong's number (see StripStrongsMarkup(), #27) --
			// outNumber (e.g. "G3056") is what
			// SwordBackend::LookupStrongsNumber() expects, untouched if
			// this returns false.
			bool				StrongsNumberAt(int32 documentOffset,
									BString& outNumber) const;

			// Extra bottom spacing per verse, used by VerseAligner to keep
			// the same verse lined up across parallel columns. Replaces
			// any previous overrides and restyles the existing paragraphs
			// in place (see _ApplyVerseSpacing()) -- deliberately NOT a
			// full _Rebuild(): VerseAligner::Align() calls this twice per
			// document (once to clear, once to apply) on every _Realign(),
			// and a full rebuild would redo the SWORD renderText() fetch
			// plus Strong's-number/cross-reference detection for every
			// verse just to change a float, confirmed live as the
			// dominant cost in a chapter switch (profiling: ~90% of a
			// switch's time was inside VerseAligner::Align(), almost all
			// of it redundant _Rebuild() calls).
			void				SetVerseSpacing(
									const std::map<int, float>& spacing);

private:
			BFont				_EffectiveFont(const BFont& baseFont) const;
			void				_Rebuild();
			void				_ApplyVerseSpacing();
			void				_SetModuleKey(VerseKey& verseKey);

private:
			SWModule*			fModule;

			// This document's OWN current position, independent of
			// fModule's own (shared, mutable) key state -- see the class
			// comment on why this exists (issue #12: two columns can now
			// show the same module in different, independently-scrolled
			// chains at once). Updated by every SetKey()/SetChapter()/
			// NextChapter()/PrevChapter() call (via _SetModuleKey()) and
			// read back by Key()/_Rebuild() instead of fModule->
			// getKeyText(), which reflects whichever BibleTextDocument
			// sharing this module happened to touch it most recently --
			// not necessarily this one.
			BString				fKeyText;
			// Scratch VerseKey for BookName()/Chapter()/Verse() to parse
			// fKeyText into on demand -- a per-instance member (not a
			// local variable) purely because VerseKey::getBookName()
			// returns a pointer into the key object itself, which would
			// dangle if that key were stack-allocated and destroyed when
			// the accessor returns. mutable: those accessors are const.
			mutable VerseKey	fDisplayKey;

			CharacterStyle		fVerseNumberStyle;
			CharacterStyle		fVerseTextStyle;
			CharacterStyle		fReferenceLinkStyle;
			CharacterStyle		fStrongsNumberStyle;
			ParagraphStyle		fParagraphStyle;

			bool				fShowVerseNumbers;
			bool				fSkipEmptyVerses;
			bool				fShowStrongsNumbers;
			bool				fShowCrossReferences;
			int					fSingleVerse;
			bool				fParagraphsEndWithNewline;
			bool				fResolvableStrongsGreek;
			bool				fResolvableStrongsHebrew;

			// paragraph index -> verse number, rebuilt in _Rebuild()
			std::vector<int>	fParagraphVerse;

			// verse number -> IsLinkedToPrevious(), rebuilt alongside
			// fParagraphVerse in _Rebuild().
			std::map<int, bool>	fLinkedToPrevious;

			// Document-wide [start, end) offset ranges for every
			// reference link found across the whole chapter, rebuilt
			// alongside fParagraphVerse in _Rebuild() -- see
			// ReferenceLinkAt().
			struct ReferenceLink {
				int32	start;
				int32	end;
				BString	key;
			};
			std::vector<ReferenceLink>	fReferenceLinks;

			// Same idea, for Strong's-tagged words (see StrongsNumberAt(),
			// #27).
			struct StrongsLink {
				int32	start;
				int32	end;
				BString	number;
			};
			std::vector<StrongsLink>	fStrongsLinks;

			// verse number -> extra SpacingBottom, set by VerseAligner
			std::map<int, float> fVerseSpacingBottom;
};

#endif // BIBLE_TEXT_DOCUMENT_H
