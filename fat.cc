#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <stdio.h>
#include <cstring>

#include "fat_internal.h"
#include "fat.h"

// to hold the BPB of the disk read in
Fat32BPB bpb;

// mmap for the disk image
char *disk;
bool mounted = false;

// vals to hold from BPB
int RootDirSectors = 0;   // count of sectors occupied by root directory
int FATSz = -1;           // cuont of sectors occupied by one FAT
int FirstDataSector = -1; // start of the data region (first sector of cluster 2)
int SectorSize = -1;      // bytes per sector
unsigned int RootCluster= -1;      // first cluster of the root dir
int ClusterSize = -1;     // bytes per cluster
int SecPerClus = -1;
int FirstFATSector = -1;

// file system
struct file{
  int fd = -1;
  unsigned int cluster = 0;
  uint32_t size =  -1;
};

int fdi = 0;                  // variable to use when creating new fds
std::vector<int> fds;         // table to hold whether a fd is open/closed
std::vector<file> files;      // vector to hold file information


/**************************************** HELPER FUNCTIONS *******************************************/


/******************************************************/
/**                  Tokenize path                   **/
/******************************************************/
std::vector<std::string> getPath(const std::string &str)
{
  std::vector<std::string> paf;

  char delim = '/';
  size_t start, end = 0;

  while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
  {
    end = str.find(delim, start);
    paf.push_back(str.substr(start, end - start));
  }

  // some error checking
  for(unsigned long i = 0; i < paf.size(); ++i)
  {
    std::string s = paf[i];
    if(s == ".")
    {
      paf.erase(paf.begin()+i);
      continue;
    }
    if(s == "..")
    {
      if(i == 0)
        paf.erase(paf.begin());
      else
      {
        paf.erase(paf.begin()+i);
        paf.erase(paf.begin()+i-1);
      }
    }
  }

  for(unsigned long i = 0; i < paf.size(); ++i)
  {
    if(paf[i].find(".") != std::string::npos && i != paf.size()-1)
      paf.clear();
  }

  return paf;
}





/*******************************************************************/
/**               Case insensitive string comparison              **/
/*******************************************************************/
bool sameName(const std::string &string1, const std::string &string2)
{
  if(string1.length() != string2.length())
    return false;

  for(unsigned long i = 0; i < string1.length(); ++i)
    if( toupper(string1[i]) != toupper(string2[i]))
      return false;

  return true;
}





/****************************************/
/**          Get FAT entry val         **/
/****************************************/
uint32_t getFATEntry(unsigned int cluster)
{
  int FATOffset = cluster*4;
  int FATSecNum = bpb.BPB_RsvdSecCnt + (FATOffset / SectorSize);
  int FATEntOffset = FATOffset % SectorSize;
  char* FATSec = &disk[FATSecNum*SectorSize];
  return *(uint32_t*)&FATSec[FATEntOffset] & 0x0FFFFFFF;
}





/**************************************/
/**       Get an Entry's Name        **/
/**************************************/
std::string getEntryName(DirEntry entry)
{
  std::string name = std::string(entry.DIR_Name, entry.DIR_Name + 8);
  while(name.back() == ' ') name.pop_back();
  if(entry.DIR_Name[8] != ' ')
  {
    name += '.';
    name += std::string(entry.DIR_Name + 8, entry.DIR_Name + 11);
    while(name.back() == ' ') name.pop_back();
  }

  return name;
}





/****************************************/
/**          Cluster -> Sector         **/
/****************************************/
inline int getSector(unsigned int cluster)
{
  return ( (cluster - 2) * bpb.BPB_SecPerClus) + FirstDataSector;
}





/*************************************/
/**    Read a cluster as a char*    **/
/*************************************/
char* readCluster(unsigned int cluster)
{
  char* clust = &disk[getSector(cluster)*SectorSize];
  if(clust == nullptr)
    printf("ERROR: readCluster failed\n");
  return clust;
}





