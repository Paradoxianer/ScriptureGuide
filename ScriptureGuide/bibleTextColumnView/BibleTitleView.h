/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TITLE_VIEW_H
#define BIBLE_TITLE_VIEW_H


#include "TitleView.h"


class BibleTitleView : public  TitleView{
public:
							BibleTitleView();
	virtual	BPopUpMenu*		CreateContextMenu();
			BMenu*			FindMenu(const char *name);

private:

};


#endif // _H
