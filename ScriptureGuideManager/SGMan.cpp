#include <stdio.h>
#include <stdlib.h>
#include <Alert.h>
#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <String.h>
#include <Directory.h>
#include <List.h>
#include <Application.h>

#include "ModUtils.h"
#include "MainWindow.h"
#include "DownloadLocations.h"

BList gFileNameList;
BList gFileSizeList;
BList fConfFileList;

class SGMApp : public BApplication
{
public:
	SGMApp(void);
	~SGMApp(void);

	void SetupPackageList(void);
	bool ConfirmNetworkAccess(void);
};

SGMApp::SGMApp(void)
 : BApplication("application/x-vnd.wgp.ScriptureGuideManager")
{
	// Fetching the package list and downloading modules both mean
	// contacting crosswire.org over plain, unencrypted HTTP -- in some
	// countries that alone can be enough to put someone at risk. Ask
	// before any of that happens, not after; if the user declines, skip
	// SetupPackageList() (and therefore every wget call in it) entirely
	// and still open the window, just with an empty list.
	if (ConfirmNetworkAccess())
		SetupPackageList();
	MainWindow *win=new MainWindow(BRect(300,200,900,600));
	win->Show();
}


// Existence of this file means "don't ask again" was chosen previously --
// its content doesn't matter, only that it's there.
#define SG_SKIP_WARNING_MARKER SG_SETTINGS_PATH "skip-network-warning"

