/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleMenuItem.h"
BibleMenuItem::BibleMenuItem(SWModule *module, BMessage* message,
          char shortcut = 0, uint32 modifiers = 0)
		: BMenuItem("", message, shortcut, modiffers),
		fModule(module)
{
	if (fModule != NULL)
		SetLabel(fMoudle->getName());
}

TextType BibleMenuItem::GetType()
{
		if (!strcmp(currentmodule->getType(), "Biblical Texts"))
			return TEXT_BIBLE;
		else
		if (!strcmp(currentmodule->getType(), "Commentaries"))
			return TEXT_COMMENTARY;
		else
		if (!strcmp(currentmodule->getType(), "Lexicons / Dictionaries"))
			return TEXT_LEXICON;
		else
		if (!strcmp(currentmodule->getType(), "Generic Books"))
			return TEXT_GENERIC;
		else
		{
			printf("Found module %s with type %s\n",
				currentmodule->getDescription(), currentmodule->getType());
			return TEXT_UNKNOWN;
		}
}



