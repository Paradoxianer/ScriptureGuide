/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "BibleTextDocument.h"

#include <Language.h>
#include <Locale.h>
#include <String.h>

#include <Catalog.h>

#include <stdio.h>
#include <string.h>

#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextDocument"


// Mirrors SGModule::IsGreek()/IsHebrew() (SwordBackend.cpp) -- duplicated
// rather than shared because this class works directly on the sword::
// SWModule layer and has no dependency on the app's SGModule wrapper.
static bool
IsGreekModule(SWModule* module)
{
	return strcmp(module->getLanguage(), "grc") == 0
		|| strcmp(module->getLanguage(), "el") == 0;
}


static bool
IsHebrewModule(SWModule* module)
{
	return strcmp(module->getLanguage(), "he") == 0;
}


// VerseKey::setText()/setBookName() only recognize localized book names
// (e.g. German "1. Mose" for Genesis) if the key's locale has been set
// first -- otherwise they fail silently and the key is left unchanged.
// fCurrentKey (see ParallelBibleView) ultimately comes from the main
// window's book menu, which is localized, so every VerseKey built from
// caller-supplied key text needs this before setText()/setBookName().
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}


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


// fModule->setKey() does not carry over its argument's locale -- verified
// empirically, it always resets the module's own persistent key back to
// the default ("en") locale no matter what the passed-in key had set.
// Every setter below therefore needs to reapply it afterward, or
// BookName()/getKeyText() silently revert to English right after any
// navigation call.
void
BibleTextDocument::_SetModuleKey(VerseKey& verseKey)
{
	fModule->setKey(verseKey);
	SetVerseKeyLocale(*(VerseKey*)fModule->getKey());
}


