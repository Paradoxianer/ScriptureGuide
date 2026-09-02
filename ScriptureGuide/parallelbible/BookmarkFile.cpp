/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#include "BookmarkFile.h"

#include <algorithm>
#include <stdio.h>

#include <AppFileInfo.h>
#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>

#include <versekey.h>

#include "../constants.h"
#include "../SwordBackend.h"

const char* BookmarkFile::kMimeType
	= "text/x-scriptureguide-bookmark";
const char* BookmarkFile::kDescriptionFileName = "Description.txt";

// Namespaced the same way VerseListFile's own attributes are.
static const char* kAttrPosition = "SG:position";
static const char* kAttrCode = "SG:code";
static const char* kAttrTags = "SG:tags";
static const char* kAttrReference = "SG:reference";
static const char* kAttrVersification = "SG:versification";
static const char* kAttrLocale = "SG:locale";
// #44 -- all optional; absent on an ordinary (whole-verse) bookmark.
static const char* kAttrSpanModule = "SG:span:module";
static const char* kAttrSpanStart = "SG:span:start";
static const char* kAttrSpanEnd = "SG:span:end";
static const char* kAttrSpanText = "SG:span:text";
static const char* kAttrColorValue = "SG:colorvalue";

static const char* kMirrorNote
	= "# Mirror of this file's own attributes -- not authoritative; see"
		" SG:reference/SG:versification/SG:locale.";
static const char* kHeaderVersification = "# Versification: ";
static const char* kHeaderLocale = "# Locale: ";


// Shared by SetTo()'s content-fallback path -- pulls reference,
// versification and locale back out of the plain-text mirror the same
// way the original content-first design always did.
static void
ParseMirrorContent(const BString& body, BString& reference,
	BString& versification, BString& locale)
{
	reference = "";
	versification = "";
	locale = "";

	int32 lineStart = 0;
	while (lineStart <= body.Length()) {
		int32 lineEnd = body.FindFirst("\n", lineStart);
		if (lineEnd < 0)
			lineEnd = body.Length();

		BString line;
		body.CopyInto(line, lineStart, lineEnd - lineStart);

		if (line.StartsWith(kHeaderVersification))
			versification = line.String() + strlen(kHeaderVersification);
		else if (line.StartsWith(kHeaderLocale))
			locale = line.String() + strlen(kHeaderLocale);
		else {
			BString trimmed(line);
			trimmed.Trim();
			if (!trimmed.IsEmpty() && trimmed.ByteAt(0) != '#')
				reference = trimmed;
		}

		if (lineEnd >= body.Length())
			break;
		lineStart = lineEnd + 1;
	}
}


BookmarkFile::BookmarkFile()
	:
	fPosition(-1),
	fSpanStart(0),
	fSpanEnd(0),
	fHasColor(false)
{
	fColor.red = 0;
	fColor.green = 0;
	fColor.blue = 0;
	fColor.alpha = 255;
}


BString
BookmarkFile::RootDirectory()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return BString();
	path.Append("scriptureguide");
	path.Append("verselists");
	create_directory(path.Path(), 0755);
	return BString(path.Path());
}


