/*
Author: Jackson Pfeffer
Contact: crazykidjack@gmail.com
Date: 2021-03-28
Compatibility: This program should compile for Windows and *nix
Description: Converts VRC alias text files to XML and inserts that XML
  into the specified "facility files" (.gz)
  that users import into vSTARS and vERAM
*/

//////////////////////////////////////////////////////////////////////////////
//INCLUDE FILES & NAMESPACES
//////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <bitcompressor.hpp> //Release version:3.1.2 - https://github.com/rikyoz/bit7z
#include <bitstreamcompressor.hpp>
#include <bitextractor.hpp>
#include <bitexception.hpp>
#include <bitformat.hpp>
using bit7z::Bit7zLibrary;
using bit7z::BitCompressor;
using bit7z::BitStreamCompressor;
using bit7z::BitExtractor;
using bit7z::BitException;
#endif

#include <limits>
#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string>
#include <string.h>
#include <ctype.h>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <unordered_set>

using namespace std;
using std::filesystem::path;
namespace chrono = std::chrono;
using sys_clock = chrono::system_clock;
using chrono::time_point;
using chrono::duration;
//using chrono::zoned_time;

//////////////////////////////////////////////////////////////////////////////
//STRUCTS & CLASSES
//////////////////////////////////////////////////////////////////////////////
//represents all of the attributes of the vSTARS PositionInfo XML entity
typedef struct Position{
public:
  string sectorName, radioName, prefix, suffix, sectorID, positionType;
  char posSym = '\0';
  float freq = -1.0;
} Position;

typedef struct Config {
public:
  unordered_set<string> inFacilityLst;
  unordered_set<string> adjacentLst;
} Config;

enum class InfoType { NONE, CMDS, POS };

//////////////////////////////////////////////////////////////////////////////
//CONSTANTS
//////////////////////////////////////////////////////////////////////////////
int static const GOOD = 0;
int static const NUM_ARGS = 1;
int static const OPEN_FILE_FAILURE = 2;
int static const TIME_FAILURE = 4;
int static const NOT_CLEAN = 8;
int static const GZIP_COMPRESS_ERROR = 16;
int static const GZIP_EXTRACT_ERROR = 32;
int static const DELETE_FILE_FAILURE = 64;
int static const FACILITY_FILE_FORMAT = 128;
int static const CONFIG_FORMAT = 256;
int static const TIME_STR_ERROR = 512;

string static const DEFAULT_CFG = "default.v2xcfg";
int static const FACILITY_IDX = 2;
int static const UPDATE_TIME_STR_LEN = 34;

//////////////////////////////////////////////////////////////////////////////
//GLOBAL VARIABLES
//////////////////////////////////////////////////////////////////////////////
string thisProg_ = "";
int status_ = GOOD;
path tmpFldrPath_;
Config cfg_;

//////////////////////////////////////////////////////////////////////////////
//FUNCTION DECLARATIONS
//////////////////////////////////////////////////////////////////////////////
void prntHelp(ostream& out = cerr);
void cleanNExit();
void prntNExit(string const& msg, ostream& out = cerr);
void chkArgs(int const& numArgs, char** const& argLst);

string getTimeStr(); //YYMMDDhhmmss
string getUpdateTimeStr(); //YYYY-MM-DDThh:mm:ss.*******-tz:tz
int getPid();
path genTmpFldr();
void initCfg();
void init(int const& numArgs, char** const& argLst);

//checks if filePath exists
//if not, returns
//if it *does* already exist, it prompts user for action
//  and potentially updates filePath based on user input
//post-condition: Either the program will exit, filePath will NOT exist,
//  or user will choose to clobber pre-existing filePath
void verifyFilePath(path& filePath);
void delFilePath(path const& filePath);
void verifyNDelFilePath(path& filePath);
ifstream openInStrm(path const& filePath);

//calls verifyFilePath() on filePath
//then opens an ofstream in out|trunc mode
ofstream openOutStrm(path& filePath, bool const& force = false);
ofstream openOutStrm(path&& filePath, bool const& force = false);

