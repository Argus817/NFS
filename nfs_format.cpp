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
    
    bool free_inode[superblock.inode_count] = {0}; //0 means free, 1 means occupied
    free_inode[0] = 1;
    free_inode[1] = 1;
    free_inode[2] = 1;
    fwrite(&free_inode, sizeof(free_inode), 1, disk);

    bool free_datablock[superblock.datablocks_count] = {0}; //0 means free, 1 means occupied 
    free_datablock[0] = 1;
    free_datablock[1] = 1;
    fwrite(&free_datablock, sizeof(free_datablock), 1, disk);

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
    root_dir.data_index[0] = 1; 
    fwrite(&root_dir, sizeof(Inode), 1, disk);

    Inode new_dir;
    new_dir.id = 1;
    new_dir.mode = S_IFDIR | 0755;
    new_dir.size = DATA_BS;
    new_dir.atime = time(NULL);
    new_dir.mtime = time(NULL);
    new_dir.type = 1;
    strcpy(new_dir.name, "dir1");
    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
        new_dir.data_index[i] = -1;
    new_dir.data_index[0] = 2;
    fwrite(&new_dir, sizeof(Inode), 1, disk);

    Inode file1;
    file1.id = 2;
    file1.mode = S_IFREG | 0644;
    file1.size = 5000;
    file1.atime = time(NULL);
    file1.mtime = time(NULL);
    file1.type = 0;
    strcpy(file1.name, "file1");
    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
        file1.data_index[i] = -1;
    file1.data_index[0] = 0;

    fwrite(&file1, sizeof(Inode), 1, disk);

    fclose(disk);

    char buff[DATA_BS];
    for (int i=0; i<DATA_BS; i++)
        buff[i] = 'a';
    diskWrite(buff, DATA_BS, 1, sizeof(Superblock) + superblock.datablocks_count*sizeof(bool) + superblock.inode_count*sizeof(Inode) );
    diskWrite(buff, DATA_BS, 1, sizeof(Superblock) + superblock.datablocks_count*sizeof(bool) + superblock.inode_count*sizeof(Inode) + DATA_BS);
    

    cout << "Successfull\n" << "Inode count: " << superblock.inode_count << endl;
    cout << "Datablock count: " << superblock.datablocks_count << endl;
    cout << "Total size: " << totalsize << endl;
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
