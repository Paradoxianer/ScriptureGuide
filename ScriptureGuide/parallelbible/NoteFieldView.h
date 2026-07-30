/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef NOTE_FIELD_VIEW_H
#define NOTE_FIELD_VIEW_H

#include <String.h>
#include <TextView.h>

class ParallelBibleView;
class PersonalNotesModule;


// A single verse's note editor -- see the class comment on ParallelBibleView
// for why a notes column is a stack of these instead of one flowing
// document. Auto-saves on every edit (rather than e.g. only on focus loss)
// so nothing is lost if the window closes with a field still focused;
// writing a short note to the local, file-backed RawCom module on every
// keystroke is cheap.
class NoteFieldView : public BTextView {
public:
								// owner/group let MakeFocus() make this
								// field's own group the active one (see
								// ParallelBibleView::SetActiveGroup(),
								// issue #12) -- focusing a note field is
								// exactly as much an "I'm working on this
								// group now" signal as clicking into its
								// Bible column is. owner may be NULL (a
								// group-0 field doesn't need to change
								// anything -- it's already the default).
								NoteFieldView(int verse,
									PersonalNotesModule* notes,
									const BString& chapterKey,
									ParallelBibleView* owner = NULL,
									int32 group = 0);

	virtual	void				MakeFocus(bool focus = true);

protected:
	virtual	void				InsertText(const char* text, int32 length,
									int32 offset, const text_run_array* runs);
	virtual	void				DeleteText(int32 fromOffset, int32 toOffset);

private:
			void				_Save();

private:
			int					fVerse;
			PersonalNotesModule*	fNotes;
			BString				fChapterKey;
			ParallelBibleView*	fOwner;
			int32				fGroup;
};

#endif // NOTE_FIELD_VIEW_H
