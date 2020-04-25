/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <MenuItem.h>
#include <swmodule.h>

#include <stdio.h>
#include <string.h>


#include "BibleTitleView.h"
#include "BibleTextColumn.h"

BibleTitleView::BibleTitleView():TitleView(BRect(0,0,0,0), /*outlineView =*/ NULL,
									/*visibleColumns =*/ NULL, /*sortColumns =*/ NULL,
									/*masterView =*/ NULL, 0)
{
}

BPopUpMenu* BibleTitleView::CreateContextMenu()
{
	if (fColumnPop == NULL)
			fColumnPop = new BPopUpMenu("Columns", false, false);

	fColumnPop->RemoveItems(0, fColumnPop->CountItems(), true);
	BMessenger me(this);
	for (int index = 0; index < fColumns->CountItems(); index++) {
		BibleTextColumn* column = 
			dynamic_cast<BibleTextColumn*>((BColumn*)fColumns->ItemAt(index));
		if (column == NULL)
			continue;
		BString name;
		column->GetColumnName(&name);
		BMessage* message = new BMessage(kToggleColumn);
		message->AddInt32("be:field_num", column->LogicalFieldNum());
		BMenuItem* item = new BMenuItem(name.String(), message);
		item->SetMarked(column->IsVisible());
		item->SetTarget(me);
		const char *textType = column->CurrentModule()->getType();
		BMenu *typeMenu = FindMenu(textType);
		if (typeMenu == NULL)
		{
			typeMenu = new BMenu(textType);
			fColumnPop->AddItem(typeMenu);			
		}
		typeMenu->AddItem(item);
	}
	return fColumnPop;
}


BMenu* BibleTitleView::FindMenu(const char *name)
{
	BMenu *menu		= NULL;
	BMenu *tmpMenu	= NULL;
	for (int32 i = 0; i < fColumnPop->CountItems(); i++) {
		tmpMenu = fColumnPop->ItemAt(i)->Submenu();
		if (tmpMenu != NULL && strcmp(tmpMenu->Name(), name) == 0) {
			menu = tmpMenu;
			break;
		}
	}
	return menu;
}