//Escpaes ALL of the following characters found in str:
//  & ' < " >
//If there are any of those characters that you do NOT want escaped...
//  (like the < and > and the beginning and end of an XML entity)
//  do not include them in str
void escapeXML(string& str);
void escapeXML(string&& str);
string cnvrtVRCaliasLine2XML(string const& aliasLine);
//ONLY cnvrts lines that start with a dot (.)
stringstream cnvrtVRCalias2XML(ifstream& vrcAliasFile);
string cnvrtVRCpositionLine2XML(string const& aliasLine);
//reads a line from the VRC pof file and rtns a corresponding Position
//also calls escapeXML() for all string members of rtnd Position
Position initPosition(string const& positionLine);
//cnvrts ALL lines that do NOT start with a semi-colon (;)
stringstream cnvrtVRCpof2XML(ifstream& vrcPofFile);
void gzipFile(path const& filePath);
void gzipStrm(stringstream& in, path& filePath);
stringstream ungzip2Strm(path const& filePath);

// --- --- --- DEPRACATED --- --- --- //
//Reads facility files in argLst[3+2n] where n is an integer.
//  Reads to end of argLst
//Adds cmdBlock to the appropriate location and outputs to new facility files
//New facility files names are defined in argLst[3+2n+1]
//  Each new name defines the output file of the input name sequentially
//  before it in argLst
//CAUTION: If the output file name is the same as the input name,
//  this will automatically clobber the original file
void addCmds2Facilities(
  string const& cmdBlock, int const& numArgs, char** const& argLst
);

//Reads facility files in argLst[3+2n] where n is an integer.
//  Reads to end of argLst
//Adds cmdBlock and posBlock to the appropriate location
//  and outputs to new facility files
//Pre-condition: Original faciality file names are define in argLst[3+2n]
//Pre-condition: New facility file names are defined in argLst[3+2n+1]
//  Each new name defines the output file of the input name sequentially
//  before it in argLst
//CAUTION: If the output file name is the same as the input name,
//  this will automatically clobber the original file
void updateFacilityFiles(
  string const& cmdBlock, string const& posBlock,
  int const& numArgs, char** const& argLst
);

//////////////////////////////////////////////////////////////////////////////
//MAIN FUNCTION
//////////////////////////////////////////////////////////////////////////////
int main(int numArgs, char* argLst[]) {
  init(numArgs, argLst);

  path vrcAliasPath(argLst[1]);
  ifstream vrcAliasFile = openInStrm(vrcAliasPath);
  stringstream commAliasesXML = cnvrtVRCalias2XML(vrcAliasFile);
  
  /*
  path vrcPofPath(argLst[2]);
  ifstream vrcPofFile = openInStrm(vrcPofPath);
  stringstream positionsXML = cnvrtVRCpof2XML(vrcPofFile);
  */
  stringstream positionsXML;

  cout << endl << "This will take a moment, please wait..." << endl;
  updateFacilityFiles(
    commAliasesXML.str(), positionsXML.str(),
    numArgs, argLst
  );

  cleanNExit();
}//end main
//////////////////////////////////////////////////////////////////////////////
//END MAIN FUNCTION
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//FUNCTION DECLARATIONS
//////////////////////////////////////////////////////////////////////////////
//----------------------------------------------------------------------------
void prntHelp(ostream& out) {
  out << "Placeholder" << endl;
  //key: [] = required arg
  //     <> = optional arg
  //     {} = arg groups
  //usage: prog [VRCAliasPath] [VRCPofPath] [{originalFacilityFilePath newFacilityFilePath}...]
  //Automatically looks for default.v2xcfg file in install directory.
  ///  Uses that if found, otherwise prompts for location of .v2xcfg
}//end prntHelp

