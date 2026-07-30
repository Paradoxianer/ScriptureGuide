/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "NoteFieldView.h"

#include <Language.h>
#include <Locale.h>

#include <versekey.h>

#include "ParallelBibleView.h"
#include "PersonalNotesModule.h"

using namespace sword;


// See the identical helper in BibleTextDocument.cpp/ParallelBibleView.cpp:
// VerseKey::setText() only recognizes localized book names (e.g. German
// "1. Mose") if the key's locale has been set first, otherwise it fails
// silently and the key is left unchanged.
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}


NoteFieldView::NoteFieldView(int verse, PersonalNotesModule* notes,
	const BString& chapterKey, ParallelBibleView* owner, int32 group)
	:
	BTextView("noteField"),
	fVerse(verse),
	fNotes(notes),
	fChapterKey(chapterKey),
	fOwner(owner),
	fGroup(group)
{
	SetWordWrap(true);
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetInsets(2.0f, 2.0f, 2.0f, 2.0f);
}


void
NoteFieldView::MakeFocus(bool focus)
{
	BTextView::MakeFocus(focus);
	if (focus && fOwner != NULL)
		fOwner->SetActiveGroup(fGroup);
}


void
NoteFieldView::InsertText(const char* text, int32 length, int32 offset,
	const text_run_array* runs)
{
	BTextView::InsertText(text, length, offset, runs);
	_Save();
}


void
NoteFieldView::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);
	_Save();
}


void
NoteFieldView::_Save()
{
	VerseKey key;
	SetVerseKeyLocale(key);
	key.setText(fChapterKey.String());
	key.setVerse(fVerse);
	fNotes->SetNote(key.getText(), Text());
}