bool
SGMApp::ConfirmNetworkAccess(void)
{
	BEntry marker(SG_SKIP_WARNING_MARKER);
	if (marker.Exists())
		return true;

	BAlert* alert = new BAlert("Network Access",
		"ScriptureGuide Book Manager needs to contact crosswire.org over "
		"the internet (plain HTTP, not encrypted) to list and download "
		"Bible modules.\n\n"
		"In some countries, accessing Bible translations or other "
		"religious content online is monitored or restricted, and doing "
		"so could put you at risk. Only continue if you're sure this is "
		"safe where you are.",
		"Cancel", "Don't ask again", "Continue", B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice = alert->Go();

	if (choice == 1)
	{
		// "Don't ask again" -- also proceed this time, same as Continue;
		// asking a second time right after they just said "don't ask
		// again" would defeat the point.
		create_directory(SG_SETTINGS_PATH, 0777);
		BFile(SG_SKIP_WARNING_MARKER, B_CREATE_FILE | B_WRITE_ONLY);
		return true;
	}

	return choice == 2; // "Continue"
}

SGMApp::~SGMApp(void)
{
}

#define EXEC(dir, cmd) system("cd " dir " && " cmd)

void SGMApp::SetupPackageList(void)
{
	// The package info is kept in a subfolder of the regular scripture guide settings
	// The information for each package is kept in a flattened BMessage which has
	// the same name as the zip file kept on the Crosswire FTP site, complete with
	// case. Inside this message is kept the necessary information for the user
	// to be able to decide whether or not the module should be installed.
	
	BEntry entry(SG_SETTINGS_PATH);
	BDirectory dir(SG_PKGINFO_PATH);
	BFile file;
	
	if(entry.InitCheck()!=B_OK)
	{
		printf("Couldn't read package file\n");
		return;
	}
	
	if(!entry.Exists())
		create_directory(SG_SETTINGS_PATH,0777);
	
	entry.SetTo(SG_PKGINFO_PATH);
	if(!entry.Exists())
		create_directory(SG_PKGINFO_PATH,0777);
	
	// Check for the index file
	entry.SetTo(SG_PKGINFO_PATH "index.html");
	if(!entry.Exists())
	{
		EXEC(SG_PKGINFO_PATH, "wget " SG_DOWNLOAD_PKGS);
		// Extracting names and sizes as two independent lists (each
		// piped through its own `awk 'NF'` to drop blank lines) used to
		// pair them back up purely by array index once read back in --
		// which breaks the instant the two lists end up different
		// lengths. Confirmed empirically (#36) against the real
		// crosswire index.html: the column-header row's own "Name"
		// link matches the same $13 field position as a real package
		// name, but its $23 position lands on an empty gap between two
		// adjacent tags (there's no size cell to speak of in a header
		// row) -- so packagelist.txt gets a phantom "Name" entry that
		// `awk 'NF'` filters out of packagesizes.txt, shifting every
		// real package's size against the wrong name from that point
		// on (465 names, 464 sizes, live-tested).
		//
		// Filtering to only the lines that actually contain a ".zip"
		// link before extracting anything (excluding the header, the
		// parent-directory row, and the <hr> separator row in one
		// step) and pulling name+size from the very same matched line
		// in a single awk pass makes them structurally impossible to
		// desync -- there is no longer a second, independently-filtered
		// list for them to fall out of step with.
		EXEC(SG_PKGINFO_PATH, "grep '\\.zip\">' index.html "
			"| awk -F'[<>]' '{print $13\"\\t\"$23}' >packages.txt");
	}
	
	entry.SetTo(SG_PKGINFO_PATH "mods.d.tar.gz");
	if(!entry.Exists())
	{
		EXEC(SG_PKGINFO_PATH, "wget " SG_DOWNLOAD_MODS);
		
		// Get and unpack the compressed list of config files. There should be more config
		// files than there are modules or at least just as many.
		EXEC(SG_PKGINFO_PATH, "tar -xvpzf mods.d.tar.gz -C " SG_PKGINFO_PATH);
		
		entry.SetTo(SG_PKGINFO_PATH "mods.d");
		entry.Rename("configfiles");
	}
	
	dir.SetTo(SG_PKGINFO_PATH);
	if(dir.CountEntries() != 1)
	{
		BFile file(SG_PKGINFO_PATH "packages.txt",B_READ_ONLY);
		off_t filesize;
		BString filedata;
		status_t err = B_OK;

		file.GetSize(&filesize);

		if(filesize<=0)
		{
			printf("Package list file size is 0\n");
			return;
		}

		char *data=new char[filesize+1];

		err = file.Seek(0,SEEK_SET);
		if (err !=B_OK)
			printf ("Error: %s\n",strerror(err));
		err = file.Read(data,filesize);
		if (err !=B_OK)
			printf ("Error: %s\n",strerror(err));
		data[filesize] = '\0';
		file.Unset();

		filedata.SetTo(data);
		filedata.RemoveAll("\"");
		filedata.RemoveAll("rawzip/");
		delete [] data;

		// Each line is "name\tsize", from the same source line in
		// packages.txt (see the EXEC() call above) -- splitting each
		// one individually here, rather than tokenizing the whole blob
		// into two separate lists the way this used to, is what
		// actually guarantees gFileNameList/gFileSizeList stay paired
		// (#36).
		int32 lineStart = 0;
		while (lineStart < filedata.Length())
		{
			int32 lineEnd = filedata.FindFirst("\n", lineStart);
			if (lineEnd < 0)
				lineEnd = filedata.Length();

			BString line;
			filedata.CopyInto(line, lineStart, lineEnd - lineStart);
			lineStart = lineEnd + 1;

			int32 tab = line.FindFirst("\t");
			if (tab < 0)
				continue;

			BString name, size;
			line.CopyInto(name, 0, tab);
			line.CopyInto(size, tab + 1, line.Length() - tab - 1);
			name.Trim();
			size.Trim();
			if (name.IsEmpty() || size.IsEmpty())
				continue;

			gFileNameList.AddItem(new BString(name));
			gFileSizeList.AddItem(new BString(size));
		}

		// Now that we have the list of filenames, we iterate through the list
		// of filenames and derive the name of the config file by removing the .zip
		// and making it all lowercase. Then, we read the config file, parse it, 
		// save it to the global config list and also save the info to package info
		// file, as well.
		
		for(int32 i=0; i<gFileNameList.CountItems(); i++)
		{
			BString *currentfile=(BString*) gFileNameList.ItemAt(i);
			if(!currentfile)
				continue;
			
			BString *filesizeentry=(BString*) gFileSizeList.ItemAt(i);
			if(!filesizeentry)
				continue;
			
			if(filesizeentry->Compare("0.0")==0)
			{
				printf("%s is an empty file\n", currentfile->String());
				continue;
			}
			
			BString filename=*currentfile;
			filename.RemoveAll(".zip");
			
			BString conffilename(filename);
			conffilename+=".conf";
			conffilename.Prepend(SG_PKGINFO_PATH "configfiles/");
			
			entry.SetTo(conffilename.String());
			if(entry.InitCheck()!=B_OK)
				continue;
			
			if(!entry.Exists())
				conffilename.ToLower();
				
			entry.SetTo(conffilename.String());
			if(entry.InitCheck()!=B_OK)
				continue;
			
			if(!entry.Exists())
			{
				printf("Entry %s doesn't exist\n",conffilename.String());
				continue;
			}
			
			ConfigFile configfile;
			if(ReadConfigFile(conffilename.String(),configfile)!=B_OK)
				continue;
			
			configfile.fZipFileName=currentfile->String();
			configfile.fZipFileName.RemoveAll(".zip");
			sscanf(filesizeentry->String(),"%f",&(configfile.fFileSize));
			
			if(FilterConfigFile(configfile)!=B_OK)
				continue;
			
			BMessage message;
			configfile.Archive(&message);

			conffilename=SG_PKGINFO_PATH;
			conffilename+=filename;
			
			file.SetTo(conffilename.String(),B_READ_WRITE|B_CREATE_FILE|B_ERASE_FILE);
			message.Flatten(&file);
		}
	}
	
	dir.SetTo(SG_PKGINFO_PATH);
	entry_ref ref;
			
	int32 entrycount=dir.CountEntries();
	for(int32 i=0; i<entrycount; i++)
	{
		if(dir.GetNextEntry(&entry)==B_OK && entry.InitCheck()==B_OK)
		{
			entry.GetRef(&ref);
			entry.Unset();
			
			BFile file(&ref,B_READ_ONLY);
			BMessage msg;
			
			if(msg.Unflatten(&file)==B_OK)
			{
				ConfigFile *conffile=(ConfigFile*)ConfigFile::Instantiate(&msg);
				if(conffile)
					fConfFileList.AddItem(conffile);
			}
		}
	}

}

int main(void)
{
	SGMApp app;
	app.Run();
	return 0;
}