/*****************************************/
/**      Get path's Directory Entry     **/
/*****************************************/
DirEntry getDirEntry(const std::string &path)
{
  DirEntry entry;
  DirEntry failedEntry;
  failedEntry.DIR_Name[0] = 0xE5;

  unsigned int curCluster = RootCluster; //start at roost cluster
  std::vector<std::string> paf = getPath(path); // parse the path

  if(paf.size() == 0)
    return failedEntry;

  // for every step in our path
  for(unsigned long pathInd = 0; pathInd < paf.size(); pathInd++)
  {
    std::string target = paf[pathInd]; //target directory/file for this step

    // traverse the cluster chain
    for(;;)
    {
      // iterate through the directory entries in the cluster
      DirEntry *clus = (DirEntry*)readCluster(curCluster);
      bool found = 0;
      for(long unsigned i = 0; i < ClusterSize/sizeof(DirEntry); i++)
      {
        entry = clus[i];
        if(entry.DIR_Name[0] == 0x00) break;
        if(entry.DIR_Name[0] == 0xE5) continue;
        std::string name = std::string(entry.DIR_Name, entry.DIR_Name + 8);
        while(name.back() == ' ') name.pop_back();
        if(entry.DIR_Name[8] != ' ')
        {
          name += '.';
          name += std::string(entry.DIR_Name + 8, entry.DIR_Name + 11);
          while(name.back() == ' ') name.pop_back();
        }

        // if this is the entry we are looking for, break
        if(sameName(target, name))
        {
          found = 1;
          break;
        }
      }

      //if we have found, then we need to step into the held directory and continue
      if(found)
      {
        curCluster = entry.DIR_FstClusLO | (entry.DIR_FstClusHI << 16);
        break;
      }

      //if there are no more clusters in the current chain, return failed entry
      if(getFATEntry(curCluster) >= 0x0FFFFFF8)
        return failedEntry;

      // if we havent found, and there are more clusters to travers, then we continue
      curCluster = getFATEntry(curCluster);
    }
  }

  return entry;
}





/*************************************** FAT FUNCTIONS **********************************************/




/*
 * Open the specified FAT disk image and set it up to be used by all subsequent fat_* calls.
 * Only 'path' is a path on the underlyig OS's filesystem instead of a path in the FAT volume
 * Returns true if the disk image is successfully opened and false on any error
 */
bool fat_mount(const std::string &path) {

  // check if disk is already opened
  if(mounted)
  {
    printf("ERROR: Disk already mounted\n");
    return false;
  }

  int disk_fd;
  //actually open the disk as a file
  if((disk_fd = open(path.c_str(), O_RDONLY)) == 0)
  {
    printf("ERROR: failed to open disk as a file\n");
    return false;
  }

  // read full disk into a char*
  struct stat s;
  int status = fstat(disk_fd, &s);
  if(status == -1)
  {
    printf("ERROR: Could not read disk\n");
    return false;
  }
  int volSize = s.st_size;

  // load the full disk into a char (byte) array
  if( (disk = (char*) mmap(NULL, volSize, PROT_READ, MAP_SHARED, disk_fd, 0)) == MAP_FAILED)
  {
    printf("ERROR: failed to mmap disk\n");
    return false;
  }

  //read the BPB into bpb
  bpb = *(Fat32BPB*)disk;

  // Initialize some fields for convenience
  FATSz = bpb.BPB_FATSz32;                                                            
  FirstDataSector = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * FATSz) + RootDirSectors; 
  SectorSize = bpb.BPB_BytsPerSec;                                                   
  RootCluster = bpb.BPB_RootClus;                                                    
  ClusterSize = bpb.BPB_SecPerClus * bpb.BPB_BytsPerSec;                             
  SecPerClus = bpb.BPB_SecPerClus;
  FirstFATSector = bpb.BPB_RsvdSecCnt;

  // BPB successfully loaded, now we can close the file and return
  close(disk_fd);
  mounted = true;
  return true;
}





/*
 * Open the specified file within the filesystem image previously passed to fat_mount.
 * Returns an integer fd that library will accept for other calls
 * On failure, return -1: fails if you ask to open a file that does not exist.
 * Supports opening at least 128 files at a time.
 * Returns an error when a directory is opened.
 */
