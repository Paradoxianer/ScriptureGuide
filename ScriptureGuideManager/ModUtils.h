#ifndef MODUTILS_H
#define MODUTILS_H

#include <stdio.h>
#include <String.h>
#include <Archivable.h>
#include <Message.h>

#include "../ScriptureGuide/constants.h"

#define SG_SETTINGS_PATH PREFERENCES_PATH
#define SG_PKGCACHE_PATH SG_SETTINGS_PATH "packages/"
#define SG_PKGINFO_PATH SG_SETTINGS_PATH "package-info/"
#define SG_MODULEBASE_PATH MODULES_PATH

class ConfigFile : public BArchivable
{
public:
	ConfigFile(void);
	~ConfigFile(void);
	ConfigFile(BMessage *archive);
	status_t Archive(BMessage *archive, bool deep=true) const;
	static BArchivable *Instantiate(BMessage *archive);
	
	// There are a great many more attributes kept in a .conf file, but
	// these are the ones we are most concerned with.
	BString fFileName;
	BString fZipFileName;
	BString fAbout;
	BString fDataPath;
	BString fDescription;
	BString fLanguage;
	BString fLicense;
	// Module type in SWORD's own vocabulary -- "Biblical Texts",
	// "Commentaries", "Lexicons / Dictionaries", "Generic Books", or a
	// finer category like "Daily Devotional" where the .conf names one.
	// See ReadConfigFile() for how it is derived.
	BString fType;
	float fFileSize;
};

status_t FilterConfigFile(const ConfigFile &cfile);

bool IsInstalled(const char *name);

status_t InstallModule(const char *name);
status_t UninstallModule(const char *name);
status_t InstallFromDisk(const char *path);

status_t ReadConfigFile(const char *name, ConfigFile &cfile);

// Both live in SGMan.cpp. Reachable from the window rather than private to
// the application object because the module list has to be obtainable
// AFTER startup: declining the network warning once used to leave an empty
// window with no control anywhere that could ask again (reported).
// SetupPackageList(true) discards the cached index and re-fetches, which is
// what Program -> Refresh module list means; false only fills in what is
// missing, which is what startup wants.
bool ConfirmNetworkAccess(void);
void SetupPackageList(bool force);

//const char *TranslateLanguageName(const BString &string);

#endif
