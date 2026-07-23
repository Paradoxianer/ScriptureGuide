/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef PARALLEL_BIBLE_WINDOW_H
#define PARALLEL_BIBLE_WINDOW_H

#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <Window.h>

#include "SwordBackend.h"

class ParallelBibleView;


// Standalone window for the side-by-side translation comparison view
// (ParallelBibleView). Kept separate from SGMainWindow rather than
// replacing its single-text rendering, so the existing, working window
// is untouched; opened via "Parallel View..." in SGMainWindow's Options
// menu.
class ParallelBibleWindow : public BWindow {
public:
								ParallelBibleWindow(BRect frame,
									SwordBackend* modManager,
									const char* moduleName,
									const char* key);
	virtual						~ParallelBibleWindow();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_BuildGUI(const char* moduleName,
									const char* key);
			BMenu*				_BuildAddColumnMenu();

private:
			SwordBackend*		fModManager;	// not owned

			ParallelBibleView*	fParallelView;
			BScrollView*		fScrollView;

			BMenuBar*			fMenuBar;
			BMenu*				fAddColumnMenu;
			BMenuItem*			fNotesItem;
};

#endif // PARALLEL_BIBLE_WINDOW_H
