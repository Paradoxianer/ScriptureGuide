/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "PersonalNotesModule.h"

#include <string.h>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Language.h>
#include <Locale.h>
#include <Path.h>
#include <StringList.h>


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


bool
IsEditableVerseModule(SWModule* module)
{
	if (module == NULL || !module->isWritable())
		return false;
	const char* driver = module->getConfigEntry("ModDrv");
	return driver != NULL && strcmp(driver, "RawFiles") == 0;
}


PersonalNotesModule::PersonalNotesModule(const char* versification)
	:
	fVersification(versification),
	fModule(NULL),
	fOwnsModule(true)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK) {
		// Lowercase, matching PREFERENCES_PATH -- settings and the Book
		// Manager's package cache have always lived in settings/
		// scriptureguide/. BFS is case sensitive, so the capitalized name
		// used up to 1.2.2 was a second, separate directory appearing
		// beside the real one; reported from the outside as exactly that.
		path.Append("scriptureguide");
		path.Append("notes");
		fPath = path.Path();

		path.GetParent(&path);
		path.GetParent(&path);
		path.Append("ScriptureGuide");
		path.Append("notes");
		fLegacyPath = path.Path();
	}
}


// Borrows a module the SWMgr owns. No path, so Open() has nothing to
// create and nothing to migrate, and the destructor must not delete it.
PersonalNotesModule::PersonalNotesModule(SWModule* module)
	:
	fModule(module),
	fOwnsModule(false)
{
}


PersonalNotesModule::~PersonalNotesModule()
{
	if (fOwnsModule)
		delete fModule;
}


status_t
PersonalNotesModule::Open()
{
	// A borrowed module arrived open; there is nothing here to build.
	if (!fOwnsModule)
		return fModule != NULL ? B_OK : B_NO_INIT;

	if (fPath.IsEmpty())
		return B_ERROR;

	_MigrateLegacyNotes();

	status_t status = _EnsureModuleExists();
	if (status != B_OK)
		return status;

	delete fModule;
	fModule = new RawCom(fPath.String(), "ScriptureGuideNotes",
		"Personal Notes", 0, ENC_UTF8, DIRECTION_LTR, FMT_PLAIN,
		"en", fVersification.String());

	return B_OK;
}


// 1.2.0 through 1.2.2 wrote notes to settings/ScriptureGuide/notes while
// everything else used settings/scriptureguide/. Anyone who took those
// releases has real notes sitting in the wrong directory, so move them
// rather than quietly starting an empty module beside them.
//
// What counts as "already occupied" is the presence of a MODULE, not of a
// directory -- the same ot.vss test _EnsureModuleExists() uses. On a
// machine that has been through the old releases the new path routinely
// exists already, holding nothing but the pre-1.2 Notes.txt that
// LogosApp.cpp used to create; treating that as occupied would strand the
// real notes and start an empty module beside them (seen exactly so on the
// test machine: 51 bytes of Notes.txt at the new path, half a megabyte of
// actual notes at the old one).
//
// If both paths hold a module, nothing moves and neither is touched:
// merging two RawCom modules is not something to attempt behind the user's
// back.
void
PersonalNotesModule::_MigrateLegacyNotes()
{
	if (fLegacyPath.IsEmpty())
		return;

	BPath legacyVss(fLegacyPath.String());
	legacyVss.Append("ot.vss");
	if (!BEntry(legacyVss.Path()).Exists())
		return;

	BPath currentVss(fPath.String());
	currentVss.Append("ot.vss");
	if (BEntry(currentVss.Path()).Exists())
		return;

	BPath parent;
	BPath currentPath(fPath.String());
	if (currentPath.GetParent(&parent) != B_OK)
		return;
	create_directory(parent.Path(), 0755);
	create_directory(fPath.String(), 0755);

	BDirectory legacyDir(fLegacyPath.String());
	if (legacyDir.InitCheck() != B_OK)
		return;

	// Names are collected before anything moves: BDirectory's iterator
	// walks by index, so renaming entries out from under it while it is
	// still walking skips whichever one slides into the vacated slot.
	BStringList names;
	BEntry entry;
	while (legacyDir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) == B_OK)
			names.Add(BString(name));
	}

	// Entry by entry rather than renaming the whole directory, which would
	// fail against an existing target, and never over anything already
	// there -- an old Notes.txt at the new path is the user's and stays.
	for (int32 i = 0; i < names.CountStrings(); i++) {
		BPath source(fLegacyPath.String());
		source.Append(names.StringAt(i));
		BPath target(fPath.String());
		target.Append(names.StringAt(i));
		if (BEntry(target.Path()).Exists())
			continue;
		BEntry(source.Path()).Rename(target.Path(), false);
	}
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
