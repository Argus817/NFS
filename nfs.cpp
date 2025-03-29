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
    size_t totalsize;
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
Inode getInode(size_t index);
Inode getInodeByPath(const char *path);
int readDatablock(char *buff, size_t index);
static int nfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int nfs_getattr(const char *path, struct stat *st);
static int nfs_open(const char *path, struct fuse_file_info *fi);
static int nfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);
static int nfs_rename(const char *old_path, const char *new_path);
static int nfs_mkdir(const char *path, mode_t mode);
static int nfs_mknod(const char *path, mode_t mode, dev_t rdev);
static int nfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);
static int nfs_truncate(const char *path, off_t size);
static int nfs_utimens(const char *path, const struct timespec tv[2]);
static int nfs_rmdir(const char *path);
static int nfs_unlink(const char *path);

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
    cout << "Readdir. Path: " << path << endl;
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
    cout << "GetAttr. Path: " << path << endl;
    return 0;
}

static int nfs_open(const char *path, struct fuse_file_info *fi)
{
    Inode file = getInodeByPath(path);
    
    if (file.id == (size_t)(-1))
        return -ENOENT;

    if ((fi->flags & O_WRONLY) && (file.type == 1))
        return -EISDIR; 

    cout << "Open. Path: " << path << endl;
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

    cout << "Read. Path: " << path << " Size: " << size << " Offset: " << offset << endl;
    return bytes_read;
}

static int nfs_rename(const char *old_path, const char *new_path)
{
    cout << old_path << " " << new_path << endl;
    Inode old_file = getInodeByPath(old_path);
    
    if (old_file.id == (size_t)(-1))
        return -ENOENT;

    string parent_path = old_path; 
    size_t last_slash = parent_path.find_last_of('/');
    if (last_slash == string::npos || last_slash == 0)
        parent_path = "/";
    else
        parent_path = parent_path.substr(0, last_slash); 

    Inode old_parent = getInodeByPath(parent_path.c_str());
    
    if (old_parent.id == (size_t)(-1))
        return -ENOENT;
    if (old_parent.type == 0)
        return -ENOTDIR; 

    if (getInodeByPath(new_path).id != (size_t)(-1))
    {
        int stat = nfs_unlink(new_path);
        if (stat != 0)
            return stat;
    }

    parent_path = new_path; 
    last_slash = parent_path.find_last_of('/');  
    if (last_slash == string::npos || last_slash == 0)
        parent_path = "/";
    else
        parent_path = parent_path.substr(0, last_slash); 
    
    Inode new_parent = getInodeByPath(parent_path.c_str());

    if (new_parent.id == (size_t)(-1))
        return -ENOENT;
    if (new_parent.type == 0)
        return -ENOTDIR;
 
    string name = "";
    int i=0;
    while (1)
    {
        if (new_path[i] == '\0')
            break;
        else if (new_path[i] == '/')
            name = "";
        else
            name += new_path[i];
        i++;
    }
    strcpy(old_file.name, name.c_str());
    diskWrite(&old_file, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count+superblock.datablocks_count)*sizeof(bool) + old_file.id*sizeof(Inode));

    if (new_parent.id == old_parent.id)
    {
        cout << "Rename. OldPath: " << old_path << " NewPath: " << new_path << endl;
        return 0;
    }

    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (new_parent.data_index[i] == -1)
        {
            new_parent.data_index[i] = old_file.id;
            break;
        }
        if (i==INODE_DATABLOCK_COUNT-1 && new_parent.data_index[i]!=-1)
            return -ENOSPC;
    }
    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (old_parent.data_index[i] == old_file.id)
        {
            memmove(old_parent.data_index+i, old_parent.data_index+i+1, (INODE_DATABLOCK_COUNT-i-1)*sizeof(ll));
            old_parent.data_index[INODE_DATABLOCK_COUNT-1] = -1;
            break;
        }
    }
    diskWrite(&new_parent, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count+superblock.datablocks_count)*sizeof(bool) + new_parent.id*sizeof(Inode));
    diskWrite(&old_parent, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count+superblock.datablocks_count)*sizeof(bool) + old_parent.id*sizeof(Inode));

    cout << "Rename. OldPath: " << old_path << " NewPath: " << new_path << endl;
    return 0;
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

    bool *free_inode = (bool *)calloc(superblock.inode_count, sizeof(bool));
    diskRead(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock));
    size_t free_id = (size_t)(-1);
    for (size_t i=0; i<superblock.inode_count; i++)
    {
        if (free_inode[i] == 0)
        {
            free_id = (size_t)i;
            break;
        }
    } 

    if (free_id == (size_t)(-1)) {
        free(free_inode);        
        return -ENOSPC; 
    }

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
            diskWrite(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock)); 
            free(free_inode);
            diskWrite(&newdir, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + newdir.id*sizeof(Inode));

            cout << "Mkdir. Path: " << path << endl;
            return 0;
        }
    } 

    free(free_inode);
    return -ENOSPC;
}