// Attributes first (see the class comment) -- only reads the plain-text
// mirror at all when SG:reference is missing or empty, i.e. this file's
// attributes didn't survive getting here (a copy off BFS). When that
// fallback fires, the recovered values are written back as attributes
// immediately, so the file is healed and the next SetTo() won't need
// the fallback again -- best-effort: on a read-only volume the write
// just silently doesn't stick, and the fallback simply runs again next
// time, which is still correct, only slower.
status_t
BookmarkFile::SetTo(const char* path)
{
	BFile file(path, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	fPath = path;

	BString reference, versification, locale;
	bool haveReference = file.ReadAttrString(kAttrReference, &reference) == B_OK
		&& !reference.IsEmpty();
	file.ReadAttrString(kAttrVersification, &versification);
	file.ReadAttrString(kAttrLocale, &locale);

	if (!haveReference) {
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
		ParseMirrorContent(body, reference, versification, locale);

		if (!reference.IsEmpty()) {
			BFile writable(path, B_READ_WRITE);
			if (writable.InitCheck() == B_OK) {
				writable.WriteAttrString(kAttrReference, &reference);
				BString ver = versification.IsEmpty()
					? BString("KJV") : versification;
				writable.WriteAttrString(kAttrVersification, &ver);
				if (!locale.IsEmpty())
					writable.WriteAttrString(kAttrLocale, &locale);
			}
		}
	}

	fReference = reference;
	fVersification = versification;
	fLocale = locale;

	if (fReference.IsEmpty())
		return B_BAD_DATA;

	fPosition = -1;
	int32 position;
	if (file.ReadAttr(kAttrPosition, B_INT32_TYPE, 0, &position,
			sizeof(position)) == (ssize_t)sizeof(position)) {
		fPosition = position;
	}

	BString tags;
	if (file.ReadAttrString(kAttrTags, &tags) == B_OK)
		fTags = tags;

	// #44: optional, and read as a group -- a bookmark either carries a
	// full span (module plus both offsets) or none at all, so a partial
	// set is treated as none rather than as a half-anchored highlight.
	fSpanModule = "";
	fSpanStart = 0;
	fSpanEnd = 0;
	fSpanText = "";
	BString spanModule;
	int32 spanStart = 0;
	int32 spanEnd = 0;
	if (file.ReadAttrString(kAttrSpanModule, &spanModule) == B_OK
		&& !spanModule.IsEmpty()
		&& file.ReadAttr(kAttrSpanStart, B_INT32_TYPE, 0, &spanStart,
			sizeof(spanStart)) == (ssize_t)sizeof(spanStart)
		&& file.ReadAttr(kAttrSpanEnd, B_INT32_TYPE, 0, &spanEnd,
			sizeof(spanEnd)) == (ssize_t)sizeof(spanEnd)
		&& spanEnd > spanStart) {
		fSpanModule = spanModule;
		fSpanStart = spanStart;
		fSpanEnd = spanEnd;
		file.ReadAttrString(kAttrSpanText, &fSpanText);
	}

	fHasColor = false;
	BString colorValue;
	if (file.ReadAttrString(kAttrColorValue, &colorValue) == B_OK)
		fHasColor = ParseHighlightColor(colorValue.String(), fColor);

	// #101: SG:reference is editable in Tracker now, so SG:code -- read
	// by Tracker's own "Bible Order" column, never by this app, which
	// always calls Code() fresh -- can go stale the moment someone
	// retypes a reference there instead of through this app's own Save().
	// Same self-healing shape as the content-mirror fallback above:
	// recompute from what was JUST read (fReference's own Versification/
	// Locale, unaffected by a reference-only edit) and write back only
	// when it actually changed, so a normal read doesn't touch the file
	// every time. Best-effort -- a read-only volume just leaves it stale
	// until the next Save() from inside the app, same as that fallback.
	BString freshCode = Code();
	BString storedCode;
	if (file.ReadAttrString(kAttrCode, &storedCode) != B_OK
		|| storedCode != freshCode) {
		BFile writable(path, B_READ_WRITE);
		if (writable.InitCheck() == B_OK)
			writable.WriteAttrString(kAttrCode, &freshCode);
	}

	return B_OK;
}


static BString
_SanitizeBookmarkName(const char* name)
{
	BString sanitized(name);
	sanitized.Trim();
	if (sanitized.IsEmpty())
		sanitized = "Reference";

	// BFS forbids '/' outright and is otherwise permissive -- same
	// reasoning and fallback VerseListFile::_SanitizeFileName() already
	// has for its own file/collection names.
	for (int32 i = 0; i < sanitized.Length(); i++) {
		if (sanitized.ByteAt(i) == '/')
			sanitized.SetByteAt(i, '-');
	}
	if (sanitized.Trim().IsEmpty())
		sanitized = "Reference";

	return sanitized;
}


status_t
BookmarkFile::CreateNew(const char* collectionPath, const char* referenceLine,
	const char* versification, const char* locale, int32 position)
{
	EnsureMimeTypeRegistered();

	if (collectionPath == NULL || collectionPath[0] == '\0'
		|| referenceLine == NULL || referenceLine[0] == '\0') {
		return B_BAD_VALUE;
	}
	create_directory(collectionPath, 0755);

	BString baseName = _SanitizeBookmarkName(referenceLine);
	BString fileName(baseName);
	fileName << ".sgvb";
	BPath path(collectionPath);
	path.Append(fileName.String());

	// A second bookmark with the same reference text gets a number
	// rather than silently overwriting the first -- same convention
	// VerseListFile::CreateNew() already uses (dragging the same verse
	// in twice is a real, unremarkable thing to do -- a reading plan
	// revisits a passage on purpose).
	int suffix = 2;
	while (BEntry(path.Path()).Exists()) {
		fileName = baseName;
		fileName << " " << suffix << ".sgvb";
		path.SetTo(collectionPath);
		path.Append(fileName.String());
		suffix++;
	}

	fPath = path.Path();
	fReference = referenceLine;
	fVersification = versification != NULL ? versification : "";
	fLocale = locale != NULL ? locale : "";
	fPosition = position;
	fTags = "";

	return Save();
}


void
BookmarkFile::SetReference(const char* referenceLine)
{
	fReference = referenceLine != NULL ? referenceLine : "";
}


void
BookmarkFile::SetPosition(int32 position)
{
	fPosition = position;
}


void
BookmarkFile::SetTags(const char* tags)
{
	fTags = tags != NULL ? tags : "";
}


// "#rrggbb" -- a plain, human-readable form so the attribute stays
// meaningful in Tracker and survives being copied off BFS in the file's
// own content mirror, rather than an opaque packed integer.
bool
ParseHighlightColor(const char* value, rgb_color& outColor)
{
	if (value == NULL)
		return false;
	BString text(value);
	text.Trim();
	if (text.Length() != 7 || text.ByteAt(0) != '#')
		return false;

	unsigned int red = 0;
	unsigned int green = 0;
	unsigned int blue = 0;
	if (sscanf(text.String() + 1, "%2x%2x%2x", &red, &green, &blue) != 3)
		return false;

	outColor.red = (uint8)red;
	outColor.green = (uint8)green;
	outColor.blue = (uint8)blue;
	outColor.alpha = 255;
	return true;
}


BString
FormatHighlightColor(rgb_color color)
{
	char buffer[8];
	snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.red, color.green,
		color.blue);
	return BString(buffer);
}


