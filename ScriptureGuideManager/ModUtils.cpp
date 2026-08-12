#include "ModUtils.h"

#include <string.h>

#include <File.h>
#include <Entry.h>
#include <Directory.h>
#include <Entry.h>

ConfigFile::ConfigFile(void)
{
}

ConfigFile::ConfigFile(BMessage *archive)
 : BArchivable(archive)
{
	BMessage msg;
	
	archive->FindString("about",&fAbout);
	archive->FindString("description", &fDescription);
	archive->FindString("license",&fLicense);
	archive->FindString("datapath",&fDataPath);
	archive->FindString("language",&fLanguage);
	archive->FindString("filename",&fFileName);
	archive->FindString("zipfilename",&fZipFileName);
	archive->FindString("type",&fType);
	archive->FindFloat("filesize",&fFileSize);
}

ConfigFile::~ConfigFile(void)
{
}

BArchivable *ConfigFile::Instantiate(BMessage *archive)
{
	if(validate_instantiation(archive,"ConfigFile"))
		return new ConfigFile(archive);
	return NULL;
}

status_t ConfigFile::Archive(BMessage *archive, bool deep) const
{
	status_t status=BArchivable::Archive(archive,deep);
	if(status!=B_OK)
		return status;
	
	archive->AddString("class","ConfigFile");
	archive->AddString("about",fAbout);
	archive->AddString("description",fDescription);
	archive->AddString("license",fLicense);
	archive->AddString("datapath",fDataPath);
	archive->AddString("language",fLanguage);
	archive->AddString("filename",fFileName);
	archive->AddString("zipfilename",fZipFileName);
	// Must be archived, not just parsed: SetupPackageList() writes each
	// ConfigFile to package-info/<NAME> and the list the window shows is
	// built by reading those back (see SGMan.cpp), so a field missing
	// here is a field that silently never reaches the UI -- which is
	// exactly how the Type column came up empty the first time.
	archive->AddString("type",fType);
	archive->AddFloat("filesize",fFileSize);
	return B_OK;
}

bool IsInstalled(const char *name)
{
	// Expects the name of the config file
	if(!name)
		return false;
	
	BString path(name);
	path.Prepend(SG_MODULEBASE_PATH "mods.d/");
	
	BEntry entry(path.String());
	return entry.Exists();
}

status_t InstallModule(const char *name)
{
	return B_ERROR;
}

status_t UninstallModule(const char *name)
{
	return B_ERROR;
}

status_t InstallFromDisk(const char *path)
{
	return B_ERROR;
}

// Reads "key=value" from a .conf, anchored to the START OF A LINE.
//
// The older lookups in ReadConfigFile() below use a plain FindFirst() on
// "key=", which also matches the middle of a line -- an About= paragraph
// mentioning "Category=" would be picked up as the category. They are
// left as they are (they work on the real files, and changing parsing
// this project already depends on is a separate, riskier change), but
// anything added from here on should use this.
static status_t
GetConfigValue(const BString &contents, const char *key, BString &out)
{
	BString prefix(key);
	prefix << "=";
	BString needle("\n");
	needle << prefix;

	int32 offset;
	if(contents.Compare(prefix.String(), prefix.Length()) == 0)
		offset = prefix.Length();	// very first line, no leading newline
	else
	{
		offset = contents.FindFirst(needle.String(), 0);
		if(offset == B_ERROR)
			return B_ENTRY_NOT_FOUND;
		offset += needle.Length();
	}

	int32 lineend = contents.FindFirst("\n", offset);
	if(lineend == B_ERROR)
		lineend = contents.Length();

	contents.CopyInto(out, offset, lineend - offset);
	out.Trim();
	return B_OK;
}


