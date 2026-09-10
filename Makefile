CXX      = g++
CXXFLAGS = -O2 `pkg-config fuse --cflags --libs`

all: bin/nfs bin/format_nfs

bin/nfs: nfs.cpp
	$(CXX) $(CXXFLAGS) nfs.cpp -o bin/nfs

bin/format_nfs: nfs_format.cpp
	$(CXX) $(CXXFLAGS) nfs_format.cpp -o bin/format_nfs

clean: 
	rm -f bin/nfs bin/format_nfs
