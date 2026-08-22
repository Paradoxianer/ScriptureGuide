#ifndef __LAPP_H__
#define __LAPP_H__

class SGApp : public BApplication
{
public:
					SGApp();
					~SGApp(void);
	virtual void	MessageReceived(BMessage* message);
	// Double-clicking a bookmark file (.sgvb, #55) in Tracker delivers
	// it here -- navigates whichever SGMainWindow is currently active
	// (or the first one, if none is) to that reference, the same
	// SG_BIBLE path a dropped reference or a click in the verse list
	// window itself already use.
	virtual void	RefsReceived(BMessage* message);
	status_t		StartupCheck(void);
};

const char *	GetAppPath(void);
bool 			HelpAvailable(void);

#endif