status_t ReadConfigFile(const char *name, ConfigFile &cfile)
{
	if(!name)
		return B_ERROR;
		
	BEntry entry(name);
	status_t status;
	
	status=entry.InitCheck();
	if(status!=B_OK)
		return status;
	
	entry_ref ref;
	entry.GetRef(&ref);
	cfile.fFileName=ref.name;
	
	BFile file(&entry,B_READ_ONLY);
	status=file.InitCheck();
	if(status!=B_OK)
		return status;
	
	off_t filesize;
	file.GetSize(&filesize);
	if(filesize<1)
	{
		status=B_BAD_VALUE;
		return status;
	}
	
	BString filecontents;
	
	char *filedata=new char[filesize];
	file.Read(filedata,filesize);
	
	filecontents=filedata;
	
	delete [] filedata;
	
	int32 offset=0,lineend=0;
	
	filecontents.RemoveAll("\r");
	
	// This is to make sure that the file ends in a newline. If a .conf
	// file doesn't and one of these entries is at the end, it will crash. :(
	filecontents+="\n";
	
	offset=filecontents.FindFirst("Description=",0);
	if(offset!=B_ERROR)
	{
		offset+=12;
		lineend=filecontents.FindFirst("\n",offset);
		filecontents.CopyInto(cfile.fDescription,offset,lineend-offset);
	}
	
	offset=filecontents.FindFirst("About=",0);
	if(offset!=B_ERROR)
	{
		offset+=6;
		lineend=filecontents.FindFirst("\n",offset);
		filecontents.CopyInto(cfile.fAbout,offset,lineend-offset);
		cfile.fAbout.ReplaceAll("\\par ","\n");
		cfile.fAbout.ReplaceAll("\\par","\n");
		
		// TODO: See what else needs to be replaced or stripped out in
		// descriptions
	}
	
	offset=filecontents.FindFirst("DataPath=",0);
	if(offset!=B_ERROR)
	{
		offset+=9;
		lineend=filecontents.FindFirst("\n",offset);
		filecontents.CopyInto(cfile.fDataPath,offset,lineend-offset);
		
		if(cfile.fDataPath.ByteAt(0)=='.')
			cfile.fDataPath.RemoveFirst(".");
		if(cfile.fDataPath.ByteAt(0)=='/')
			cfile.fDataPath.RemoveFirst("/");
	}
	
	offset=filecontents.FindFirst("Lang=",0);
	if(offset!=B_ERROR)
	{
		offset+=5;
		lineend=filecontents.FindFirst("\n",offset);
		filecontents.CopyInto(cfile.fLanguage,offset,lineend-offset);
	}
	
	offset=filecontents.FindFirst("DistributionLicense=",0);
	if(offset!=B_ERROR)
	{
		offset+=20;
		lineend=filecontents.FindFirst("\n",offset);
		filecontents.CopyInto(cfile.fLicense,offset,lineend-offset);
	}
	
	// Module type, derived from ModDrv= alone. Per CrossWire's conf-file
	// specification (wiki.crosswire.org/DevTools:conf_Files) the driver
	// is what determines the kind of module, and Category= exists "to
	// further categorize modules beyond what can be figured out by the
	// ModDrv" -- it SUPPLEMENTS the driver rather than overriding it.
	//
	// Deliberately not using Category= for this column even though it is
	// more specific where present. It appears in only 41 of the 426
	// config files, and its values are a different axis entirely
	// ("Daily Devotional", "Glossaries", "Utility", "Essays"). Mixing
	// the two would give a column with nine possible values, most of
	// them present on a handful of rows -- which is worse for the one
	// thing this column is for, namely sorting (issue #41). ModDrv
	// yields exactly four, on every row, and they are the same four the
	// reading app itself switches on (see SwordBackend.cpp).
	//
	// The full driver list from that spec:
	//   RawText, RawText4, zText, zText4          -> Biblical Texts
	//   RawCom, RawCom4, zCom, zCom4, HREFCom,
	//     RawFiles                                -> Commentaries
	//   RawLD, RawLD4, zLD                        -> Lexicons / Dictionaries
	//   RawGenBook                                -> Generic Books
	// The checks match substrings because of the compressed drivers'
	// leading 'z' and the ">64K" variants' trailing '4'.
	BString modDrv;
	if(GetConfigValue(filecontents,"ModDrv",modDrv)==B_OK)
	{
		if(modDrv.IFindFirst("Text")>=0)
			cfile.fType="Biblical Texts";
		else if(modDrv.IFindFirst("Com")>=0 || modDrv.ICompare("RawFiles")==0)
			cfile.fType="Commentaries";
		else if(modDrv.IFindFirst("LD")>=0)
			cfile.fType="Lexicons / Dictionaries";
		else if(modDrv.IFindFirst("GenBook")>=0)
			cfile.fType="Generic Books";
	}
	if(cfile.fType.IsEmpty())
		cfile.fType="Unknown";
	
	return B_OK;
}

status_t FilterConfigFile(const ConfigFile &cfile)
{
	if(cfile.fLanguage.Compare("zh")==0)
		return B_ERROR;
	
	return B_OK;
}

/*
const char *TranslateLanguageName(const BString &string)
{
	char first, second;
	
	first=string.ByteAt(0);
	second=string.ByteAt(1);
	
	switch(first)
	{
		case
	}
}
*/
