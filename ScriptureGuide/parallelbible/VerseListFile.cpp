/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#include "VerseListFile.h"

#include <algorithm>
#include <ctype.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Path.h>
#include <StringList.h>

#include "../constants.h"

const char* VerseListFile::kMimeType
	= "text/x-scriptureguide-verselist";

// Attribute names, kept short (BFS convention) and namespaced the same
// way PersonalNotesModule's own files are, so anything inspecting the
// directory can tell these apart from a stray text file at a glance.
static const char* kAttrName = "SG:name";
static const char* kAttrDescription = "SG:description";
static const char* kAttrVersification = "SG:versification";
static const char* kAttrCount = "SG:count";


VerseListFile::VerseListFile()
{
}


BString
VerseListFile::ListsDirectory()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return BString();
	path.Append("scriptureguide");
	path.Append("library");
	path.Append("verselists");
	create_directory(path.Path(), 0755);
	return BString(path.Path());
}


void
VerseListFile::EnsureMimeTypeRegistered()
{
	BMimeType mime(kMimeType);
	if (mime.IsInstalled())
		return;

	mime.Install();
	mime.SetShortDescription("ScriptureGuide verse list");
	mime.SetLongDescription("A named, ordered list of Bible references "
		"(ScriptureGuide)");
	mime.SetPreferredApp(SG_APP_SIGNATURE);

	BMessage extensions;
	extensions.AddString("extensions", "sgvl");
	mime.SetFileExtensions(&extensions);

	// Gives Tracker real columns for these -- "Type" already exists for
	// every file, "Name"/"Description"/"Versification"/"Entries" only
	// mean something for one of ours.
	BMessage attrInfo;
	attrInfo.AddString("attr:name", kAttrName);
	attrInfo.AddString("attr:public_name", "Name");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);

	attrInfo.AddString("attr:name", kAttrDescription);
	attrInfo.AddString("attr:public_name", "Description");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);

	attrInfo.AddString("attr:name", kAttrVersification);
	attrInfo.AddString("attr:public_name", "Versification");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);

	attrInfo.AddString("attr:name", kAttrCount);
	attrInfo.AddString("attr:public_name", "Entries");
	attrInfo.AddInt32("attr:type", B_INT32_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);

	mime.SetAttrInfo(&attrInfo);
}


std::vector<BString>
VerseListFile::ListPaths()
{
	std::vector<BString> paths;
	BDirectory dir(ListsDirectory().String());
	if (dir.InitCheck() != B_OK)
		return paths;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		BPath path;
		if (entry.GetPath(&path) == B_OK)
			paths.push_back(BString(path.Path()));
	}
	return paths;
}


std::vector<BString>
VerseListFile::ListCollectionNames()
{
	std::vector<BString> names;
	BDirectory dir(ListsDirectory().String());
	if (dir.InitCheck() != B_OK)
		return names;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsDirectory())
			continue;
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) == B_OK)
			names.push_back(BString(name));
	}
	std::sort(names.begin(), names.end());
	return names;
}


std::vector<BString>
VerseListFile::ListCollectionPaths(const char* collectionName)
{
	std::vector<BString> paths;
	if (collectionName == NULL || collectionName[0] == '\0')
		return paths;

	BPath path(ListsDirectory().String());
	path.Append(collectionName);
	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK)
		return paths;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		BPath entryPath;
		if (entry.GetPath(&entryPath) == B_OK)
			paths.push_back(BString(entryPath.Path()));
	}
	return paths;
}


// The three recognized header lines. Matched by prefix, order-
// independent, and optional -- a file with none of these is still a
// valid list, exactly the plain "one reference per line" file this
// class always accepted.
static const char* kHeaderName = "# Name: ";
static const char* kHeaderDescription = "# Description: ";
static const char* kHeaderVersification = "# Versification: ";


// Splits raw file content into header fields and reference-only text.
// Shared between SetTo() (parsing) and nothing else -- Save() has its
// own, much simpler composer, since round-tripping through the same
// function in both directions would mean tolerating on read whatever
// quirks a human editing the file by hand introduces, which is the
// point, but writing should always produce the one clean canonical
// form rather than preserving those quirks.
void
VerseListFile::_ParseBody(const BString& body, BString& name,
	BString& description, BString& versification, BString& referenceText)
{
	name = "";
	description = "";
	versification = "";
	referenceText = "";

	int32 lineStart = 0;
	while (lineStart <= body.Length()) {
		int32 lineEnd = body.FindFirst("\n", lineStart);
		if (lineEnd < 0)
			lineEnd = body.Length();

		BString line;
		body.CopyInto(line, lineStart, lineEnd - lineStart);

		if (line.StartsWith(kHeaderName))
			name = line.String() + strlen(kHeaderName);
		else if (line.StartsWith(kHeaderDescription))
			description = line.String() + strlen(kHeaderDescription);
		else if (line.StartsWith(kHeaderVersification))
			versification = line.String() + strlen(kHeaderVersification);
		else {
			BString trimmed(line);
			trimmed.Trim();
			// Blank lines and anything else starting with "#" (room for
			// a comment a human adds by hand, or a header line this
			// version doesn't recognize) are skipped rather than turned
			// into a reference SWORD would then fail to parse.
			if (!trimmed.IsEmpty() && trimmed.ByteAt(0) != '#') {
				if (!referenceText.IsEmpty())
					referenceText << "\n";
				referenceText << line;
			}
		}

		if (lineEnd >= body.Length())
			break;
		lineStart = lineEnd + 1;
	}
}