void
BookmarkFile::SetSpan(const char* module, int32 start, int32 end,
	const char* text)
{
	fSpanModule = module != NULL ? module : "";
	fSpanStart = start;
	fSpanEnd = end;
	fSpanText = text != NULL ? text : "";
}


void
BookmarkFile::SetColor(rgb_color color)
{
	fColor = color;
	fHasColor = true;
}


BString
BookmarkFile::NavigationKey() const
{
	BString base, suffix;
	SplitVerseRangeSuffix(fReference, base, suffix);

	BString key;
	if (!ConvertVerseReference(base.String(), fLocale.String(),
			fVersification.String(), "", fVersification.String(), key)) {
		return fReference;
	}
	key << suffix;
	return key;
}


// Testament(1 digit) + book-within-testament(2 digits) + chapter(3
// digits) + verse(3 digits), all zero-padded -- a plain lexicographic
// sort of this string equals Bible order, independent of Position (see
// the class comment). The testament digit is an addition to the 2+3+3
// digit widths #55 itself quotes: without it, Genesis (testament 1) and
// Matthew (testament 2), both "book 1" of their own testament, would
// collide on the same code -- confirmed by inspection of VerseKey's own
// getBook(), which numbers books WITHIN a testament, not across both.
static BString
ComputeBookmarkCode(const char* reference, const char* versification,
	const char* locale)
{
	sword::VerseKey key;
	// The bookmark's OWN recorded locale, not whatever the system's
	// current one happens to be -- Save() runs again on every reorder
	// (see SGVerseListWindow::_MoveRow()), long after the reference was
	// first written, so the current locale at THAT moment has no reason
	// to still match the one the reference's own book name is in.
	if (locale != NULL && locale[0] != '\0')
		key.setLocale(locale);
	key.setVersificationSystem(
		versification != NULL && versification[0] != '\0'
			? versification : "KJV");
	key.setText(reference);
	if (key.popError() != 0)
		return BString();

	char code[16];
	snprintf(code, sizeof(code), "%01d%02d%03d%03d",
		(int)key.getTestament(), (int)key.getBook(), key.getChapter(),
		key.getVerse());
	return BString(code);
}