//----------------------------------------------------------------------------
void cleanNExit() {
  //this variable is used to force remove_all to return error codes
  //to detect an error condition
  //...though currently *which* error the OP API reports is not being checked
  error_code err;
  //this condition comes straight from the documentation
  // for the remove_all funciton to test for error return
  if(filesystem::remove_all(tmpFldrPath_, err) == static_cast<uintmax_t>(-1)){
    status_ += NOT_CLEAN;
    cerr << "ERROR: Could not clean up temporary files. " << endl
         << "All remaining files are located at: " << tmpFldrPath_.string()
         << endl << endl;
  }
  exit(status_);
}//end cleanNExit

//----------------------------------------------------------------------------
void prntNExit(string const& msg, ostream& out) {
  out << endl << msg << endl;
  cleanNExit();
}//end prntNExit

//----------------------------------------------------------------------------
void chkArgs(int const& numArgs, char** const& argLst) {
  //if (numArgs >= 5) //for pof
  if (numArgs >= 4)
    return;

  status_ += NUM_ARGS;
  prntHelp();
  prntNExit("Incorrect number of arguments");
}//end chkArgs

//----------------------------------------------------------------------------
string getTimeStr() {
  time_t currTime = sys_clock::to_time_t(sys_clock::now());
  char timeStrBuf[15];
  struct tm tm;
  localtime_s(&tm, &currTime);
  if (strftime(timeStrBuf, 15, "%Y%m%d%H%M%S", &tm) != 14) {
    status_ += TIME_FAILURE;
    prntNExit("Error while generating time current string");
  }

  return timeStrBuf;
}//end getTimeStr

//----------------------------------------------------------------------------
/*
string getUpdateTimeStr() {
  //<CommandAliasesLastImported>2021-03-24T19:26:55.1456232-04:00</CommandAliasesLastImported>
  time_t currTime = system_clock::to_time_t(system_clock::now());
  char timeStrBuf[15];
  struct tm tm;
  localtime_s(&tm, &currTime);
  if (strftime(timeStrBuf, 15, "%FT%T.0-", &tm) != 14) {
    status_ += TIME_FAILURE;
    prntNExit("Error while generating time current string");
  }

  //zoned_time 

  return timeStrBuf;
}//end getUpdateTimeStr
*/
string getUpdateTimeStr() {
  //<CommandAliasesLastImported>2021-03-24T19:26:55.1456232-04:00</CommandAliasesLastImported>
  time_point now = sys_clock::now();
  time_t now_time = sys_clock::to_time_t(now);
  struct tm nowInfo;
  if (gmtime_s(&nowInfo, &now_time) != 0) {
    status_ += TIME_STR_ERROR;
    cerr << endl << "Warning: Could not convert now_time to zulu time... contining..." << endl;
    return "";
  }

  //get current zulu time seconds with partial seconds
  duration<double> secs = now - sys_clock::from_time_t(now_time) + chrono::seconds(nowInfo.tm_sec);
  char timeStrBuf[UPDATE_TIME_STR_LEN];
  if (strftime(timeStrBuf, 18, "%FT%H:%M:", &nowInfo) == 0) {
    status_ += TIME_STR_ERROR;
    cerr << endl << "Warning: Error printing zulu time to string... continuing..." << endl;
    return "";
  }
  sprintf_s((timeStrBuf + 17), 17, "%#010.7f-00:00", secs.count());
  return timeStrBuf;
}//end getUpdateTimeStr

//----------------------------------------------------------------------------
int getPid() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return ::getpid();
#endif
}//end getPid

//----------------------------------------------------------------------------
path genTmpFldr() {
  string timeStr = getTimeStr();
  int pid = getPid();

  string tmpFldrSfx = thisProg_+"."+timeStr+"."+to_string(pid)+".";
  int cnt = 0;
  path tmpFldrPath = filesystem::temp_directory_path() / (tmpFldrSfx + to_string(cnt) + ".tmp");
  while (true) {
    if (filesystem::create_directories(tmpFldrPath)) break;
    ++cnt;
    tmpFldrPath = filesystem::temp_directory_path() / (tmpFldrSfx + to_string(cnt) + ".tmp");
  }
    
  return tmpFldrPath;
}//end genTmpFldr

