#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <filesystem>
#include <bits/stdc++.h>
#include <time.h>
using namespace std;

#define ll long long int
#define DATA_BS 4096
#define INODE_BS 256
#define MEM_PER_INODE 16384
#define INODE_DATABLOCK_COUNT ((INODE_BS - (sizeof(size_t) * 3 + 2*sizeof(time_t) + sizeof(bool) + 50 + 5)) / sizeof(long long))

struct Superblock 
{
    char sig[4];
    size_t inode_count;
    size_t datablocks_count;
};

struct Inode
{ 
    size_t id;
    size_t mode;
    size_t size;
    time_t atime;
    time_t mtime;
    bool type; //0 for file 1 for dir
    char name[55];
    ll data_index[INODE_DATABLOCK_COUNT];

    bool operator==(const Inode &other) const
    {
        return id == other.id &&
               mode == other.mode &&
               size == other.size &&
               atime == other.atime &&
               mtime == other.mtime &&
               type == other.type &&
               strcmp(name, other.name) == 0 &&
               memcmp(data_index, other.data_index, sizeof(data_index)) == 0;
    }
};

const char diskfile[] = "image.iso";
Superblock superblock;

void diskRead(void *buff, size_t size, size_t count, long offset)
{
    FILE *disk = fopen(diskfile, "r+b");
    if (!disk)
    {
        cerr << "Unexpected error in opening disk\n";
        exit(1);
    }
    fseek(disk, offset, SEEK_SET);
    fread(buff, size, count, disk);
    fclose(disk);
}

void diskWrite(void *buff, size_t size, size_t count, long offset)
{
    FILE *disk = fopen(diskfile, "r+b");
    if (!disk)
    {
        cerr << "Unexpected error in opening disk\n";
        exit(1);
    }
    fseek(disk, offset, SEEK_SET);
    fwrite(buff, size, count, disk);
    fclose(disk);
}

void init()
{
    strcpy(superblock.sig, "3NFS");
    size_t totalsize = filesystem::file_size(diskfile);
    if (filesystem::file_size(diskfile) <= DATA_BS+INODE_BS+sizeof(Superblock))
    {
        cerr << "Size of image.iso is very small\n";
        exit(1);
    }
    superblock.inode_count = totalsize/MEM_PER_INODE;
    superblock.datablocks_count = (totalsize - sizeof(Superblock) - superblock.inode_count*sizeof(Inode)) / (DATA_BS + sizeof(bool));
    FILE *disk = fopen(diskfile, "r+b");
    fwrite(&superblock, sizeof(Superblock), 1, disk);
    
    bool *free_inode = (bool *)calloc(superblock.inode_count, sizeof(bool)); //0 means free, 1 means occupied
    free_inode[0] = 1;
    free_inode[1] = 1;
    free_inode[2] = 1;
    fwrite(free_inode, sizeof(bool), superblock.inode_count, disk);
    free(free_inode);

    bool *free_datablock = (bool *)calloc(superblock.datablocks_count, sizeof(bool)); //0 means free, 1 means occupied 
    free_datablock[0] = 1;
    free_datablock[1] = 1;
    fwrite(free_datablock, sizeof(bool), superblock.datablocks_count, disk);
    free(free_datablock);

    Inode root_dir;
    root_dir.id = 0;
    root_dir.mode = S_IFDIR | 0755;
    root_dir.size = DATA_BS;
    root_dir.atime = time(NULL);
    root_dir.mtime = time(NULL);
    root_dir.type = 1;
    strcpy(root_dir.name, "/");
    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
        root_dir.data_index[i] = -1;
     
    fwrite(&root_dir, sizeof(Inode), 1, disk);
    
    cout << "Successfull\n" << "Inode count: " << superblock.inode_count << endl;
    cout << "Datablock count: " << superblock.datablocks_count << endl;
    cout << "Total size: " << totalsize << endl;
    fclose(disk);
}

int main(int argc, char **argv)
{
    cout << "Confirm formatting image.iso (y/N): ";
    char x;
    cin >> x;
    if (x=='y' || x=='Y')
        init(); 
    return 0;
}
