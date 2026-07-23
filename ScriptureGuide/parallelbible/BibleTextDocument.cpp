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

	VerseKey* verseKey = (VerseKey*)fModule->getKey();
	verseKey->setText(key);
	verseKey->setVerse(1);
	fModule->setKey(*verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::SetChapter(const char* book, int chapter)
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey* verseKey = (VerseKey*)fModule->getKey();
	if (book != NULL)
		verseKey->setBookName(book);
	verseKey->setChapter(chapter);
	verseKey->setVerse(1);
	fModule->setKey(*verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::NextChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey* verseKey = (VerseKey*)fModule->getKey();
	verseKey->setChapter(verseKey->getChapter() + 1);
	verseKey->setVerse(1);
	fModule->setKey(*verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::PrevChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey* verseKey = (VerseKey*)fModule->getKey();
	verseKey->setChapter(verseKey->getChapter() - 1);
	verseKey->setVerse(1);
	fModule->setKey(*verseKey);

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
	if (Length() > 0)
		Remove(0, Length());
	fParagraphVerse.clear();

	if (fModule == NULL) {
		Paragraph paragraph(fParagraphStyle);
		paragraph.Append(TextSpan(
			B_TRANSLATE("No module selected."), fVerseTextStyle));
		Append(paragraph);
		return;
	}

	VerseKey* verseKey = (VerseKey*)fModule->getKey();
	VerseKey savedKey(*verseKey);

	int verseCount = verseKey->getVerseMax();
	for (int verse = 1; verse <= verseCount; verse++) {
		verseKey->setVerse(verse);
		fModule->setKey(*verseKey);

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
		}
		paragraph.Append(TextSpan(text, fVerseTextStyle));
		Append(paragraph);
		fParagraphVerse.push_back(verse);
	}

	fModule->setKey(savedKey);
}