static int nfs_mknod(const char *path, mode_t mode, dev_t rdev)
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

    bool *free_inode = (bool *)calloc(superblock.inode_count, sizeof(bool));
    diskRead(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock));
    size_t free_id = (size_t)(-1);
    for (size_t i=0; i<superblock.inode_count; i++)
    {
        if (free_inode[i] == 0)
        {
            free_id = (size_t)i;
            break;
        }
    } 

    if (free_id == (size_t)(-1)) {
        free(free_inode);    
        return -ENOSPC;
    }

    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (parent.data_index[i] == -1)
        {
            Inode newfile;
            newfile.id = free_id;
            newfile.mode = mode | S_IFREG;
            newfile.size = 0;
            newfile.atime = time(NULL);
            newfile.mtime = time(NULL);
            newfile.type = 0;
            strncpy(newfile.name, path + last_slash + 1, sizeof(newfile.name) - 1);
            for (int i = 0; i < INODE_DATABLOCK_COUNT; i++)
                newfile.data_index[i] = -1;

            parent.data_index[i] = newfile.id;
            parent.mtime = time(NULL);

            diskWrite(&parent, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + parent.id*sizeof(Inode));
            free_inode[free_id] = 1;
            diskWrite(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock)); 
            free(free_inode);
            diskWrite(&newfile, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + newfile.id*sizeof(Inode));

            cout << "Mknod. Path: " << path << endl;
            return 0;
        }
    } 

    free(free_inode);
    return -ENOSPC;
}

static int nfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    Inode file = getInodeByPath(path);
    if (file.id == (size_t)(-1))
        return -ENOENT;

    if (file.type == 1)
        return -EISDIR;

    size_t start_block = offset / DATA_BS;
    size_t block_offset = offset % DATA_BS;
    size_t bytes_written = 0;

    bool *free_blocks = (bool *)calloc(superblock.datablocks_count, sizeof(bool));
    diskRead(free_blocks, sizeof(bool), superblock.datablocks_count, sizeof(Superblock) + superblock.inode_count * sizeof(bool));

    while (bytes_written < size)
    {
        if (file.data_index[start_block] == -1)
        {
            size_t free_block = (size_t)(-1);
            for (size_t i = 0; i < superblock.datablocks_count; i++)
            {
                if (!free_blocks[i])
                {
                    free_block = i;
                    free_blocks[i] = 1;
                    break;
                }
            }

            if (free_block == (size_t)(-1)) {
                free(free_blocks);            
                return -ENOSPC;
            }

            file.data_index[start_block] = free_block;
        }

        size_t chunk_size = min(DATA_BS - block_offset, size - bytes_written);

        char temp[DATA_BS] = {0};
        if (block_offset > 0 || chunk_size < DATA_BS)
            diskRead(temp, DATA_BS, 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + superblock.inode_count * sizeof(Inode) + file.data_index[start_block] * DATA_BS);

        memmove(temp + block_offset, buffer + bytes_written, chunk_size);
        diskWrite(temp, DATA_BS, 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + superblock.inode_count * sizeof(Inode) + file.data_index[start_block] * DATA_BS);

        bytes_written += chunk_size;
        block_offset = 0;
        start_block++;
    }

    file.size = max(file.size, offset + bytes_written);
    file.mtime = time(NULL);
    diskWrite(&file, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + file.id * sizeof(Inode));

    diskWrite(free_blocks, sizeof(bool), superblock.datablocks_count, sizeof(Superblock) + superblock.inode_count * sizeof(bool));
    free(free_blocks);

    cout << "Write: Path: " << path << " Size: " << size << " Offset: " << offset << endl;
    return bytes_written;
}