void readNPopulateCfg(ifstream& cfgFileStrm) {
  string sectorLstLine, sectorID;
  getline(cfgFileStrm, sectorLstLine);
  stringstream sectorLstStrm(sectorLstLine);
  while (sectorLstStrm >> sectorID)
    cfg_.inFacilityLst.insert(sectorID);

  getline(cfgFileStrm, sectorLstLine);
  sectorLstStrm = stringstream(sectorLstLine);
  while (sectorLstStrm >> sectorID)
    cfg_.adjacentLst.insert(sectorID);
}//end readNPopulateCfg

//write and reads "In Facility" list first,
//  then "Adjacent" list
void initCfg() {
  path cfgPath(DEFAULT_CFG);
  if (filesystem::exists(cfgPath)) {
    ifstream cfgFileStrm = openInStrm(cfgPath);
    readNPopulateCfg(cfgFileStrm);
    return;//!!! EXIT FUNCTION HERE !!!//
  }
  
  string response;
  //WHILE INPUT INVALID
  while (response.length() != 1) {
    if (!response.empty()) cout << "Incorrect input... try again..." << endl;

    cout << "default.v2xcfg not found, do you have a config file (y/n)? ";
    getline(cin, response);
  }//END WHILE INPUT INVALID

  if (toupper(response[0]) == 'Y') {
    string cfgFileName;
    cout << "Enter path to config file: ";
    getline(cin, cfgFileName);
    cfgPath = cfgFileName;
    ifstream cfgFileStrm = openInStrm(cfgPath);
    readNPopulateCfg(cfgFileStrm);
    return;//!!! EXIT FUNCTION HERE !!!//
  }
  response.clear();

  //WHILE INPUT INVALID
  while (response.length() != 1) {
    if (!response.empty()) cout << "Incorrect input... try again..." << endl;

    cout << endl << "Would you like to set up a new config?"
          << endl << "Without one, all positions will be marked \"other\""
          << " (y/n)? ";
    getline(cin, response);
  }//END WHILE INPUT INVALID

  //ask user to continue without cfg or create new one
  if (toupper(response[0]) != 'Y') {
    cout << "Continuing without \"Adjacent\" or \"In Facility\" "
          << "position config..." << endl;
    return;//!!! EXIT FUNCTION HERE !!!//
  }
  response.clear();

  //prompt for new cfg
  string input;
  cout << "Enter \"In Facility\" sector IDs, each separated by a space: ";
  getline(cin, input);
  stringstream inputStrm(input);
  input.clear();
  while (inputStrm >> input)
    cfg_.inFacilityLst.insert(input);

  cout << "Enter \"Adjacent\" sector IDs, each separated by a space: ";
  getline(cin, input);
  inputStrm = stringstream(input);
  input.clear();
  while (inputStrm >> input)
    cfg_.adjacentLst.insert(input);

  //create new cfg file
  cout << "Enter the path where you would like to save the new config file. " 
        << "Leave blank for default auto-loading cfg file (do not include extension): ";
  getline(cin, input);
  if (input.empty()) input = "default";
  input.append(".v2xcfg");
  ofstream cfgFileStrm = openOutStrm((cfgPath = input));
  for (string const& sector : cfg_.inFacilityLst)
    cfgFileStrm << sector << " ";
  cfgFileStrm << endl;
  for (string const& sector : cfg_.adjacentLst)
    cfgFileStrm << sector << " ";
}//end initCfg

//----------------------------------------------------------------------------
void init(int const& numArgs, char** const& argLst) {
  thisProg_ = argLst[0];
  path tmp(thisProg_);
  thisProg_ = tmp.filename().string();

  chkArgs(numArgs, argLst);

  //tmpFldrPath_ = genTmpFldr();
  cerr << "tmpFldrPath: " << tmpFldrPath_.string() << endl;

  //initCfg();
}//end init

