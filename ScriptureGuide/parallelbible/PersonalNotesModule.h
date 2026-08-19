/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef PERSONAL_NOTES_MODULE_H
#define PERSONAL_NOTES_MODULE_H

#include <String.h>
#include <SupportDefs.h>

#include <rawcom.h>
#include <swmodule.h>
#include <versekey.h>

using namespace sword;


// True for a module the user may edit in place.
//
// NOT SWModule::isWritable(), which is no use as a permission: SWORD
// answers yes for plain zText Bibles and for published commentaries
// alike (AKJV, ASV, KJV, MAK, Rieger all report writable). Gating an
// edit mode on it would make every Bible column editable.
//
// RawFiles is the discriminator, and it is SWORD's own convention rather
// than our invention: the "Personal" commentary CrossWire ships declares
// ModDrv=RawFiles precisely because it stores one file per entry, which
// is what taking per-entry writes requires. Every consolidated
// commentary uses another driver.
bool							IsEditableVerseModule(SWModule* module);


// Wraps a writable, verse-keyed SWORD module holding the user's own text
// for each verse.
//
// Two kinds, and the difference is ownership rather than behaviour:
//
//  - Our own notes module, created on first use under
//    settings/scriptureguide/notes and owned by this object.
//  - Any writable module already installed -- above all SWORD's
//    "Personal" commentary, which is what BibleTime and Xiphos edit.
//    That one belongs to the SWMgr that opened it and is only borrowed
//    here.
//
// Everything below the constructor is the same for both: the calls this
// makes -- setKey, hasEntry, getRawEntryBuf, isWritable, setEntry,
// deleteEntry -- are all plain SWModule, so nothing here depends on which
// driver is underneath.
class PersonalNotesModule {
public:
								PersonalNotesModule(
									const char* versification = "KJV");
								// Borrows an already-open module; Open()
								// creates nothing and the module outlives
								// this object.
								PersonalNotesModule(SWModule* module);
	virtual						~PersonalNotesModule();

			status_t			Open();
			bool				IsOpen() const
									{ return fModule != NULL; }

			SWModule*			Module() const
									{ return fModule; }
			// False for a borrowed module: it is not ours to create,
			// migrate or delete.
			bool				IsOwnModule() const
									{ return fOwnsModule; }

			BString				GetNote(const char* key) const;
			status_t			SetNote(const char* key, const char* text);

private:
			status_t			_EnsureModuleExists();
			void				_MigrateLegacyNotes();

private:
			BString				fPath;
			BString				fLegacyPath;
			BString				fVersification;
			SWModule*			fModule;
			bool				fOwnsModule;
};

#endif // PERSONAL_NOTES_MODULE_H
