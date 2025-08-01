#ifndef __FAT_FILELIB_H__
#define __FAT_FILELIB_H__

#include "fat_opts.h"
#include "fat_access.h"
#include "fat_list.h"

//-----------------------------------------------------------------------------
// Defines
//-----------------------------------------------------------------------------
#ifndef SEEK_CUR
    #define SEEK_CUR    1
#endif

#ifndef SEEK_END
    #define SEEK_END    2
#endif

#ifndef SEEK_SET
    #define SEEK_SET    0
#endif

#ifndef EOF
    #define EOF         (-1)
#endif

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------
struct sFL_FILE;

struct cluster_lookup
{
    uint32 ClusterIdx;
    uint32 CurrentCluster;
};

typedef struct sFL_FILE
{
    uint32                  parentcluster;
    uint32                  startcluster;
    uint32                  bytenum;
    uint32                  filelength;
    int                     filelength_changed;
    char                    path[FATFS_MAX_LONG_FILENAME];
    char                    filename[FATFS_MAX_LONG_FILENAME];
    uint8                   shortfilename[11];

#ifdef FAT_CLUSTER_CACHE_ENTRIES
    uint32                  cluster_cache_idx[FAT_CLUSTER_CACHE_ENTRIES];
    uint32                  cluster_cache_data[FAT_CLUSTER_CACHE_ENTRIES];
#endif

    // Cluster Lookup
    struct cluster_lookup   last_fat_lookup;

    // Read/Write sector buffer
    uint8                   file_data_sector[FAT_SECTOR_SIZE];
    uint32                  file_data_address;
    int                     file_data_dirty;

    // File fopen flags
    uint8                   flags;
#define FILE_READ           (1 << 0)
#define FILE_WRITE          (1 << 1)
#define FILE_APPEND         (1 << 2)
#define FILE_BINARY         (1 << 3)
#define FILE_ERASE          (1 << 4)
#define FILE_CREATE         (1 << 5)

    struct fat_node         list_node;
} FL_FILE;

//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------

// External
struct fatfs*       fl_new(void);
void                fl_attach_locks(struct fatfs* fs, void (*lock)(void), void (*unlock)(void));
int                 fl_attach_media(struct fatfs* fs, fn_diskio_read rd, fn_diskio_write wr, void* ctx);
void                fl_delete(struct fatfs* fs);

// Standard API
void*               fl_fopen(struct fatfs* fs, const char *path, const char *modifiers);
void                fl_fclose(struct fatfs* fs, void *file);
int                 fl_fflush(struct fatfs* fs, void *file);
int                 fl_fgetc(struct fatfs* fs, void *file);
char *              fl_fgets(struct fatfs* fs, char *s, int n, void *f);
int                 fl_fputc(struct fatfs* fs, int c, void *file);
int                 fl_fputs(struct fatfs* fs, const char * str, void *file);
int                 fl_fwrite(struct fatfs* fs, const void * data, int size, int count, void *file );
int                 fl_fread(struct fatfs* fs, void * data, int size, int count, void *file );
int                 fl_fseek(struct fatfs* fs, void *file , long offset, int origin );
int                 fl_fgetpos(struct fatfs* fs, void *file, uint32* position);
long                fl_ftell(struct fatfs* fs, void *f);
int                 fl_feof(struct fatfs* fs, void *f);
int                 fl_remove(struct fatfs* fs, const char * filename);

// Equivelant dirent.h
typedef struct fs_dir_list_status    FL_DIR;
typedef struct fs_dir_ent            fl_dirent;

FL_DIR*             fl_opendir(struct fatfs* fs, const char* path, FL_DIR *dir);
int                 fl_readdir(struct fatfs* fs, FL_DIR *dirls, fl_dirent *entry);
int                 fl_closedir(struct fatfs* fs, FL_DIR* dir);

// Extensions
void                fl_listdirectory(struct fatfs* fs, const char *path);
int                 fl_createdirectory(struct fatfs* fs, const char *path);
int                 fl_is_dir(struct fatfs* fs, const char *path);

int                 fl_format(struct fatfs* fs, uint32 volume_sectors, const char *name);

#endif
