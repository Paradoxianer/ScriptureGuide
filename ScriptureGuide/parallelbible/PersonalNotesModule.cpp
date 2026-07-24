/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "PersonalNotesModule.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Language.h>
#include <Locale.h>
#include <Path.h>


// See the identical helper in BibleTextDocument.cpp/ParallelBibleView.cpp:
// VerseKey::setText() only recognizes localized book names (e.g. German
// "1. Mose") if the key's locale has been set first, otherwise it fails
// silently and the key is left unchanged. Every key handed to GetNote()/
// SetNote() ultimately comes from the main window's (localized) book menu,
// so without this, every note silently collapsed onto whatever the
// default-constructed VerseKey's key happened to be, regardless of which
// verse was actually being edited.
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}


PersonalNotesModule::PersonalNotesModule(const char* versification)
	:
	fVersification(versification),
	fModule(NULL)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK) {
		path.Append("ScriptureGuide");
		path.Append("notes");
		fPath = path.Path();
	}
}


PersonalNotesModule::~PersonalNotesModule()
{
	delete fModule;
}


status_t
PersonalNotesModule::Open()
{
	if (fPath.IsEmpty())
		return B_ERROR;

	status_t status = _EnsureModuleExists();
	if (status != B_OK)
		return status;

	delete fModule;
	fModule = new RawCom(fPath.String(), "ScriptureGuideNotes",
		"Personal Notes", 0, ENC_UTF8, DIRECTION_LTR, FMT_PLAIN,
		"en", fVersification.String());

	return B_OK;
}


BString
PersonalNotesModule::GetNote(const char* key) const
{
	BString note;
	if (fModule == NULL)
		return note;

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(key);
	fModule->setKey(verseKey);

	if (fModule->hasEntry(&verseKey))
		note = fModule->getRawEntryBuf().c_str();

	return note;
}


status_t
PersonalNotesModule::SetNote(const char* key, const char* text)
{
	if (fModule == NULL)
		return B_NO_INIT;

	if (!fModule->isWritable())
		return B_NOT_ALLOWED;

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(key);
	fModule->setKey(verseKey);

	if (text == NULL || text[0] == '\0')
		fModule->deleteEntry();
	else
		fModule->setEntry(text);

	return B_OK;
}


status_t
PersonalNotesModule::_EnsureModuleExists()
{
	BPath path(fPath.String());
	BPath parent;
	path.GetParent(&parent);

	create_directory(parent.Path(), 0755);
	create_directory(fPath.String(), 0755);

	BDirectory dir(fPath.String());
	if (dir.InitCheck() != B_OK)
		return B_ERROR;

	// A freshly created directory has no RawCom index files yet;
	// only then do we need to lay down a new, empty module.
	BPath otVss(fPath.String());
	otVss.Append("ot.vss");
	if (!BEntry(otVss.Path()).Exists())
		RawCom::createModule(fPath.String(), fVersification.String());

	return B_OK;
}
