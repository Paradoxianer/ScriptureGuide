/*
 * Copyright 2013-2015, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef TEXT_DOCUMENT_VIEW_H
#define TEXT_DOCUMENT_VIEW_H


#include <Invoker.h>
#include <String.h>
#include <View.h>

#include "TextDocument.h"
#include "TextDocumentLayout.h"
#include "TextEditor.h"


class BClipboard;
class BMessageRunner;


class TextDocumentView : public BView, public BInvoker {
public:
								TextDocumentView(const char* name = NULL);
	virtual						~TextDocumentView();

	// BView implementation
	virtual	void				MessageReceived(BMessage* message);

	virtual void				Draw(BRect updateRect);

	virtual	void				AttachedToWindow();
	virtual void				FrameResized(float width, float height);
	virtual	void				WindowActivated(bool active);
	virtual	void				MakeFocus(bool focus = true);

	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);

	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				KeyUp(const char* bytes, int32 numBytes);

	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);

	virtual void				Relayout();

	// TextDocumentView interface
			void				SetTextDocument(
									const TextDocumentRef& document);

			void				SetEditingEnabled(bool enabled);
			void				SetTextEditor(
									const TextEditorRef& editor);

			void				SetInsets(float inset);
			void				SetInsets(float horizontal, float vertical);
			void				SetInsets(float left, float top, float right,
									float bottom);

			void				SetSelectionEnabled(bool enabled);
			void				SetCaret(BPoint where, bool extendSelection);

			void				SelectAll();
			bool				HasSelection() const;
			void				GetSelection(int32& start, int32& end) const;
				// Programmatic counterpart to SetCaret()'s mouse-driven
				// range selection -- for callers that already know the
				// text offsets they want selected (e.g. highlighting a
				// verse jumped to from search) rather than a click point.
			void				SetSelection(int32 start, int32 end);

				// Text offset of the character under `where` (view
				// coordinates) -- exposes the same lookup MouseDown()/
				// MouseMoved() already use internally, for subclasses
				// that need their own hit-testing (e.g. to tell whether a
				// click landed inside the current selection, to decide
				// between starting a drag and placing the caret there).
				int32				TextOffsetAt(BPoint where);

				// View-coordinate bounds of the character at `offset` --
				// the inverse of TextOffsetAt(), for a subclass that
				// needs to draw its own per-line/per-paragraph overlay
				// (e.g. a verse-number gutter) aligned with where the
				// document actually rendered that offset.
				void				GetTextBounds(int32 offset,
										float& x1, float& y1, float& x2,
										float& y2);

				// Same idea, but by paragraph index -- see
				// TextDocumentLayout::GetParagraphBounds()'s own comment
				// on why a caller that already knows which paragraph it
				// wants should use this instead of GetTextBounds() at
				// that paragraph's own first offset (which is
				// ambiguous with the previous paragraph's own last one).
				void				GetParagraphBounds(int32 paragraphIndex,
										float& y1, float& y2);

			void				Copy(BClipboard* clipboard);
			void				Paste(BClipboard* clipboard);

				// Total height of the laid-out document plus this view's
				// own insets -- exactly the "data height" this view's
				// own vertical scrollbar range is derived from. Cheap:
				// the layout is cached, so this only re-measures when
				// something already invalidated it. Unlike
				// GetHeightForWidth(), it measures at the width the view
				// actually has rather than copying the whole layout to
				// measure at some other width.
				//
				// For callers that must size ONE scrollbar to cover
				// several views at once (see ParallelBibleView, where a
				// chain of columns shares the rightmost column's bar).
			float				ContentHeight();

protected:
				// This view's own editor (always set -- the constructor
				// installs a default one). Exposed so a subclass can
				// inspect the caret/selection and run its own edits
				// through it, which a subclass that has to enforce a
				// document-structure invariant needs before deciding
				// whether to let the base class's KeyDown()/Paste() run
				// at all (see NotesDisplayView, which keeps exactly one
				// paragraph per verse).
			const TextEditorRef& Editor() const
									{ return fTextEditor; }

private:
			float				_TextLayoutWidth(float viewWidth) const;

			void				_UpdateScrollBars();

			void				_ShowCaret(bool show);
			void				_BlinkCaret();
			void				_DrawCaret(int32 textOffset);
			void				_DrawSelection();
			void				_GetSelectionShape(BShape& shape,
									int32 start, int32 end);

			status_t			_PastePossiblyDisallowedChars(const char* str, int32 maxLength);
			void				_PasteAllowedChars(const char* str, int32 maxLength);
	static	bool				_IsAllowedChar(char c);
	static	bool				_AreCharsAllowed(const char* str, int32 maxLength);

private:
			TextDocumentRef		fTextDocument;
			TextDocumentLayout	fTextDocumentLayout;
			TextEditorRef		fTextEditor;

			float				fInsetLeft;
			float				fInsetTop;
			float				fInsetRight;
			float				fInsetBottom;

			BRect				fCaretBounds;
			BMessageRunner*		fCaretBlinker;
			int32				fCaretBlinkToken;
			bool				fSelectionEnabled;
			bool				fShowCaret;
};

#endif // TEXT_DOCUMENT_VIEW_H
