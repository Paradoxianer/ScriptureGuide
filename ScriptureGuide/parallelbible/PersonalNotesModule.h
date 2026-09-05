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
// Everything below the constructor is the same for both -- setKey,
// getRawEntryBuf, isWritable, setEntry and deleteEntry are plain
// SWModule. One call turned out NOT to be driver-independent, which
// this comment used to claim: hasEntry() answers false on a RawFiles
// module for an entry written a moment earlier that getRawEntryBuf()
// then returns correctly, so GetNote() must not consult it.
class PersonalNotesModule {
public:
								// `path` overrides where the module is
								// created; NULL means the location set by
								// SetLocationOverride(), or
								// settings/scriptureguide/notes if that is
								// unset too. Only the real location is
								// ever migrated from the pre-1.2.3 one.
								PersonalNotesModule(
									const char* versification = "KJV",
									const char* path = NULL);
								// Borrows an already-open module; Open()
								// creates nothing and the module outlives
								// this object.
								PersonalNotesModule(SWModule* module);
	virtual						~PersonalNotesModule();

			// Redirects every default-located notes module built from
			// here on, including the ones ParallelBibleView creates for
			// its own columns. Exists for the test suite, which writes
			// real notes through a real module and must not do that in
			// the user's own: leftovers from an interrupted run used to
			// sit in their notes, and since #54 made notes searchable,
			// turn up in their search results.
	static	void				SetLocationOverride(const char* path);

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
			bool				fUsesDefaultLocation;

	static	BString				sLocationOverride;
			BString				fVersification;
			SWModule*			fModule;
			bool				fOwnsModule;
};

#endif // PERSONAL_NOTES_MODULE_H
