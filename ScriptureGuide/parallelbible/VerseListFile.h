/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef VERSE_LIST_FILE_H
#define VERSE_LIST_FILE_H

#include <String.h>
#include <SupportDefs.h>

#include <vector>


// A named, ordered collection of references, kept as one plain-text file
// under settings/scriptureguide/lists/ (#47) -- a reading plan, a lesson,
// a topical study. One reference per line, in the form
// BibleTextDocument::SetVerseList() already parses; the file body IS the
// list text, nothing to translate between the two.
//
// Deliberately a file, not a SWORD module: every SWORD module answers
// "what text belongs to this key", and a verse list is the inverse -- a
// set of keys with no text of its own. See the storage discussion on
// issue #47 for the rest of that reasoning, and for why it is not shared
// with BibleTime/Xiphos either (they keep their own bookmarks in their
// own formats, not as SWORD modules).
class VerseListFile {
public:
								VerseListFile();

			// Reads an existing file: attributes plus body.
			status_t			SetTo(const char* path);

			// Creates a new file under the lists directory, named after
			// `displayName` (sanitized into a filename -- see
			// _SanitizeFileName()), and writes it immediately with one
			// seed line.
			//
			// A brand-new list is deliberately never actually empty:
			// SetVerseList("") means "show the chapter", so an empty
			// file would be indistinguishable from no list at all the
			// moment anything tried to render it. Seeding with whatever
			// reference the user was already looking at avoids the
			// question rather than answering it -- the same reasoning
			// _BuildNotesDocument() already uses to seed a fresh notes
			// column from the chain's current key.
			status_t			CreateNew(const char* displayName,
									const char* seedReferenceLine,
									const char* versification);

			const char*			Path() const
									{ return fPath.String(); }
			const char*			Name() const
									{ return fName.String(); }
			const char*			Description() const
									{ return fDescription.String(); }
			const char*			Versification() const
									{ return fVersification.String(); }
			// One reference per line -- exactly what
			// BibleTextDocument::SetVerseList() takes.
			const char*			ReferenceText() const
									{ return fReferenceText.String(); }
			int32				EntryCount() const;

			void				SetName(const char* name);
			void				SetDescription(const char* description);
			void				SetReferenceText(const char* text);

			// Writes attributes and body back to Path(). SetTo() or
			// CreateNew() must have set a path first.
			status_t			Save();

			// settings/scriptureguide/lists/, created on first use.
			static BString		ListsDirectory();

			// Every list file's path, in the order Tracker would show
			// them (BDirectory's own enumeration order) -- read fresh
			// each call, since files can appear or vanish here from
			// Tracker at any moment (#47's own note on the band's
			// popup). Not cached anywhere.
			static std::vector<BString>	ListPaths();

			// Installs the MIME type in the database if it is not there
			// yet -- short/long description, this application as the
			// preferred handler, and the attribute info that makes
			// name/description/count/versification show up as Tracker
			// columns. Idempotent (BMimeType::IsInstalled() guards it),
			// so every call site just calls it rather than tracking
			// whether it already ran.
			static void			EnsureMimeTypeRegistered();

			static const char*	kMimeType;

private:
			BString				_SanitizeFileName(const char* name) const;

private:
			BString				fPath;
			BString				fName;
			BString				fDescription;
			BString				fVersification;
			BString				fReferenceText;
};

#endif // VERSE_LIST_FILE_H