//----------------------------------------------------------------------------
void verifyFilePath(path& filePath) {
  //ask what user wants if file already exists
  while (filesystem::exists(filePath)) {
    string select = "0";
    cout << "Output file \"" << filePath.string() << "\" already exists. "
      << endl << "Would you like to:" << endl
      << "  (1) Clobber and overwrite this file path" << endl
      << "  (2) Enter a new file path" << endl
      << "  (3) Exit" << endl
      << "Type selection and press [enter]: ";
    getline(cin, select);

    if (select == "1") break;
    if (select == "2") {
      cout << "Enter new file path: ";
      getline(cin, select);
      filePath = select;
      select = "0";
    }//end if select == 2
    else if (select == "3")
      cleanNExit();
    else {
      cout << "Invalid choice... try again." << endl;
      select = "0";
    }//end else
  }//end while file already exists
}//end verifyFilePath

//----------------------------------------------------------------------------
void delFilePath(path const& filePath) {
  error_code err;
  if ((filesystem::remove(filePath, err) == false) && (err.value() != 0)) {
    status_ += DELETE_FILE_FAILURE;
    prntNExit("ERROR: Could not delete pre-existing output gzip file.");
  }
}//end delFilePath

//----------------------------------------------------------------------------
void verifyNDelFilePath(path& filePath) {
  verifyFilePath(filePath);
  //at this point, the new GZip Path is guaranteed to hold a path that
  //  we can do whatever we want with...
  //  so delete it in prep for bit7z compressor
  delFilePath(filePath);
}//end verifyNDelFilePath

//----------------------------------------------------------------------------
ifstream openInStrm(path const& filePath) {
  ifstream inFileStrm(filePath);
  if (!inFileStrm) {
    status_ += OPEN_FILE_FAILURE;
    prntNExit("Unable to open input file path: "s + filePath.string());
  }//end if

  return inFileStrm;
}//end openInStrm

//----------------------------------------------------------------------------
ofstream openOutStrm(path& filePath, bool const& force) {
  if(!force) verifyFilePath(filePath);

  ofstream outFileStrm(filePath, ios_base::out|ios_base::trunc);
  if (!outFileStrm) {
    status_ += OPEN_FILE_FAILURE;
    prntNExit("Unable to open output file path: "s + filePath.string());
  }//end if

  return outFileStrm;
}//end openOutStrm(path&)

//----------------------------------------------------------------------------
ofstream openOutStrm(path&& filePath, bool const& force) {
  if (!force) verifyFilePath(filePath);

  ofstream outFileStrm(filePath, ios_base::out | ios_base::trunc);
  if (!outFileStrm) {
    status_ += OPEN_FILE_FAILURE;
    prntNExit("Unable to open output file path: "s + filePath.string());
  }//end if

  return outFileStrm;
}//end openOutStrm(path&&)

//----------------------------------------------------------------------------
void escapeXML(string& str) {
  char searchTermLst[4] = { '&', '"', '\'', '<' };
  string replacementLst[4] = { "&amp;", "&quot;", "&apos;", "&lt;" };
  char term = '\0';
  string rplcmnt;
  size_t escapePos = -1;
  //LOOP THRU INVALID terms
  for (int termIdx = 0; termIdx < 4; ++termIdx) {
    term = searchTermLst[termIdx];
    rplcmnt = replacementLst[termIdx];
    escapePos = str.find(term);
    //LOOP WHILE AN INSTNACE OF term IS STILL IN str
    while (escapePos != string::npos) {
      str.replace(escapePos, 1, rplcmnt);
      escapePos += (rplcmnt.length() - 1);
      escapePos = str.find(term, escapePos);
    }//END LOOP THRU str FOR term
  }//END LOOP THRU INVALID CHARS
}//end escapeXML