BString
BookmarkFile::Code() const
{
	return ComputeBookmarkCode(fReference.String(), fVersification.String(),
		fLocale.String());
}


status_t
BookmarkFile::Save()
{
	if (fPath.IsEmpty() || fReference.IsEmpty())
		return B_NO_INIT;

	EnsureMimeTypeRegistered();

	BFile file(fPath.String(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	BString versification = fVersification.IsEmpty()
		? BString("KJV") : fVersification;

	// Attributes are the source of truth (see the class comment); the
	// content written below is only ever a readable mirror of these
	// three, regenerated in full on every save.
	file.WriteAttrString(kAttrReference, &fReference);
	file.WriteAttrString(kAttrVersification, &versification);
	if (!fLocale.IsEmpty())
		file.WriteAttrString(kAttrLocale, &fLocale);

	BString body;
	body << kMirrorNote << "\n";
	body << "# Versification: " << versification << "\n";
	// Omitted when empty (English/no locale -- VerseKey's own default
	// needs no explicit marker to round-trip correctly), same convention
	// VerseListFile's own optional "# Description:" line already uses.
	if (!fLocale.IsEmpty())
		body << "# Locale: " << fLocale << "\n";
	body << fReference;
	file.Write(body.String(), body.Length());

	file.WriteAttr(kAttrPosition, B_INT32_TYPE, 0, &fPosition,
		sizeof(fPosition));
	BString code = Code();
	file.WriteAttrString(kAttrCode, &code);
	file.WriteAttrString(kAttrTags, &fTags);

	// #44: written only when actually set, so an ordinary bookmark's
	// attribute list stays exactly as it was before highlighting existed.
	if (HasSpan()) {
		file.WriteAttrString(kAttrSpanModule, &fSpanModule);
		file.WriteAttr(kAttrSpanStart, B_INT32_TYPE, 0, &fSpanStart,
			sizeof(fSpanStart));
		file.WriteAttr(kAttrSpanEnd, B_INT32_TYPE, 0, &fSpanEnd,
			sizeof(fSpanEnd));
		file.WriteAttrString(kAttrSpanText, &fSpanText);
	}
	if (fHasColor) {
		BString colorValue = FormatHighlightColor(fColor);
		file.WriteAttrString(kAttrColorValue, &colorValue);
	}

	BNodeInfo info(&file);
	info.SetType(kMimeType);

	return B_OK;
}


status_t
BookmarkFile::Remove()
{
	if (fPath.IsEmpty())
		return B_NO_INIT;
	return BEntry(fPath.String()).Remove();
}


std::vector<BString>
BookmarkFile::ListBookmarkPaths(const char* collectionPath)
{
	std::vector<std::pair<int32, BString> > ordered;
	if (collectionPath == NULL || collectionPath[0] == '\0')
		return std::vector<BString>();

	BDirectory dir(collectionPath);
	if (dir.InitCheck() != B_OK)
		return std::vector<BString>();

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) == B_OK
			&& BString(name) == kDescriptionFileName) {
			continue;
		}

		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;

		// A cheap attribute read, not a full SetTo() -- sorting a
		// folder's worth of bookmarks shouldn't have to parse every
		// file's content just to order them. A file missing the
		// attribute entirely (dropped in from outside BFS, say) sorts
		// after every file that has one, via INT32_MAX, then by
		// filename -- see the sort comparator below.
		BNode node(&entry);
		int32 position = 0x7fffffff;
		node.ReadAttr(kAttrPosition, B_INT32_TYPE, 0, &position,
			sizeof(position));

		ordered.push_back(std::make_pair(position, BString(path.Path())));
	}

	std::sort(ordered.begin(), ordered.end());

	std::vector<BString> paths;
	paths.reserve(ordered.size());
	for (size_t i = 0; i < ordered.size(); i++)
		paths.push_back(ordered[i].second);
	return paths;
}