static int nfs_truncate(const char *path, off_t size)
{
    Inode file = getInodeByPath(path);
    if (file.id == (size_t)(-1))
        return -ENOENT; 

    if (file.type == 1)  
        return -EISDIR;  

    if (size == 0) 
    {
        bool *free_datablock = (bool *)calloc(superblock.datablocks_count, sizeof(bool));
        diskRead(free_datablock, sizeof(bool), superblock.datablocks_count, sizeof(Superblock)+superblock.inode_count*sizeof(bool));
        for (size_t i = 0; i < (file.size + DATA_BS - 1) / DATA_BS; i++)
        {
            if (file.data_index[i] != (size_t)-1)
            {
                free_datablock[file.data_index[i]] = 0;   
                memmove(file.data_index+i, file.data_index+i+1, (INODE_DATABLOCK_COUNT-i-1)*sizeof(ll));
                file.data_index[INODE_DATABLOCK_COUNT-1] = -1;
            }
        }
        diskWrite(free_datablock, sizeof(bool), superblock.datablocks_count, sizeof(Superblock)+superblock.inode_count*sizeof(bool));
        free(free_datablock);
    }

    file.size = size;
    file.mtime = time(NULL);

    
    diskWrite(&file, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + file.id * sizeof(Inode));

    cout << "Truncate: Path: " << path << " Size: " << size << endl;
    return 0;
}


static int nfs_utimens(const char *path, const struct timespec tv[2]) 
{
    Inode file = getInodeByPath(path);
    if (file.id == (size_t)(-1))
        return -ENOENT;

    file.atime = tv[0].tv_sec;
    file.mtime = tv[1].tv_sec;
 
    diskWrite(&file, sizeof(Inode), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count) * sizeof(bool) + file.id * sizeof(Inode));

    cout << "Utimens. Path: " << path << endl;
    return 0;
}

static int nfs_rmdir(const char *path)
{
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

    Inode dir = getInodeByPath(path);
    if (dir.id == (size_t)(-1))
        return -ENOENT;

    if (dir.type == 0)
        return -ENOTDIR;

    if (dir.data_index[0] != -1)
        return -ENOTEMPTY;

    bool *free_inode = (bool *)calloc(superblock.inode_count, sizeof(bool));
    diskRead(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock));

    free_inode[dir.id] = 0;
    diskWrite(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock));
    free(free_inode);

    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (parent.data_index[i] == dir.id)
        {
            memmove(parent.data_index+i, parent.data_index+i+1, (INODE_DATABLOCK_COUNT-i-1)*sizeof(ll));
            parent.data_index[INODE_DATABLOCK_COUNT-1] = -1;
            break;
        }
    }
    diskWrite(&parent, sizeof(parent), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count)*sizeof(bool) + parent.id*sizeof(Inode));

    cout << "Rmdir. Path: " << path << endl;
    return 0;
}

static int nfs_unlink(const char *path)
{
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

    Inode file = getInodeByPath(path);

    if (file.id == (size_t)(-1))
        return -ENOENT;

    if (file.type == 1)
        return -EISDIR;

    bool *free_inode = (bool *)calloc(superblock.inode_count, sizeof(bool));
    diskRead(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock));
    bool *free_datablock = (bool *)calloc(superblock.datablocks_count, sizeof(bool));
    diskRead(free_datablock, sizeof(bool), superblock.datablocks_count, sizeof(Superblock)+superblock.inode_count*sizeof(bool));

    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (parent.data_index[i] == file.id)
        {
            memmove(parent.data_index+i, parent.data_index+i+1, (INODE_DATABLOCK_COUNT-i-1)*sizeof(ll));
            parent.data_index[INODE_DATABLOCK_COUNT-1] = -1;
            break;
        }
    }

    for (int i=0; i<INODE_DATABLOCK_COUNT; i++)
    {
        if (file.data_index[i] == -1)
            break;
        free_datablock[file.data_index[i]] = 0;
    }

    free_inode[file.id] = 0;
    diskWrite(free_inode, sizeof(bool), superblock.inode_count, sizeof(Superblock));
    diskWrite(free_datablock, sizeof(bool), superblock.datablocks_count, sizeof(Superblock)+superblock.inode_count*sizeof(bool));
    diskWrite(&parent, sizeof(parent), 1, sizeof(Superblock) + (superblock.inode_count + superblock.datablocks_count)*sizeof(bool) + parent.id*sizeof(Inode));

    free(free_inode);
    free(free_datablock);    

    cout << "Unlink. Path: " << path << endl;
    return 0;
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
    operations.rename = nfs_rename;
    operations.mkdir = nfs_mkdir;
    operations.mknod = nfs_mknod;
    operations.write = nfs_write;
    operations.open = nfs_open;
    operations.truncate = nfs_truncate;
    operations.rmdir = nfs_rmdir;
    operations.unlink = nfs_unlink;
    operations.utimens = nfs_utimens;
    return fuse_main( argc, argv, &operations, NULL );
}
