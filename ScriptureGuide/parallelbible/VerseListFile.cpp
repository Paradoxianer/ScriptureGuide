/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#include "VerseListFile.h"

#include <ctype.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Path.h>

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
	path.Append("lists");
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


status_t
VerseListFile::SetTo(const char* path)
{
	BFile file(path, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	fPath = path;

	if (file.ReadAttrString(kAttrName, &fName) != B_OK)
		fName = "";
	if (file.ReadAttrString(kAttrDescription, &fDescription) != B_OK)
		fDescription = "";
	if (file.ReadAttrString(kAttrVersification, &fVersification) != B_OK)
		fVersification = "";

	off_t size = 0;
	file.GetSize(&size);
	fReferenceText = "";
	if (size > 0) {
		char* buffer = new(std::nothrow) char[size + 1];
		if (buffer != NULL) {
			ssize_t bytesRead = file.Read(buffer, size);
			if (bytesRead > 0) {
				buffer[bytesRead] = '\0';
				fReferenceText = buffer;
			}
			delete[] buffer;
		}
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

	file.Write(fReferenceText.String(), fReferenceText.Length());

	file.WriteAttrString(kAttrName, &fName);
	file.WriteAttrString(kAttrDescription, &fDescription);
	file.WriteAttrString(kAttrVersification, &fVersification);
	int32 count = EntryCount();
	file.WriteAttr(kAttrCount, B_INT32_TYPE, 0, &count, sizeof(count));

	BNodeInfo info(&file);
	info.SetType(kMimeType);

	return B_OK;
}
