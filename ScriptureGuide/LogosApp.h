#ifndef __LAPP_H__
#define __LAPP_H__

class SwordBackend;

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

private:
	// StartupCheck() already has to build a SwordBackend (the full
	// SWMgr module scan) just to answer "is anything installed at
	// all" -- kept here afterward instead of thrown away, so the
	// SGMainWindow the constructor builds next can reuse it rather
	// than paying for that same scan a second time. NULL once handed
	// off to that window (which then owns it); only still non-NULL,
	// and only then deleted by ~SGApp(), if StartupCheck() succeeded
	// but something prevented the handoff.
	SwordBackend*	fStartupBackend;
};

const char *	GetAppPath(void);
bool 			HelpAvailable(void);

#endif