//----------------------------------------------------------------------------
void escapeXML(string&& str) {
  char searchTermLst[4] = { '&', '"', '\'', '<'};
  string replacementLst[4] = { "&amp;", "&quot;", "&apos;", "&lt;" };
  char term = '\0';
  string rplcmnt;
  size_t escapePos = -1;
  //LOOP THRU INVALID terms
  for (int termIdx = 0; termIdx < 4; ++termIdx) {
    term = searchTermLst[termIdx];
    rplcmnt = replacementLst[termIdx];
    escapePos = str.find(term);
    //LOOP WHILE AN INSTNACE OF term IS STILL IN str
    while (escapePos != string::npos) {
      str.replace(escapePos, 1, rplcmnt);
      escapePos += (rplcmnt.length()-1);
      escapePos = str.find(term, escapePos);
    }//END LOOP THRU str FOR term
  }//END LOOP THRU INVALID CHARS
}//end escapeXML

//----------------------------------------------------------------------------
string cnvrtVRCaliasLine2XML(string const& aliasLine) {
  int static cmdNameLen;
  cmdNameLen = aliasLine.find(' ');
  int static rplcmntIdx;
  rplcmntIdx = cmdNameLen + 1;

  string cmdName = aliasLine.substr(0, cmdNameLen);
  string rplcmnt = aliasLine.substr(rplcmntIdx);

  escapeXML(cmdName);
  escapeXML(rplcmnt);

  return "      <CommandAlias Command=\"" + cmdName + "\" ReplaceWith=\"" + rplcmnt + "\" />";
}//end cnvrtVRCaliasLine2XML

//----------------------------------------------------------------------------
stringstream cnvrtVRCalias2XML(ifstream& vrcAliasFile) {
  string aliasLine;
  stringstream cmdAliasesXML;
  cmdAliasesXML << "    <CommandAliases>";
  //LOOP THRU LINES OF VRC ALIAS FILE
  while (getline(vrcAliasFile, aliasLine)) {
    if (aliasLine[0] != '.') continue; //!!!GO TO NEXT LINE!!!//
    cmdAliasesXML << endl << cnvrtVRCaliasLine2XML(aliasLine);
  }//END LOOP THRU VRC ALIAS FILE
  cmdAliasesXML << endl << "    </CommandAliases>";
  //<CommandAliasesLastImported>2021-03-24T19:26:55.1456232-04:00</CommandAliasesLastImported>
  cmdAliasesXML << endl << "    <CommandAliasesLastImported>"+getUpdateTimeStr()+"</CommandAliasesLastImported>";

  return cmdAliasesXML;
}//end cnvrtVRCalias2XML

//----------------------------------------------------------------------------
Position initPosition(string const& positionLine) {
  Position pos;
  stringstream posLineStrm(positionLine);
  getline(posLineStrm, pos.sectorName, ':');
  getline(posLineStrm, pos.radioName, ':');
  posLineStrm >> pos.freq;
  posLineStrm.ignore(1, ':');
  getline(posLineStrm, pos.sectorID, ':');
  posLineStrm >> pos.posSym;
  posLineStrm.ignore(1, ':');
  getline(posLineStrm, pos.prefix, ':');
  getline(posLineStrm, pos.suffix, ':');

  pos.freq = (pos.freq - 100) * 1000;
  escapeXML(pos.sectorName);
  escapeXML(pos.radioName);
  escapeXML(pos.prefix);
  escapeXML(pos.suffix);
  escapeXML(pos.sectorID);

  if (cfg_.adjacentLst.count(pos.sectorID))
    pos.positionType = "Adjacent";
  else if(cfg_.inFacilityLst.count(pos.sectorID))
    pos.positionType = "InFacility";
  else
    pos.positionType = "Other";

  return pos;
}//end initPosition

