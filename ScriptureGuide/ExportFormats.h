/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef EXPORT_FORMATS_H
#define EXPORT_FORMATS_H

#include <String.h>
#include <SupportDefs.h>

#include "parallelbible/ParallelBibleView.h"

// Plain-text/structured export of the current reading pane (#8) -- one row
// per verse, verse-aligned across every open Bible/Commentary column (and
// the notes column, if any), the same data ParallelBibleView::
// BuildExportRows() already assembles. Each of these turns that into one
// paste-able BString; the caller decides where it goes (clipboard, for now
// -- see SGMainWindow's "Copy Comparison" submenu).
enum ExportFormat {
	EXPORT_PLAIN_TEXT,
	EXPORT_TAB_SEPARATED,
	EXPORT_MARKDOWN_TABLE,
	EXPORT_HTML_TABLE
};

BString FormatExport(ExportFormat format,
	const std::vector<BString>& columnNames, bool hasNotes,
	const std::vector<ParallelBibleView::ExportRow>& rows);

#endif // EXPORT_FORMATS_H