status_t
VerseListFile::SetTo(const char* path)
{
	BFile file(path, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	fPath = path;

	off_t size = 0;
	file.GetSize(&size);
	BString body;
	if (size > 0) {
		char* buffer = new(std::nothrow) char[size + 1];
		if (buffer != NULL) {
			ssize_t bytesRead = file.Read(buffer, size);
			if (bytesRead > 0) {
				buffer[bytesRead] = '\0';
				body = buffer;
			}
			delete[] buffer;
		}
	}

	_ParseBody(body, fName, fDescription, fVersification, fReferenceText);

	// The header lines are the source of truth; an attribute is only
	// consulted for whatever the content left blank -- a file that
	// arrived from somewhere its attributes did not survive, or one
	// written by hand with no header at all.
	BString attrValue;
	if (fName.IsEmpty() && file.ReadAttrString(kAttrName, &attrValue) == B_OK)
		fName = attrValue;
	if (fDescription.IsEmpty()
		&& file.ReadAttrString(kAttrDescription, &attrValue) == B_OK) {
		fDescription = attrValue;
	}
	if (fVersification.IsEmpty()
		&& file.ReadAttrString(kAttrVersification, &attrValue) == B_OK) {
		fVersification = attrValue;
	}

	// Still nothing: fall back to the filename itself, which -- unlike
	// an attribute -- is exactly as portable as the file is.
	if (fName.IsEmpty()) {
		BPath p(path);
		fName = p.Leaf();
		int32 dot = fName.FindLast(".sgvl");
		if (dot >= 0)
			fName.Truncate(dot);
	}

	return B_OK;
}


BString
VerseListFile::_SanitizeFileName(const char* name) const
{
	BString sanitized(name);
	sanitized.Trim();
	if (sanitized.IsEmpty())
		sanitized = "Untitled list";

	// BFS forbids '/' outright and is otherwise permissive, but a name
	// with no visible letters in it (only slashes, say) would collapse
	// to nothing -- guarded against by the fallback above running once
	// more after stripping.
	for (int32 i = 0; i < sanitized.Length(); i++) {
		if (sanitized.ByteAt(i) == '/')
			sanitized.SetByteAt(i, '-');
	}
	if (sanitized.Trim().IsEmpty())
		sanitized = "Untitled list";

	return sanitized;
}


status_t
VerseListFile::CreateNew(const char* displayName,
	const char* seedReferenceLine, const char* versification)
{
	EnsureMimeTypeRegistered();

	BString directory = ListsDirectory();
	if (directory.IsEmpty())
		return B_ERROR;

	// BPath::Append() adds a new path COMPONENT, not a string suffix --
	// a second Append(".sgvl") would create a directory named after the
	// list with a file called ".sgvl" inside it, confirmed the hard way.
	// The extension is concatenated onto the name first instead, so
	// Append() is called exactly once.
	BString baseName = _SanitizeFileName(displayName);

	BString fileName(baseName);
	fileName << ".sgvl";
	BPath path(directory.String());
	path.Append(fileName.String());

	// A second list with the same name gets a number rather than
	// silently overwriting the first -- Tracker does the same thing
	// when you duplicate a file into an occupied name.
	int suffix = 2;
	while (BEntry(path.Path()).Exists()) {
		fileName = baseName;
		fileName << " " << suffix << ".sgvl";
		path.SetTo(directory.String());
		path.Append(fileName.String());
		suffix++;
	}

	fPath = path.Path();
	fName = displayName;
	fDescription = "";
	fVersification = versification != NULL ? versification : "";
	fReferenceText = seedReferenceLine != NULL ? seedReferenceLine : "";

	return Save();
}


// Relocates this file to a new name (and, optionally, collection
// subfolder), keeping its current content -- the counterpart to
// CreateNew(), which always starts a fresh, still-empty list. The
// original file at the old path is left untouched, matching how "Save
// As..." behaves everywhere else (StyledEdit, a browser's "Save Page
// As...").
status_t
VerseListFile::SaveAs(const char* directory, const char* displayName)
{
	EnsureMimeTypeRegistered();

	if (directory == NULL || directory[0] == '\0')
		return B_BAD_VALUE;
	create_directory(directory, 0755);

	BString baseName = _SanitizeFileName(displayName);
	BString fileName(baseName);
	fileName << ".sgvl";
	BPath path(directory);
	path.Append(fileName.String());

	// Same collision handling as CreateNew(): a number, not a silent
	// overwrite.
	int suffix = 2;
	while (BEntry(path.Path()).Exists()) {
		fileName = baseName;
		fileName << " " << suffix << ".sgvl";
		path.SetTo(directory);
		path.Append(fileName.String());
		suffix++;
	}

	fPath = path.Path();
	fName = displayName;
	// fDescription/fVersification/fReferenceText are left exactly as
	// they are -- that is the entire point of "Save As" over CreateNew().

	return Save();
}


void
VerseListFile::SetName(const char* name)
{
	fName = name != NULL ? name : "";
}


void
VerseListFile::SetDescription(const char* description)
{
	fDescription = description != NULL ? description : "";
}


void
VerseListFile::SetReferenceText(const char* text)
{
	fReferenceText = text != NULL ? text : "";
}


int32
VerseListFile::EntryCount() const
{
	int32 count = 0;
	int32 lineStart = 0;
	while (lineStart < fReferenceText.Length()) {
		int32 lineEnd = fReferenceText.FindFirst("\n", lineStart);
		if (lineEnd < 0)
			lineEnd = fReferenceText.Length();

		BString line;
		fReferenceText.CopyInto(line, lineStart, lineEnd - lineStart);
		if (!line.Trim().IsEmpty())
			count++;

		lineStart = lineEnd + 1;
	}
	return count;
}


status_t
VerseListFile::AppendReference(const char* referenceLine)
{
	if (referenceLine == NULL || referenceLine[0] == '\0')
		return B_BAD_VALUE;
	if (fPath.IsEmpty())
		return B_NO_INIT;

	if (!fReferenceText.IsEmpty())
		fReferenceText << "\n";
	fReferenceText << referenceLine;
	return Save();
}


status_t
VerseListFile::RemoveLine(int32 lineIndex)
{
	if (lineIndex < 0 || fPath.IsEmpty())
		return B_BAD_INDEX;

	// Split on "\n" -- the exact same rule _Rebuild() applies to this
	// same text -- rather than anything smarter, so the Nth entry here
	// is always the Nth section there.
	BStringList lines;
	if (!fReferenceText.Split("\n", true, lines)
		|| lineIndex >= lines.CountStrings()) {
		return B_BAD_INDEX;
	}

	lines.Remove(lineIndex);

	BString rebuilt;
	for (int32 i = 0; i < lines.CountStrings(); i++) {
		if (i > 0)
			rebuilt << "\n";
		rebuilt << lines.StringAt(i);
	}
	fReferenceText = rebuilt;

	return Save();
}


status_t
VerseListFile::Save()
{
	if (fPath.IsEmpty())
		return B_NO_INIT;

	EnsureMimeTypeRegistered();

	BFile file(fPath.String(),
		B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	// The header, then the references -- the canonical form _ParseBody()
	// reads back exactly. Name is always written, even when it matches
	// the filename, so the file remains self-describing on its own once
	// separated from that filename (renamed, pasted elsewhere, quoted in
	// a forum post). Description only when there is one, so a plain list
	// stays a plain list instead of growing a blank line. Versification
	// always written, defaulting to KJV when unset -- exactly the
	// convention BibleTextDocument::_PrepareKey() already uses for a
	// module that declares none, so an unlabeled list means the same
	// thing here that it would mean anywhere else in this program.
	BString body;
	body << "# Name: " << fName << "\n";
	if (!fDescription.IsEmpty())
		body << "# Description: " << fDescription << "\n";
	body << "# Versification: "
		<< (fVersification.IsEmpty() ? "KJV" : fVersification) << "\n";
	if (!fReferenceText.IsEmpty())
		body << fReferenceText;

	file.Write(body.String(), body.Length());

	// Written after the content, and only as a cache for Tracker's
	// benefit -- SetTo() never trusts these over what it just parsed
	// from the body above.
	file.WriteAttrString(kAttrName, &fName);
	file.WriteAttrString(kAttrDescription, &fDescription);
	file.WriteAttrString(kAttrVersification, &fVersification);
	int32 count = EntryCount();
	file.WriteAttr(kAttrCount, B_INT32_TYPE, 0, &count, sizeof(count));

	BNodeInfo info(&file);
	info.SetType(kMimeType);

	return B_OK;
}
