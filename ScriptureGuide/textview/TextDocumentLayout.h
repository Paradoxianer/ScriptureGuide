/*
 * Copyright 2013-2015, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef TEXT_DOCUMENT_LAYOUT_H
#define TEXT_DOCUMENT_LAYOUT_H


#include <Referenceable.h>

#include "List.h"
#include "TextDocument.h"
#include "ParagraphLayout.h"


class BView;


class ParagraphLayoutInfo {
public:
	ParagraphLayoutInfo()
		:
		y(0.0f),
		layout()
	{
	}

	ParagraphLayoutInfo(float y, const ParagraphLayoutRef& layout)
		:
		y(y),
		layout(layout)
	{
	}

	ParagraphLayoutInfo(const ParagraphLayoutInfo& other)
		:
		y(other.y),
		layout(other.layout)
	{
	}


	ParagraphLayoutInfo& operator=(const ParagraphLayoutInfo& other)
	{
		y = other.y;
		layout = other.layout;
		return *this;
	}

	bool operator==(const ParagraphLayoutInfo& other) const
	{
		return y == other.y
			&& layout == other.layout;
	}

	bool operator!=(const ParagraphLayoutInfo& other) const
	{
		return !(*this == other);
	}

public:
	float				y;
	ParagraphLayoutRef	layout;
};


class TextDocumentLayout : public BReferenceable {
public:
								TextDocumentLayout();
								TextDocumentLayout(
									const TextDocumentRef& document);
								TextDocumentLayout(
									const TextDocumentLayout& other);
	virtual						~TextDocumentLayout();

			void				SetTextDocument(
									const TextDocumentRef& document);

			void				Invalidate();
			void				InvalidateParagraphs(int32 start, int32 count);

			void				SetWidth(float width);
			float				Width() const
									{ return fWidth; }

			float				Height();
			void				Draw(BView* view, const BPoint& offset,
									const BRect& updateRect);

			int32				LineIndexForOffset(int32 textOffset);
			int32				FirstOffsetOnLine(int32 lineIndex);
			int32				LastOffsetOnLine(int32 lineIndex);
			int32				CountLines();

			void				GetLineBounds(int32 lineIndex,
									float& x1, float& y1,
									float& x2, float& y2);

			void				GetTextBounds(int32 textOffset,
									float& x1, float& y1,
									float& x2, float& y2);

			// Same idea as GetTextBounds(), but by PARAGRAPH index
			// instead of a flat character offset -- an offset exactly at
			// a paragraph boundary is inherently ambiguous (the same
			// numeric position is simultaneously "end of paragraph N"
			// and "start of paragraph N+1"; see
			// _ParagraphLayoutIndexForOffset()'s own comment on which
			// one it resolves to and why). A caller that already knows
			// which paragraph it wants (e.g. NotesColumnView's gutter,
			// via BibleTextDocument::ParagraphIndexForVerse()) has no
			// reason to go through that ambiguity at all.
			void				GetParagraphBounds(int32 paragraphIndex,
									float& y1, float& y2);

			int32				TextOffsetAt(float x, float y,
									bool& rightOfCenter);

private:
			void				_Init();
			void				_ValidateLayout();
			void				_Layout();

			void				_DrawLayout(BView* view,
									const ParagraphLayoutInfo& layout) const;

			int32				_ParagraphLayoutIndexForOffset(
									int32& textOffset);
			int32				_ParagraphLayoutIndexForLineIndex(
									int32& lineIndex,
									int32& paragraphOffset);

private:
			float				fWidth;
			bool				fLayoutValid;

			TextDocumentRef		fDocument;
			TextListenerRef		fTextListener;
			std::vector<ParagraphLayoutInfo>
								fParagraphLayouts;
};


typedef BReference<TextDocumentLayout> TextDocumentLayoutRef;

#endif // TEXT_DOCUMENT_LAYOUT_H
