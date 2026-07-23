/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleWindow.h"

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <String.h>

#include "ParallelBibleView.h"
#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ParallelBibleWindow"


ParallelBibleWindow::ParallelBibleWindow(BRect frame,
	SwordBackend* modManager, const char* moduleName, const char* key)
	:
	BWindow(frame, B_TRANSLATE("Parallel View"), B_DOCUMENT_WINDOW, 0),
	fModManager(modManager),
	fParallelView(NULL),
	fScrollView(NULL),
	fMenuBar(NULL),
	fAddColumnMenu(NULL),
	fNotesItem(NULL)
{
	_BuildGUI(moduleName, key);
}


ParallelBibleWindow::~ParallelBibleWindow()
{
}


void
ParallelBibleWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case NEXT_CHAPTER:
			fParallelView->NextChapter();
			break;

		case PREV_CHAPTER:
			fParallelView->PrevChapter();
			break;

		case PARALLEL_ADD_COLUMN:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				SGModule* module = fModManager->BibleAt(index);
				if (module != NULL)
					fParallelView->AddColumn(module->Name());
			}
			break;
		}

		case PARALLEL_TOGGLE_NOTES:
		{
			bool enabled = !fParallelView->NotesEnabled();
			fParallelView->SetNotesEnabled(enabled);
			fNotesItem->SetMarked(enabled);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ParallelBibleWindow::_BuildGUI(const char* moduleName, const char* key)
{
	fMenuBar = new BMenuBar("parallel_menubar");

	BMenu* menu = new BMenu(B_TRANSLATE("Navigation"));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Next Chapter"),
		new BMessage(NEXT_CHAPTER), B_RIGHT_ARROW));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Previous Chapter"),
		new BMessage(PREV_CHAPTER), B_LEFT_ARROW));
	fMenuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Columns"));
	fAddColumnMenu = _BuildAddColumnMenu();
	menu->AddItem(fAddColumnMenu);
	menu->AddSeparatorItem();
	fNotesItem = new BMenuItem(B_TRANSLATE("Notes Column"),
		new BMessage(PARALLEL_TOGGLE_NOTES));
	menu->AddItem(fNotesItem);
	fMenuBar->AddItem(menu);

	fParallelView = new ParallelBibleView("parallelView",
		fModManager->Manager(), Frame().Width());

	fScrollView = new BScrollView("parallel_scroll", fParallelView,
		0, false, true, B_NO_BORDER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar, B_USE_DEFAULT_SPACING)
		.Add(fScrollView)
		.End();

	fParallelView->AddColumn(moduleName);
	fParallelView->SetKey(key);
}


BMenu*
ParallelBibleWindow::_BuildAddColumnMenu()
{
	BMenu* menu = new BMenu(B_TRANSLATE("Add Column"));

	int32 count = fModManager->CountBibles();
	for (int32 i = 0; i < count; i++) {
		BString name = fModManager->BibleAt(i)->FullName();
		BMessage* message = new BMessage(PARALLEL_ADD_COLUMN);
		message->AddInt32("index", i);
		menu->AddItem(new BMenuItem(name.String(), message));
	}

	return menu;
}
