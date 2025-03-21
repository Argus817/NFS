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

void diskRead(void *buff, size_t size, size_t count, long offset);
void diskWrite(void *buff, size_t size, size_t count, long offset);

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

Inode getInode(size_t index)
{
    long offset = sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count)*sizeof(bool) + index*sizeof(Inode);
    Inode target;
    diskRead(&target, sizeof(Inode), 1, offset);
    return target;
}

Inode getInodeByPath(const char *path)
{
    if (strcmp(path, "/")==0)
        return getInode(0);

    int i=1;
    size_t currdir_index=0;
    string name;
    while (1)
    {
        if (path[i] == '/' or path[i] == '\0')
        { 
            Inode currdir = getInode(currdir_index);
            if (currdir.type == 0 || currdir.data_index[0] == -1)
                return { .id = (size_t)(-1) };

            bool found = false;
            for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
            {
                if (currdir.data_index[i]==-1)
                    break;
                Inode entry = getInode(currdir.data_index[i]);
                string entryname = entry.name;
                if (name==entryname) 
                {
                    currdir_index = currdir.data_index[i];
                    found = true;
                    break;
                }
            }

            if (!found)
                return { .id = (size_t)(-1) };

            if (path[i] == '/') 
            {
                i++;
                name = "";
                continue;
            }
            else
                break;
        }
        
        name += path[i];
        i++;
    }
    return getInode(currdir_index);
}

int readDatablock(char *buff, size_t index)
{
    size_t offset = sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count)*sizeof(bool) + superblock.inode_count*sizeof(Inode) + index*DATA_BS;
    diskRead(buff, DATA_BS, 1, offset);
    return DATA_BS;
}

static int nfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);
    Inode dir = getInodeByPath(path);
    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (dir.data_index[i] == -1)
            break;

        filler(buffer, getInode(dir.data_index[i]).name, NULL, 0);
    }
    return 0;
}

static int nfs_getattr(const char *path, struct stat *st)
{
    Inode entry = getInodeByPath(path);

    if (entry.id == (size_t)(-1))
        return -ENOENT;
    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = entry.atime;
    st->st_mtime = entry.mtime;
    st->st_mode = entry.mode;
    st->st_size = entry.size; 
    return 0;
}

static int nfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    Inode file = getInodeByPath(path);
    if (file.id == (size_t)(-1))
        return -ENOENT;

    if (file.type==1)
        return -EISDIR;

    if (offset >= file.size)
        return 0;

    size_t to_read = min(size, file.size - offset);  
    size_t ind = offset / DATA_BS;  
    size_t block_offset = offset % DATA_BS;  

    char temp[DATA_BS];  
    size_t bytes_read = 0;

    while (bytes_read < to_read)
    {
        if (readDatablock(temp, file.data_index[ind]) < 0)  
            return -EIO;  

        size_t chunk_size = min(DATA_BS - block_offset, to_read - bytes_read); 
        memmove(buffer + bytes_read, temp + block_offset, chunk_size);

        bytes_read += chunk_size;
        block_offset = 0;  
        ind++;  
    }

    return bytes_read;
}

static int nfs_mkdir(const char *path, mode_t mode)
{
    if (getInodeByPath(path).id != (size_t)(-1))
        return -EEXIST;

    string parent_path = path; 
    size_t last_slash = parent_path.find_last_of('/');
    if (last_slash == string::npos || last_slash == 0)
        parent_path = "/";
    else
        parent_path = parent_path.substr(0, last_slash); 

    Inode parent = getInodeByPath(parent_path.c_str());

    if (parent.id == (size_t)(-1))
        return -ENOENT;

    if (parent.type == 0)
        return -ENOTDIR;

    bool free_inode[superblock.inode_count] = {0};
    diskRead(&free_inode, sizeof(free_inode), 1, sizeof(Superblock));
    size_t free_id = (size_t)(-1);
    for (size_t i=0; i<superblock.inode_count; i++)
    {
        if (free_inode[i] == 0)
        {
            free_id = (size_t)i;
            break;
        }
    } 

    if (free_id == (size_t)(-1))
        return -ENOSPC; 

    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (parent.data_index[i] == -1)
        {
            Inode newdir;
            newdir.id = free_id;
            newdir.mode = mode | S_IFDIR;
            newdir.size = DATA_BS;
            newdir.atime = time(NULL);
            newdir.mtime = time(NULL);
            newdir.type = 1;
            strncpy(newdir.name, path + last_slash + 1, sizeof(newdir.name) - 1);
            for (int i = 0; i < INODE_DATABLOCK_COUNT; i++)
                newdir.data_index[i] = -1;

            parent.data_index[i] = newdir.id;
            parent.mtime = time(NULL);

            diskWrite(&parent, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + parent.id*sizeof(Inode));
            free_inode[free_id] = 1;
            diskWrite(&free_inode, sizeof(free_inode), 1, sizeof(Superblock)); 
            
            diskWrite(&newdir, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + newdir.id*sizeof(Inode));

            return 0;
        }
    } 

    return -ENOSPC;
}

void init()
{
    diskRead(&superblock, sizeof(Superblock), 1, 0);
    if (superblock.sig[0]!='3' || superblock.sig[1]!='N' || superblock.sig[2]!='F' || superblock.sig[3]!='S')
    {
        cerr << "NFS signature doesn't match\n";
        exit(1);
    } 
}

int main(int argc, char *argv[])
{
    init();
    
    static struct fuse_operations operations = {};
    operations.getattr = nfs_getattr;
    operations.readdir = nfs_readdir;
    operations.read = nfs_read;
    operations.mkdir = nfs_mkdir;
    return fuse_main( argc, argv, &operations, NULL );
    cout << getInodeByPath("/dir1/abc").name << endl;
    return 0;
}
