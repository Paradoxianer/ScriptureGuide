/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef PERSONAL_NOTES_MODULE_H
#define PERSONAL_NOTES_MODULE_H

#include <String.h>
#include <SupportDefs.h>

#include <rawcom.h>
#include <versekey.h>

using namespace sword;


// Wraps a local, writable SWORD "raw commentary" module (RawCom) used to
// store the user's per-verse notes. Sharing the Bible's versification
// scheme means a note is automatically correctly associated with its verse
// key, exactly like BibleTime/Xiphos "personal commentary" notes.
class PersonalNotesModule {
public:
								PersonalNotesModule(
									const char* versification = "KJV");
	virtual						~PersonalNotesModule();

			status_t			Open();
			bool				IsOpen() const
									{ return fModule != NULL; }

			RawCom*				Module() const
									{ return fModule; }

			BString				GetNote(const char* key) const;
			status_t			SetNote(const char* key, const char* text);

private:
			status_t			_EnsureModuleExists();

private:
			BString				fPath;
			BString				fVersification;
			RawCom*				fModule;
};

#endif // PERSONAL_NOTES_MODULE_H
