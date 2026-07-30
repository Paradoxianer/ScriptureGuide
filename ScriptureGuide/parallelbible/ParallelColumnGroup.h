/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef PARALLEL_COLUMN_GROUP_H
#define PARALLEL_COLUMN_GROUP_H

#include <vector>

#include <String.h>
#include <View.h>

class BScrollView;
class BStringView;
class BibleTextDocument;
class NoteFieldView;
class ParallelBibleView;
class PersonalNotesModule;
class TextDocumentView;


// A "mini ParallelBibleView" for one detached group (see ParallelBibleView's
// class comment on SetColumnsLinked(), issue #12): a contiguous run of
// Bible/Commentary columns, plus optionally a notes column of its own,
// sharing VerseAligner alignment and one scroll position -- independent of
// the group-0 columns ParallelBibleView still manages directly, and of
// every other group. Mirrors ParallelBibleView's own layout logic at a
// smaller scale (VerseAligner::Align(), manual left-to-right MoveTo()/
// ResizeTo(), GetHeightForWidth()-driven natural sizing, the same
// _RowHeight() technique for its notes column's field heights), but with
// no header row, no add/remove buttons, no notes-width cap negotiation --
// all of that stays the outer ParallelBibleView's job.
//
// Does NOT own the TextDocumentView/BibleTextDocument objects its Bible
// members wrap (see AddBibleMember()) -- those remain owned by
// ParallelBibleView's own fDocuments/fTextViews, exactly as for a group-0
// column; this object only keeps a second, non-owning reference to lay
// them out and align them together, and is itself what actually
// AddChild()s the TextDocumentView (as this class's own child, not
// ParallelBibleView's -- a BView only ever has one parent).
//
// Not added as a plain child anywhere -- always reached through its own
// fScrollView (see ScrollView()), the same relationship ParallelBibleView
// itself has to the single outer BScrollView that wraps it
// (LogosMainWindow.cpp), just one level deeper. TextDocumentView already
// knows how to drive its own scrollbars when it's a direct BScrollView
// target (see TextDocumentView.h); nothing extra is needed to make that
// work here too.
class ParallelColumnGroup : public BView {
public:
								// owner/group are handed down to this
								// group's own notes fields (see
								// NoteFieldView) so focusing one makes
								// this group active, same as clicking a
								// Bible column does.
								ParallelColumnGroup(const char* name,
									PersonalNotesModule* notesModule,
									ParallelBibleView* owner, int32 group);
	virtual						~ParallelColumnGroup();

			// Order = left to right. view becomes this object's own child
			// (AddChild()); document is not owned (see the class
			// comment).
			void				AddBibleMember(TextDocumentView* view,
									BibleTextDocument* document);

			// Whether this group has its own notes column -- building/
			// tearing down its fields (keyed off this group's own
			// fCurrentKey, via the shared notesModule passed to the
			// constructor) happens internally, the same way
			// ParallelBibleView manages its own group-0 notes column.
			void				SetHasNotes(bool hasNotes);
			bool				HasNotes() const
									{ return fHasNotes; }

			// VerseAligner (if 2+ Bible members) + positioning + this
			// group's own natural size, all freshly recomputed -- pure
			// geometry, doesn't touch any document's content.
			// bibleColumnWidth/notesColumnWidth are the SAME per-column
			// widths every group-0 column also gets
			// (ParallelBibleView::_ColumnWidth()/_NotesColumnWidth()) --
			// queried fresh and passed in on every call rather than
			// cached from AddBibleMember()/SetHasNotes() time, since
			// they can change (a window resize) without this group's
			// membership changing. Called by ParallelBibleView::
			// _Realign() for every open group whenever something
			// view-wide changes (font, verse-number visibility, a
			// resize), and internally by SetKey() below.
			void				Relayout(float bibleColumnWidth,
									float notesColumnWidth);

			// Changes which chapter this group's own Bible/notes members
			// show -- entirely self-contained, touches nothing outside
			// this group. Mirrors ParallelBibleView::SetKey(), scoped to
			// just this group's own members.
			status_t			SetKey(const char* key);
			void				HighlightVerse(int startVerse,
									int endVerse);
			// Seeds a newly inserted Bible member so it starts on the
			// same chapter as everything else already in this group (see
			// ParallelBibleView::InsertColumn()) -- empty until the
			// first SetKey() call.
			const BString&		CurrentKey() const
									{ return fCurrentKey; }

			float				NaturalWidth() const
									{ return fNaturalWidth; }
			float				NaturalHeight() const
									{ return fNaturalHeight; }
			BScrollView*		ScrollView() const
									{ return fScrollView; }

private:
			float				_RowHeight(int verse) const;
			void				_PositionMembers();
			void				_RebuildNoteFields();

private:
			PersonalNotesModule*	fNotesModule;	// not owned
			BScrollView*			fScrollView;	// not owned (child)
			BString					fCurrentKey;
			ParallelBibleView*		fOwner;			// not owned
			int32					fGroup;

			// Not owned -- see the class comment.
			std::vector<TextDocumentView*>	fBibleViews;
			std::vector<BibleTextDocument*>	fBibleDocuments;
			// Refreshed on every Relayout() call -- see there for why
			// these aren't just set once from AddBibleMember()/
			// SetHasNotes(). Every Bible member shares the same width, so
			// one float covers all of them.
			float				fBibleColumnWidth;

			bool				fHasNotes;
			float				fNotesColumnWidth;
			std::vector<BStringView*>	fNoteVerseLabels;
			std::vector<NoteFieldView*>	fNoteFields;

			float				fNaturalWidth;
			float				fNaturalHeight;

	static	const float			kColumnSpacing;
	static	const float			kNoteVerseLabelWidth;
	static	const float			kBibleColumnInset;
};

#endif // PARALLEL_COLUMN_GROUP_H
