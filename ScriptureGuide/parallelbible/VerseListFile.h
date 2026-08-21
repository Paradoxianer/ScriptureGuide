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
// a topical study.
//
// Name, description and versification live in the file's own content, as
// three optional "# Name: ...", "# Description: ...", "# Versification:
// ..." lines before the references, NOT only as BFS attributes. BFS
// attributes do not survive a zip, an email attachment, a copy to a
// non-BFS filesystem or a paste into a forum post -- every one of which
// is an ordinary way to share a study plan -- so a file whose metadata
// lived only there would arrive elsewhere as a bare, uncounted list of
// references with no explanation and, worse, no recorded versification:
// exactly the silent misreading #46 exists to prevent, reopened for
// every list anyone shares.
//
// Attributes are still written on Save(), as a cache: they are what lets
// a folder of many lists show Tracker columns without opening and
// parsing every file. SetTo() always reads the content first and treats
// it as authoritative; an attribute is consulted only where the content
// says nothing (a file with no header at all -- which is deliberately
// still a valid, minimal list, exactly the plain "one reference per
// line" file described below).
//
// One reference per line otherwise, in the form
// BibleTextDocument::SetVerseList() already parses. ReferenceText()
// returns only those lines -- the header, if any, is stripped out, so
// nothing downstream has to learn to recognize it.
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

			// Appends one reference line and saves immediately -- the
			// file IS the list (#47's whole point), so "add an entry"
			// means write it now, not queue an in-memory change someone
			// has to remember to flush. SetTo() must have already
			// loaded a file (or CreateNew() built one) for this to have
			// anything to append to.
			status_t			AppendReference(const char* referenceLine);

			// Removes the Nth (0-based) line of ReferenceText() and saves
			// immediately -- the counterpart to AppendReference(), and
			// what BibleTextDocument::ListLineForParagraphIndex()'s index
			// refers to: both split the same text on "\n" the same way,
			// so a section's own paragraph index maps straight to the
			// line to delete. B_BAD_INDEX if there is no such line
			// (already removed by something else since the menu was
			// built -- Tracker or another window could always have
			// edited this file in the meantime).
			status_t			RemoveLine(int32 lineIndex);

			// settings/scriptureguide/library/verselists/, created on
			// first use.
			static BString		ListsDirectory();

			// Every list file directly inside ListsDirectory() -- NOT
			// recursive, so a file that lives inside a collection
			// subfolder (see ListCollectionNames()/ListCollectionPaths()
			// below) is not among these. Read fresh each call, since
			// files can appear or vanish here from Tracker at any
			// moment. Not cached anywhere.
			static std::vector<BString>	ListPaths();

			// The immediate subfolders of ListsDirectory() -- one per
			// collection (e.g. a reading plan, a topic), sorted the way
			// Tracker's own Name column would read. One level only,
			// deliberately: a collection's own subfolders (nested
			// collections) are not walked. Together with
			// ListCollectionPaths() this is what a cascading navigation
			// menu (one submenu per collection) is built from.
			static std::vector<BString>	ListCollectionNames();

			// Every list file directly inside
			// ListsDirectory()/collectionName -- same non-recursive
			// shape as ListPaths(), just scoped to one collection.
			static std::vector<BString>	ListCollectionPaths(
										const char* collectionName);

			// Changes this file's path to a new name inside
			// ListsDirectory() (or, when collectionName is non-NULL,
			// inside that collection subfolder -- created if it doesn't
			// exist yet) and saves there, leaving the original file on
			// disk untouched. What "Save As..." needs; CreateNew() is
			// for a brand-new, still-empty list, not for relocating one
			// that already has content.
			status_t			SaveAs(const char* displayName,
										const char* collectionName = NULL);

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
			// Splits raw file content into header fields and reference-
			// only text -- see the definition for the header format and
			// why parsing and composing are deliberately not the same
			// function.
			static void			_ParseBody(const BString& body,
									BString& name, BString& description,
									BString& versification,
									BString& referenceText);

private:
			BString				fPath;
			BString				fName;
			BString				fDescription;
			BString				fVersification;
			BString				fReferenceText;
};

#endif // VERSE_LIST_FILE_H