BString
BookmarkFile::HighlightsDirectory()
{
	// A dedicated folder under the verse-list root, deliberately kept
	// apart from ordinary reading lists (#44) -- one sub-folder per
	// colour, each an ordinary collection of ordinary bookmark files,
	// so Tracker browses them and Copy to... already moves one into a
	// real verse list without any new machinery.
	BString root = RootDirectory();
	if (root.IsEmpty())
		return BString();

	BPath path(root.String());
	path.Append("Highlights");
	create_directory(path.Path(), 0755);
	return BString(path.Path());
}


std::vector<BookmarkFile>
BookmarkFile::ListHighlights()
{
	std::vector<BookmarkFile> highlights;
	BString root = HighlightsDirectory();
	if (root.IsEmpty())
		return highlights;

	// One level of colour folders, then the bookmarks inside each --
	// same non-recursive shape ListCollectionNames()/ListBookmarkPaths()
	// already use, rather than walking an arbitrary tree.
	BDirectory rootDir(root.String());
	if (rootDir.InitCheck() != B_OK)
		return highlights;

	BEntry categoryEntry;
	while (rootDir.GetNextEntry(&categoryEntry) == B_OK) {
		if (!categoryEntry.IsDirectory())
			continue;
		BPath categoryPath;
		if (categoryEntry.GetPath(&categoryPath) != B_OK)
			continue;

		std::vector<BString> paths = ListBookmarkPaths(categoryPath.Path());
		for (size_t i = 0; i < paths.size(); i++) {
			BookmarkFile bookmark;
			if (bookmark.SetTo(paths[i].String()) == B_OK
				&& bookmark.HasSpan() && bookmark.HasColor()) {
				highlights.push_back(bookmark);
			}
		}
	}

	return highlights;
}