status_t
BibleTextDocument::SetKey(const char* key)
{
	if (fModule == NULL)
		return B_NO_INIT;

	// Seeded from the module's own current position via an explicit
	// setText() call, not the VerseKey(const char*) constructor -- once
	// _SetModuleKey() below keeps the module's key permanently localized,
	// getKeyText() itself starts returning localized text (e.g. "Johannes
	// 3:16"), and parsing that through a fresh, still-default-locale key
	// (what the constructor form does) silently mis-parses it -- confirmed
	// empirically, "Johannes 3:16" with no locale set resolves to
	// "Revelation of John" 1:1 instead. Locale has to be set before either
	// parse, not just the second one.
	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(fModule->getKeyText());
	verseKey.setText(key);
	// Deliberately not forcing verse 1 here, unlike SetChapter()/Next/
	// PrevChapter() (which always want the chapter's first verse):
	// _Rebuild() below always renders the whole chapter regardless of
	// which verse `key` names, so preserving it only affects Verse()
	// afterward -- which is exactly what ParallelBibleView::SetKey() uses
	// to scroll straight to the requested verse.
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::SetChapter(const char* book, int chapter)
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(fModule->getKeyText());
	if (book != NULL)
		verseKey.setBookName(book);
	verseKey.setChapter(chapter);
	verseKey.setVerse(1);
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::NextChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(fModule->getKeyText());
	verseKey.setChapter(verseKey.getChapter() + 1);
	verseKey.setVerse(1);
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::PrevChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(fModule->getKeyText());
	verseKey.setChapter(verseKey.getChapter() - 1);
	verseKey.setVerse(1);
	_SetModuleKey(verseKey);

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
BibleTextDocument::SetBaseFont(const BFont& font)
{
	BFont effective = _EffectiveFont(font);

	fVerseTextStyle.SetFont(effective);

	// SetFont() replaces the style's whole BFont, including its face, so
	// the bold weight set in the constructor has to be reapplied after it.
	fVerseNumberStyle.SetFont(effective);
	fVerseNumberStyle.SetBold(true);

	_Rebuild();
}


// Greek/Hebrew modules (fModule->getLanguage(), see IsGreekModule()/
// IsHebrewModule() above) get a family suited to those scripts instead of
// whatever the user picked for the rest of the reading pane -- baseFont's
// size carries over either way. Falls back to baseFont unchanged if that
// family isn't actually installed (SetFamilyAndFace() failing is the
// expected case on a system without it, not a bug -- see #21).
BFont
BibleTextDocument::_EffectiveFont(const BFont& baseFont) const
{
	const char* family = NULL;
	if (IsGreekModule(fModule))
		family = GREEK;
	else if (IsHebrewModule(fModule))
		family = HEBREW;

	if (family == NULL)
		return baseFont;

	font_family fontFamily;
	strlcpy(fontFamily, family, sizeof(font_family));

	BFont effective(baseFont);
	if (effective.SetFamilyAndFace(fontFamily, B_REGULAR_FACE) != B_OK)
		return baseFont;

	return effective;
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


bool
BibleTextDocument::TextRangeForVerseRange(int startVerse, int endVerse,
	int32& start, int32& end) const
{
	int32 startParagraph = ParagraphIndexForVerse(startVerse);
	int32 endParagraph = ParagraphIndexForVerse(endVerse);
	if (startParagraph < 0 || endParagraph < 0)
		return false;

	// Mirrors how TextDocument::ParagraphIndexFor() itself accumulates
	// paragraphOffset from Paragraph::Length() -- there's no public
	// "offset of paragraph N" lookup to reuse directly.
	int32 offset = 0;
	for (int32 i = 0; i < startParagraph; i++)
		offset += ParagraphAtIndex(i).Length();
	start = offset;

	for (int32 i = startParagraph; i <= endParagraph; i++)
		offset += ParagraphAtIndex(i).Length();
	end = offset;

	return true;
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
	//
	// Locale has to be set on savedKey/iterKey before parsing
	// savedKeyText, not left to the VerseKey(const char*) constructor --
	// _SetModuleKey() keeps the module's own key permanently localized,
	// so savedKeyText itself is already localized text (e.g. "1. Mose
	// 1:1") by the time this runs. Parsing that through a fresh,
	// default-locale key (what the constructor form does) silently
	// mis-parses it into "Revelation of John" 1:1 instead -- confirmed
	// empirically -- which fModule->setKey(savedKey) at the bottom of
	// this function then wrote back as the module's new position,
	// undoing whatever SetKey()/SetChapter() had just correctly set
	// right before calling _Rebuild().
	BString savedKeyText(fModule->getKeyText());
	VerseKey savedKey;
	SetVerseKeyLocale(savedKey);
	savedKey.setText(savedKeyText.String());

	VerseKey iterKey;
	SetVerseKeyLocale(iterKey);
	iterKey.setText(savedKeyText.String());
	int verseCount = iterKey.getVerseMax();

	// Commentary modules commonly link one entry across a whole verse
	// range (e.g. a single comment discussing verses 13-20) rather than
	// storing separate text per verse; renderText() then returns the
	// SAME long text for every linked verse. Without this check each of
	// those verses became its own full-size paragraph, forcing
	// VerseAligner to inflate every other column's spacing once per
	// linked verse instead of just once overall -- the more verses an
	// entry spans, the more the whole chapter's height balloons.
	VerseKey previousKey;
	bool havePreviousEntry = false;

	for (int verse = 1; verse <= verseCount; verse++) {
		iterKey.setVerse(verse);
		fModule->setKey(iterKey);

		bool linkedToPrevious = havePreviousEntry
			&& fModule->isLinked(&iterKey, &previousKey);

		BString text;
		if (!linkedToPrevious)
			text = fModule->renderText();

		fprintf(stderr, "[BibleTextDocument %s] verse %d linked=%d "
			"textLen=%d\n", fModule->getName(), verse, linkedToPrevious,
			(int)text.CountChars());

		if (text.CountChars() < 1 && fSkipEmptyVerses && !linkedToPrevious)
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

		if (!linkedToPrevious) {
			previousKey = iterKey;
			havePreviousEntry = true;
		}
	}

	_SetModuleKey(savedKey);
}