//----------------------------------------------------------------------------
string cnvrtVRCpositionLine2XML(string const& positionLine) {
  Position pos = initPosition(positionLine);
  
  return "      <PositionInfo PositionType=\""s+pos.positionType+"\" SectorName=\""+pos.sectorName+"\" RadioName=\""+pos.radioName+"\" Prefix=\""+pos.prefix+"\" Suffix=\""+pos.suffix+"\" Frequency=\""+to_string(pos.freq)+"\" SectorID=\""+pos.sectorID+"\" PositionSymbol=\""+pos.posSym+"\" />";
}//end cnvrtVRCpositionLine2XML

//----------------------------------------------------------------------------
stringstream cnvrtVRCpof2XML(ifstream& vrcPofFile) {
  string positionLine;
  stringstream positionsXML;
  positionsXML << "    <Positions>";
  //LOOP THRU LINES OF VRC ALIAS FILE
  while (getline(vrcPofFile, positionLine)) {
    if (positionLine[0] == ';') continue; //!!!GO TO NEXT LINE!!!//
    positionsXML << endl << cnvrtVRCpositionLine2XML(positionLine);
  }//END LOOP THRU VRC ALIAS FILE
  positionsXML << endl << "    </Positions>";

  return positionsXML;
}//end cnvrtVRCpof2XML

//----------------------------------------------------------------------------
void gzipFile(path const& filePath){
  //try compress
  try {
    BitCompressor bit7zCompressor(Bit7zLibrary(), ::bit7z::BitFormat::GZip);

    path newGZipFacilityPath = filePath.wstring() + L".gz";
    verifyNDelFilePath(newGZipFacilityPath);

    bit7zCompressor.compressFile(
      filePath.wstring(),
      newGZipFacilityPath
    );
  }//end try
  catch (const BitException& err) {
    //do something with err.what()
    cerr << err.what() << endl;
    status_ += GZIP_COMPRESS_ERROR;
    cleanNExit();
  }//edn try compress / catch
}//end gzipFile

//----------------------------------------------------------------------------
void gzipStrm(stringstream& in, path& filePath) {
  //try compress
  try {
    Bit7zLibrary lib;
    BitStreamCompressor bit7zCompressor(lib, ::bit7z::BitFormat::GZip);
    verifyNDelFilePath(filePath);

    bit7zCompressor.compress(
      in,
      filePath
    );
  }//end try
  catch (const BitException& err) {
    //do something with err.what()
    cerr << err.what() << endl;
    status_ += GZIP_COMPRESS_ERROR;
    cleanNExit();
  }//edn try compress / catch
}//end gzipFile

//----------------------------------------------------------------------------
stringstream ungzip2Strm(path const& filePath) {
  stringstream fileStrm;

  //try extract
  try {
    Bit7zLibrary lib;
    BitExtractor bit7zExtractor(lib, ::bit7z::BitFormat::GZip);
    
    bit7zExtractor.extract(filePath.wstring(), fileStrm);
  }//end try
  catch (const BitException& err) {
    //do something with err.what()
    cerr << err.what() << endl;
    status_ += GZIP_EXTRACT_ERROR;
    cleanNExit();
  }//end try extract / catch

  return fileStrm;
}//end ungzip2Strm

//----------------------------------------------------------------------------
//depracated
void addCmds2Facilities(
  string const& cmdBlock, int const& numArgs, char** const& argLst
) {
  string facilityLine;
  bool orig = true, force = false, firstDone = false;
  //LOOP THRU ADD <CommandAliases>...</CommandAliases> TO EACH FACILITY FILE
  //actually increments facilityIdx += 2 on ea iteration
  for (int facilityIdx = FACILITY_IDX; facilityIdx < numArgs; ++facilityIdx) {
    //get input and output file names
    path facilityFilePath(argLst[facilityIdx++]);
    path newFacilityFilePath(argLst[facilityIdx]);

    //auto-clobber if input and output names are the same
    force = false;
    if (newFacilityFilePath == facilityFilePath) force = true;

    ifstream origFacilityFile = openInStrm(facilityFilePath);
    ofstream newFacilityFile = openOutStrm(newFacilityFilePath, force);

    //LOOP THRU LINES OF THIS FACILITY FILE
    while (getline(origFacilityFile, facilityLine)) {
      if (facilityLine.find("<CommandAliases>") != string::npos) orig = false;
      if (orig) {
        if (firstDone) newFacilityFile << endl << facilityLine;
        else {
          newFacilityFile << facilityLine;
          firstDone = true;
        }//end if firstDone ... else
      }//end if orig
      if (facilityLine.find("</CommandAliases>") != string::npos) {
        orig = true;
        newFacilityFile << endl << cmdBlock;
      }
    }//END LOOP THRU LINES OF THIS FACILITY FILE

    newFacilityFile.close();

    gzipFile(newFacilityFilePath);
  }//END LOOP THRU FACILITY FILES
}//end addCmds2Facilities

