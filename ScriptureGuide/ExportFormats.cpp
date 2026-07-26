/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ExportFormats.h"

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ExportFormats"


// A verse's stored note, or a translation's rendered text, can contain a
// newline of its own (a note the user typed across multiple lines; a
// verse GBFPlain left a stray line break in) -- collapsing that to a
// space keeps one table row one physical line, which every one of these
// formats except plain text depends on to stay parseable/well-formed.
static BString
CollapseNewlines(const BString& text)
{
	BString result(text);
	result.ReplaceAll("\r\n", " ");
	result.ReplaceAll("\n", " ");
	result.ReplaceAll("\r", " ");
	return result;
}


static BString
EscapeMarkdownCell(const BString& text)
{
	BString result = CollapseNewlines(text);
	result.ReplaceAll("|", "\\|");
	return result;
}


static BString
EscapeHtml(const BString& text)
{
	BString result(text);
	result.ReplaceAll("&", "&amp;");
	result.ReplaceAll("<", "&lt;");
	result.ReplaceAll(">", "&gt;");
	return CollapseNewlines(result);
}


static BString
FormatPlainText(const std::vector<BString>& columnNames, bool hasNotes,
	const std::vector<ParallelBibleView::ExportRow>& rows)
{
	BString out;
	for (size_t r = 0; r < rows.size(); r++) {
		const ParallelBibleView::ExportRow& row = rows[r];
		out << B_TRANSLATE("Verse") << " " << row.verse << "\n";

		for (size_t c = 0; c < row.columnText.size() && c < columnNames.size();
				c++) {
			out << "  " << columnNames[c] << ": " << row.columnText[c]
				<< "\n";
		}
		if (hasNotes) {
			out << "  " << B_TRANSLATE("Notes") << ": " << row.notesText
				<< "\n";
		}
		out << "\n";
	}
	return out;
}


static BString
FormatTabSeparated(const std::vector<BString>& columnNames, bool hasNotes,
	const std::vector<ParallelBibleView::ExportRow>& rows)
{
	BString out;
	out << B_TRANSLATE("Verse");
	for (size_t c = 0; c < columnNames.size(); c++)
		out << "\t" << columnNames[c];
	if (hasNotes)
		out << "\t" << B_TRANSLATE("Notes");
	out << "\n";

	for (size_t r = 0; r < rows.size(); r++) {
		const ParallelBibleView::ExportRow& row = rows[r];
		out << row.verse;
		for (size_t c = 0; c < row.columnText.size(); c++)
			out << "\t" << CollapseNewlines(row.columnText[c]);
		if (hasNotes)
			out << "\t" << CollapseNewlines(row.notesText);
		out << "\n";
	}
	return out;
}


static BString
FormatMarkdownTable(const std::vector<BString>& columnNames, bool hasNotes,
	const std::vector<ParallelBibleView::ExportRow>& rows)
{
	BString out;
	out << "| " << B_TRANSLATE("Verse") << " |";
	for (size_t c = 0; c < columnNames.size(); c++)
		out << " " << columnNames[c] << " |";
	if (hasNotes)
		out << " " << B_TRANSLATE("Notes") << " |";
	out << "\n|";
	for (size_t c = 0; c < columnNames.size() + 1 + (hasNotes ? 1 : 0); c++)
		out << " --- |";
	out << "\n";

	for (size_t r = 0; r < rows.size(); r++) {
		const ParallelBibleView::ExportRow& row = rows[r];
		out << "| " << row.verse << " |";
		for (size_t c = 0; c < row.columnText.size(); c++)
			out << " " << EscapeMarkdownCell(row.columnText[c]) << " |";
		if (hasNotes)
			out << " " << EscapeMarkdownCell(row.notesText) << " |";
		out << "\n";
	}
	return out;
}


static BString
FormatHtmlTable(const std::vector<BString>& columnNames, bool hasNotes,
	const std::vector<ParallelBibleView::ExportRow>& rows)
{
	BString out;
	out << "<table>\n<tr><th>" << B_TRANSLATE("Verse") << "</th>";
	for (size_t c = 0; c < columnNames.size(); c++)
		out << "<th>" << EscapeHtml(columnNames[c]) << "</th>";
	if (hasNotes)
		out << "<th>" << B_TRANSLATE("Notes") << "</th>";
	out << "</tr>\n";

	for (size_t r = 0; r < rows.size(); r++) {
		const ParallelBibleView::ExportRow& row = rows[r];
		out << "<tr><td>" << row.verse << "</td>";
		for (size_t c = 0; c < row.columnText.size(); c++)
			out << "<td>" << EscapeHtml(row.columnText[c]) << "</td>";
		if (hasNotes)
			out << "<td>" << EscapeHtml(row.notesText) << "</td>";
		out << "</tr>\n";
	}
	out << "</table>\n";
	return out;
}


BString
FormatExport(ExportFormat format, const std::vector<BString>& columnNames,
	bool hasNotes, const std::vector<ParallelBibleView::ExportRow>& rows)
{
	switch (format) {
		case EXPORT_TAB_SEPARATED:
			return FormatTabSeparated(columnNames, hasNotes, rows);
		case EXPORT_MARKDOWN_TABLE:
			return FormatMarkdownTable(columnNames, hasNotes, rows);
		case EXPORT_HTML_TABLE:
			return FormatHtmlTable(columnNames, hasNotes, rows);
		case EXPORT_PLAIN_TEXT:
		default:
			return FormatPlainText(columnNames, hasNotes, rows);
	}
}