int fat_open(const std::string &path) {
  if(!mounted)
    return -1;
  
  std::vector<std::string> paf = getPath(path);

  //make sure it's not the root directory
  if(paf.size() == 0)
    return -1;

  DirEntry entry = getDirEntry(path);

  //check if we are trying to open a directory
  if(entry.DIR_Name[0] == 0xE5 || entry.DIR_Attr & DirEntryAttributes::DIRECTORY)
    return -1;


  //else, we are opening a valid file!
  fds.push_back(1); 
  file myfile;
  myfile.fd = fdi++;
  myfile.cluster = (entry.DIR_FstClusLO | (entry.DIR_FstClusHI << 16));
  myfile.size = entry.DIR_FileSize;
  //files.push_back((file){fdi++, (entry.DIR_FstClusLO | (entry.DIR_FstClusHI << 16)), entry.DIR_FileSize});
  files.push_back(myfile);

  return fdi - 1;
}





/*
 * Close the specified file descriptor returned by 'fat_open'
 * Returns 'false' on failure (e.g. file not open)
 */
bool fat_close(int fd) {
  if(!mounted)
    return false;

  //we will never have created an fd >= fds.size()
  if((unsigned long)fd >= fds.size())
    return false;

  //check if file not open
  if(fds[fd] == 0)
    return false;

  fds[fd] = 0;
  return true;
}





/*
 * Copy 'count' bytes starting with byte number 'offset' in the previously opened file into 'buffer'..
 * Returns the number of bytes read, which will be less than 'count' if the caller asks to read past the end-of-file.
 * Returns -1 if the read fails (e.g. 'fd' was not opened). 
 * Reading 0 bytes successfuly, such as when 'count' is 0 or when offset is past the end of file should return 0, not -1
 */
int fat_pread(int fd, void *buffer, int count, int offset) {
  if(!mounted)
    return -1;
  // check if file even exists and that it's open
  if((unsigned long)fd >= fds.size())
    return -1;
  if(fds[fd] == 0)
    return -1;

  //check if we even need to do any hard work
  if((unsigned long)offset >= files[fd].size || count == 0)
    return 0;

  int amountleft = count;
  int amountread = 0;
  int curCluster = files[fd].cluster;

  // iterate through cluster chain, 
  for(;;)
  { 
    // get actual cluster data
    char* data = readCluster(curCluster);
    //char* buf = new char[count];

    // get correct amount to read
    int size = (amountleft < ClusterSize) ? amountleft : ClusterSize;
    std::memcpy(reinterpret_cast<char*>(buffer)+amountread, data, size); //std::strncpy(buf[amountread], data, size);
    amountleft -= size;
    amountread += size;

    // break conditions: are we at end of chain/ have we read the amount
    if(getFATEntry(curCluster) >= 0xFFFFFF8 || amountread == count)
      break;
    else
      curCluster = getFATEntry(curCluster);
  }

  return amountread;
}





/*
 * Return a vector of directory entries for a directory identified by 'path' within the filesystem image passed to fat_mount
 * In the event of an error, returns an empty vector.
 */
std::vector<AnyDirEntry> fat_readdir(const std::string &path) {


  // initialize return vector
  std::vector<AnyDirEntry> entries;
  if(!mounted)
    return entries;

  std::vector<std::string> paf = getPath(path);
  DirEntry mydirentry;

  // if we are not reading the root directory, then get the directory entry of the path
  if(paf.size() > 0)
  {
    mydirentry = getDirEntry(path);
    if(mydirentry.DIR_Name[0] == 0xE5)
      return entries;
  }

  unsigned int curCluster;
  if(paf.size() > 0)
    curCluster =  mydirentry.DIR_FstClusLO | (mydirentry.DIR_FstClusHI << 16);
  else
    curCluster = RootCluster;

  // traverse the cluster chain starting at curCluster
  for(;;)
  {
    DirEntry entry;

    // iterate through the directory entries in the cluster
    DirEntry *clus = (DirEntry*)readCluster(curCluster);
    for(long unsigned i = 0; i < ClusterSize/sizeof(DirEntry); i++)
    {
      entry = clus[i];
      if(entry.DIR_Name[0] == 0x00) break;
      if(entry.DIR_Name[0] == 0xE5) continue;
      AnyDirEntry ent;
      ent.dir = entry;
      entries.push_back(ent);
    }

    // if there are no more clusters in the current chain, return failed entry
    if(getFATEntry(curCluster) >= 0x0FFFFFF8)
      break;

    //there are more clusters to travere
    curCluster = getFATEntry(curCluster);
  }

  return entries;
}
