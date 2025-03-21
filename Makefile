all: bin/nfs bin/format_nfs

bin/nfs: nfs.cpp
	g++ nfs.cpp -o bin/nfs `pkg-config fuse --cflags --libs`

bin/format_nfs: nfs_format.cpp
	g++ nfs_format.cpp -o bin/format_nfs `pkg-config fuse --cflags --libs`

clean: 
	rm -f bin/nfs bin/format_nfs