//----------------------------------------------------------------------------
void updateFacilityFiles(
  string const& cmdBlock, string const& posBlock,
  int const& numArgs, char** const& argLst
) {
  string facilityLine;
  bool orig = true, /*force = false,*/ firstDone = false;
  InfoType type = InfoType::NONE;
  //LOOP THRU ADD <CommandAliases>...</CommandAliases> TO EACH FACILITY FILE
  //actually increments facilityIdx += 2 on ea iteration
  for (int facilityIdx = FACILITY_IDX; facilityIdx < numArgs; ++facilityIdx) {
    orig = true;
    firstDone = false;
    //get input file
    path facilityFilePath(argLst[facilityIdx++]);
    stringstream origFacilityFile = ungzip2Strm(facilityFilePath);
    //ofstream newFacilityFile = openOutStrm(newFacilityFilePath, force);
    stringstream newFacilityFile;

    //LOOP THRU LINES OF THIS FACILITY FILE
    while (getline(origFacilityFile, facilityLine)) {
      if (facilityLine.find("<CommandAliases>") != string::npos) {
        //check for bad format
        if (type != InfoType::NONE) {
          status_ += FACILITY_FILE_FORMAT;
          string errMsg = "Error updating facility file: "s + facilityFilePath.string()
            + "\nOriginal facility file invalid format";
          prntNExit(errMsg);
        }//end check for bad format
        orig = false;
        type = InfoType::CMDS;
      }//END IF FOUND CommandAliases entity
      /*
      if (facilityLine.find("<Positions>") != string::npos) {
        //check for bad format
        if (type != InfoType::NONE) {
          status_ += FACILITY_FILE_FORMAT;
          string errMsg = "Error updating facility file: "s + facilityFilePath.string()
                        + "\nOriginal facility file invalid format";
          prntNExit(errMsg);
        }//end check for bad format
        orig = false;
        type = InfoType::POS;
      }//END IF FOUND Positions entity
      */

      if (orig) {
        if (firstDone) newFacilityFile << endl << facilityLine;
        else {
          newFacilityFile << facilityLine;
          firstDone = true;
        }//end if firstDone ... else
      }//end if orig

      if ((type == InfoType::CMDS)&&(facilityLine.find("<CommandAliasesLastImported>") != string::npos)) {
        orig = true;
        type = InfoType::NONE;
        newFacilityFile << endl << cmdBlock;
      }
      /*
      if ((type == InfoType::POS) && (facilityLine.find("</Positions>") != string::npos)) {
        orig = true;
        type = InfoType::NONE;
        newFacilityFile << endl << posBlock;
      }
      */
    }//END LOOP THRU LINES OF THIS FACILITY FILE

    //newFacilityFile.close();
    //gzipFile(newFacilityFilePath);

    path newFacilityFilePath(argLst[facilityIdx]);
    //auto-clobber if input and output names are the same
    if (newFacilityFilePath == facilityFilePath)
      delFilePath(newFacilityFilePath);
    else
      verifyNDelFilePath(newFacilityFilePath);
    gzipStrm(newFacilityFile, newFacilityFilePath);
  }//END LOOP THRU FACILITY FILES
}//end updateFacilityFiles















