#ifndef FAT_H_
#define FAT_H_

#include <cstdint>
#include <vector>
#include <string>

/* This struct matches the FAT32 directory entry structure, described on pages 22-24 of 
 * the FAT specification. Use packed struct to copy directly from disk.
 */
struct __attribute__((packed)) DirEntry {
    uint8_t DIR_Name[11];           // short name
    uint8_t DIR_Attr;               // file attribute
    uint8_t DIR_NTRes;              // set value to 0, never change this
    uint8_t DIR_CrtTimeTenth;       // millisecond timestamp for file creation time
    uint16_t DIR_CrtTime;           // time file was created
    uint16_t DIR_CrtDate;           // date file was created
    uint16_t DIR_LstAccDate;        // last access date
    uint16_t DIR_FstClusHI;         // high word of this entry's first cluster number
    uint16_t DIR_WrtTime;           // time of last write
    uint16_t DIR_WrtDate;           // dat eof last write
    uint16_t DIR_FstClusLO;         // low word of this entry's first cluster number
    uint32_t DIR_FileSize;          // file size in bytes
};

/* This has the same size as the DirEntry struct and represent a part of a "long" filename.
 *
 * The DIR_attr and LDIR_Attr are in the same part of the structs and can be used to distinguish
 * which of the two a directory entry is being used for.
 */
struct __attribute__((packed)) LongDirEntry {
    uint8_t LDIR_Ord;
    uint8_t LDIR_Name1[10];
    uint8_t LDIR_Attr;
    uint8_t LDIR_Type;
    uint8_t LDIR_Chksum;
    uint8_t LDIR_Name2[12];
    uint16_t LDIR_FstClusLO;
    uint8_t LDIR_Name3[4];
};

/* Union between "normal" directory entries and "long" directory entries.
 * DIR_Attr, LDIR_Attr fields show type of directory entry
 */
union AnyDirEntry {
    DirEntry dir;
    LongDirEntry ldir;
};

/* Directory entry attributes from page 23 of the FAT specification. 
 * Use case DirEntryAttributes::SYSTEM
 */
enum DirEntryAttributes {
    READ_ONLY       = 0x01,
    HIDDEN          = 0x02,
    SYSTEM          = 0x04,
    VOLUME_ID       = 0x08,
    DIRECTORY       = 0x10,
    ARCHIVE         = 0x20,
    LONG_NAME       = 0x0F,
    LONG_NAME_MASK  = 0x3F,
};

extern bool fat_mount(const std::string &path);
extern int fat_open(const std::string &path);
extern bool fat_close(int fd);
extern int fat_pread(int fd, void *buffer, int count, int offset);
extern std::vector<AnyDirEntry> fat_readdir(const std::string &path);

#endif
