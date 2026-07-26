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
class BibleTextDocument : public TextDocument {
public:
								// Does not take ownership of module; the
								// module must already have its render
								// filters attached and must outlive this
								// object.
								BibleTextDocument(SWModule* module);
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

			// -1 if the verse is not part of the currently loaded chapter
			int32				ParagraphIndexForVerse(int verse) const;
			int					VerseForParagraphIndex(int32 index) const;

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
			// any previous overrides and rebuilds the document once.
			void				SetVerseSpacing(
									const std::map<int, float>& spacing);

private:
			BFont				_EffectiveFont(const BFont& baseFont) const;
			void				_Rebuild();
			void				_SetModuleKey(VerseKey& verseKey);

private:
			SWModule*			fModule;

			CharacterStyle		fVerseNumberStyle;
			CharacterStyle		fVerseTextStyle;
			CharacterStyle		fReferenceLinkStyle;
			CharacterStyle		fStrongsNumberStyle;
			ParagraphStyle		fParagraphStyle;

			bool				fShowVerseNumbers;
			bool				fSkipEmptyVerses;

			// paragraph index -> verse number, rebuilt in _Rebuild()
			std::vector<int>	fParagraphVerse;

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