std::vector<BString>
BookmarkFile::ListCollectionNames(const char* parentPath)
{
	BString root = parentPath != NULL && parentPath[0] != '\0'
		? BString(parentPath) : RootDirectory();

	std::vector<BString> names;
	BDirectory dir(root.String());
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


BString
BookmarkFile::CreateCollection(const char* parentPath, const char* name)
{
	BString root = parentPath != NULL && parentPath[0] != '\0'
		? BString(parentPath) : RootDirectory();
	if (root.IsEmpty())
		return BString();

	BString baseName = _SanitizeBookmarkName(name);
	BPath path(root.String());
	path.Append(baseName.String());

	int suffix = 2;
	while (BEntry(path.Path()).Exists()) {
		BString numbered(baseName);
		numbered << " " << suffix;
		path.SetTo(root.String());
		path.Append(numbered.String());
		suffix++;
	}

	if (create_directory(path.Path(), 0755) != B_OK)
		return BString();
	return BString(path.Path());
}


void
BookmarkFile::EnsureMimeTypeRegistered()
{
	BMimeType mime(kMimeType);
	bool alreadyInstalled = mime.IsInstalled();
	if (!alreadyInstalled) {
		mime.Install();
		mime.SetShortDescription("ScriptureGuide bookmark");
		mime.SetLongDescription("A single Bible reference (ScriptureGuide)");
		mime.SetPreferredApp(SG_APP_SIGNATURE);

		BMessage extensions;
		extensions.AddString("extensions", "sgvb");
		mime.SetFileExtensions(&extensions);
	}

	// Icon and attribute info are refreshed on EVERY call, even when the
	// type was already installed -- unlike the block above, which only
	// needs to run once. Confirmed the hard way: an earlier build of
	// this app installed this type with incomplete attribute info
	// (missing "attr:width"/"attr:alignment" -- see the comment below),
	// and the IsInstalled() guard alone left that system stuck with the
	// gap permanently, since nothing short of manually clearing the MIME
	// database entry could ever re-run the block that fixes it. A future
	// icon/attribute fix should not require that.
	//
	// The tilted ribbon-bookmark icon (see ScriptureGuide.rdef's
	// "bookmark_icon" resource) -- read out of this app's own resources
	// at runtime, same technique LogosMainWindow.cpp's _LoadVectorIcon()
	// already uses for the toolbar's back/forward arrows, just handed
	// straight to SetIcon() as raw HVIF bytes instead of being decoded
	// into a BBitmap first (SetIcon(const uint8*, size_t) takes the
	// vector data directly -- no bitmap needed for a MIME type's icon).
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK) {
		BFile appFile(&info.ref, B_READ_ONLY);
		BResources resources(&appFile);
		if (resources.InitCheck() == B_OK) {
			size_t dataSize = 0;
			const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE,
				"bookmark_icon", &dataSize);
			if (data != NULL && dataSize > 0)
				mime.SetIcon((const uint8*)data, dataSize);
		}
	}

	// Gives Tracker real columns for these, matching VerseListFile's own
	// EnsureMimeTypeRegistered() pattern -- "attr:width"/"attr:alignment"
	// are undocumented in BMimeType::SetAttrInfo()'s own public API
	// (only name/public_name/type/viewable/editable are), but confirmed
	// against Tracker's own source (BContainerWindow::AddMimeMenu(),
	// src/kits/tracker/ContainerWindow.cpp) that it silently skips any
	// attribute missing either one when building the Attributes menu --
	// VerseListFile's own four attributes have the exact same gap, which
	// is why none of them show up there either.
	BMessage attrInfo;
	// #101: editable in Tracker now, unlike Versification/Locale/Code
	// below -- SetTo() recomputes and rewrites SG:code from whatever
	// SG:reference now says on every read, so a Tracker edit here can't
	// leave the Bible-order attribute stale. A reference typed badly
	// enough to not parse just leaves SG:code empty (sorts last in
	// Tracker's own "Bible Order" column, the same place a bookmark
	// with no position attribute at all already sorts to).
	attrInfo.AddString("attr:name", kAttrReference);
	attrInfo.AddString("attr:public_name", "Reference");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", true);
	attrInfo.AddInt32("attr:width", 140);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	attrInfo.AddString("attr:name", kAttrVersification);
	attrInfo.AddString("attr:public_name", "Versification");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);
	attrInfo.AddInt32("attr:width", 90);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	attrInfo.AddString("attr:name", kAttrLocale);
	attrInfo.AddString("attr:public_name", "Locale");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);
	attrInfo.AddInt32("attr:width", 60);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	// #101: editable too -- ListBookmarkPaths() already sorts by
	// whatever is here with zero validation (ties/gaps fall back to
	// filename order, same as a file missing the attribute entirely),
	// and any reorder driven from inside the app (drag/Move Up/Move
	// Down, SGVerseListWindow::_MoveRow()) already renumbers every
	// bookmark in the open collection to a clean 0..n-1 sequence on
	// every move -- so values left messy by a Tracker edit self-heal
	// the next time the user reorders anything from inside the app,
	// without this attribute needing its own separate healing logic.
	attrInfo.AddString("attr:name", kAttrPosition);
	attrInfo.AddString("attr:public_name", "Position");
	attrInfo.AddInt32("attr:type", B_INT32_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", true);
	attrInfo.AddInt32("attr:width", 50);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_RIGHT);

	attrInfo.AddString("attr:name", kAttrCode);
	attrInfo.AddString("attr:public_name", "Bible Order");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);
	attrInfo.AddInt32("attr:width", 80);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	// #44: visible in Tracker, but not editable -- unlike Reference and
	// Position (#101), a character offset is only meaningful against the
	// exact rendered text it was taken from, so hand-editing one has no
	// sensible outcome to aim for.
	attrInfo.AddString("attr:name", kAttrColorValue);
	attrInfo.AddString("attr:public_name", "Highlight Colour");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);
	attrInfo.AddInt32("attr:width", 80);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	attrInfo.AddString("attr:name", kAttrSpanText);
	attrInfo.AddString("attr:public_name", "Highlighted Text");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", false);
	attrInfo.AddInt32("attr:width", 200);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	attrInfo.AddString("attr:name", kAttrTags);
	attrInfo.AddString("attr:public_name", "Tags");
	attrInfo.AddInt32("attr:type", B_STRING_TYPE);
	attrInfo.AddBool("attr:viewable", true);
	attrInfo.AddBool("attr:editable", true);
	attrInfo.AddInt32("attr:width", 120);
	attrInfo.AddInt32("attr:alignment", B_ALIGN_LEFT);

	mime.SetAttrInfo(&attrInfo);
}
