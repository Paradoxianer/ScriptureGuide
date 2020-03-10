/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_MENU_ITEM_H
#define BIBLE_MENU_ITEM_H

#include <MenuItem.h>
#include <SupportDefs.h>

#include <swmodule.h>

#inlcude "constants.h"

using namespace sword;


class BibleMenuItem : public BMenuItem{
public:
	BibleMenuItem(SWModule *module, BMessage* message,
          char shortcut = 0,
          uint32 modifiers = 0);
	
	TextType	GetType();


private:
	SWModule *fModule;
};


#endif // _H
