/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "BibleTextDocument.h"

#include <String.h>

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextDocument"


BibleTextDocument::BibleTextDocument(SWModule* module)
	:
	TextDocument(),
	fModule(module),
	fShowVerseNumbers(true),
	fSkipEmptyVerses(true)
{
	fVerseNumberStyle.SetBold(true);
	fParagraphStyle.SetJustify(true);

	_Rebuild();
}


BibleTextDocument::~BibleTextDocument()
{
}


const char*
BibleTextDocument::Key() const
{
	if (fModule == NULL)
		return NULL;
	return fModule->getKeyText();
}


const char*
BibleTextDocument::BookName() const
{
	if (fModule == NULL)
		return NULL;
	return ((VerseKey*)fModule->getKey())->getBookName();
}


int
BibleTextDocument::Chapter() const
{
	if (fModule == NULL)
		return 0;
	return ((VerseKey*)fModule->getKey())->getChapter();
}


int
BibleTextDocument::Verse() const
{
	if (fModule == NULL)
		return 0;
	return ((VerseKey*)fModule->getKey())->getVerse();
}


status_t
BibleTextDocument::SetKey(const char* key)
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey(fModule->getKeyText());
	verseKey.setText(key);
	// Deliberately not forcing verse 1 here, unlike SetChapter()/Next/
	// PrevChapter() (which always want the chapter's first verse):
	// _Rebuild() below always renders the whole chapter regardless of
	// which verse `key` names, so preserving it only affects Verse()
	// afterward -- which is exactly what ParallelBibleView::SetKey() uses
	// to scroll straight to the requested verse.
	fModule->setKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::SetChapter(const char* book, int chapter)
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey(fModule->getKeyText());
	if (book != NULL)
		verseKey.setBookName(book);
	verseKey.setChapter(chapter);
	verseKey.setVerse(1);
	fModule->setKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::NextChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey(fModule->getKeyText());
	verseKey.setChapter(verseKey.getChapter() + 1);
	verseKey.setVerse(1);
	fModule->setKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::PrevChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey(fModule->getKeyText());
	verseKey.setChapter(verseKey.getChapter() - 1);
	verseKey.setVerse(1);
	fModule->setKey(verseKey);

	_Rebuild();
	return B_OK;
}


void
BibleTextDocument::SetShowVerseNumbers(bool show)
{
	if (fShowVerseNumbers == show)
		return;

	fShowVerseNumbers = show;
	_Rebuild();
}


void
BibleTextDocument::SetSkipEmptyVerses(bool skip)
{
	if (fSkipEmptyVerses == skip)
		return;

	fSkipEmptyVerses = skip;
	_Rebuild();
}


int32
BibleTextDocument::ParagraphIndexForVerse(int verse) const
{
	for (size_t i = 0; i < fParagraphVerse.size(); i++) {
		if (fParagraphVerse[i] == verse)
			return (int32)i;
	}
	return -1;
}


int
BibleTextDocument::VerseForParagraphIndex(int32 index) const
{
	if (index < 0 || (size_t)index >= fParagraphVerse.size())
		return -1;
	return fParagraphVerse[index];
}


void
BibleTextDocument::SetVerseSpacing(const std::map<int, float>& spacing)
{
	fVerseSpacingBottom = spacing;
	_Rebuild();
}


void
BibleTextDocument::_Rebuild()
{
	// Remove(0, Length()) -- clearing by replacing the entire document
	// with empty text -- reliably leaves exactly one empty placeholder
	// paragraph behind at index 0 instead of reaching zero paragraphs
	// (the underlying TextDocument::Replace()/_Remove() guarantees "at
	// least one paragraph always exists", the same way an empty text
	// file conceptually still has one empty line). That placeholder is
	// harmless on its own, but our verse loop below always Append()s
	// rather than replacing it, so every verse's paragraph ends up one
	// index higher than fParagraphVerse (built alongside the very same
	// Append() calls) expects -- verse 1 silently maps to index 1, not
	// 0, and whatever the true index-0 paragraph is (the leftover
	// placeholder) gets misread as verse 1's content instead.
	//
	// Resetting the inherited TextDocument state directly sidesteps
	// Remove()'s placeholder guarantee entirely: a fresh, default-
	// constructed TextDocument's fParagraphs is a genuinely empty
	// vector, and TextDocument::operator=() is a plain field copy, not
	// the Insert/Remove machinery that leaves the placeholder behind.
	// BibleTextDocument's own members (fModule, fShowVerseNumbers, etc.)
	// are untouched since only the base-class subobject is reassigned.
	TextDocument::operator=(TextDocument());
	fParagraphVerse.clear();

	if (fModule == NULL) {
		Paragraph paragraph(fParagraphStyle);
		paragraph.Append(TextSpan(
			B_TRANSLATE("No module selected."), fVerseTextStyle));
		Append(paragraph);
		return;
	}

	// Build independent VerseKey objects from the module's current key
	// text rather than aliasing fModule->getKey() directly: passing the
	// module's own live key object back into setKey() crashes deep inside
	// SWORD (ListKey's copy constructor), since setKey() replaces its
	// internal key before cloning from what was just passed in.
	BString savedKeyText(fModule->getKeyText());
	VerseKey savedKey(savedKeyText.String());

	VerseKey iterKey(savedKeyText.String());
	int verseCount = iterKey.getVerseMax();
	for (int verse = 1; verse <= verseCount; verse++) {
		iterKey.setVerse(verse);
		fModule->setKey(iterKey);

		BString text(fModule->renderText());
		if (text.CountChars() < 1 && fSkipEmptyVerses)
			continue;

		// GBFPlain leaves paragraph markers behind; strip them so verses
		// don't carry stray blank lines or pilcrows into the layout.
		text.RemoveAll("\x0a\x0a");
		text.RemoveAll("\xc2\xb6 ");
		text.RemoveAll("<P> ");

		ParagraphStyle style(fParagraphStyle);
		std::map<int, float>::const_iterator spacing
			= fVerseSpacingBottom.find(verse);
		if (spacing != fVerseSpacingBottom.end())
			style.SetSpacingBottom(spacing->second);

		Paragraph paragraph(style);
		if (fShowVerseNumbers) {
			BString number;
			number << " " << verse << " ";
			paragraph.Append(TextSpan(number, fVerseNumberStyle));
		} else if (text.IsEmpty()) {
			// A paragraph whose only span is empty makes the whole
			// document's Length() undercount how many paragraphs actually
			// exist (Length() sums *text* length, not paragraph count),
			// which breaks Remove(0, Length())'s ability to clear them on
			// the next rebuild (see _Rebuild()'s CountParagraphs() check
			// above -- Remove() itself still no-ops on a zero length). A
			// single space keeps every paragraph's length nonzero and
			// still reads as an empty, clickable line to type a note into.
			text = " ";
		}
		paragraph.Append(TextSpan(text, fVerseTextStyle));
		Append(paragraph);
		fParagraphVerse.push_back(verse);
	}

	fModule->setKey(savedKey);
}
