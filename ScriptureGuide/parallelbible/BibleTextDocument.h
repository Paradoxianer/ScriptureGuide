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

			// Extra bottom spacing per verse, used by VerseAligner to keep
			// the same verse lined up across parallel columns. Replaces
			// any previous overrides and rebuilds the document once.
			void				SetVerseSpacing(
									const std::map<int, float>& spacing);

private:
			BFont				_EffectiveFont(const BFont& baseFont) const;
			void				_Rebuild();

private:
			SWModule*			fModule;

			CharacterStyle		fVerseNumberStyle;
			CharacterStyle		fVerseTextStyle;
			ParagraphStyle		fParagraphStyle;

			bool				fShowVerseNumbers;
			bool				fSkipEmptyVerses;

			// paragraph index -> verse number, rebuilt in _Rebuild()
			std::vector<int>	fParagraphVerse;

			// verse number -> extra SpacingBottom, set by VerseAligner
			std::map<int, float> fVerseSpacingBottom;
};

#endif // BIBLE_TEXT_DOCUMENT_H
