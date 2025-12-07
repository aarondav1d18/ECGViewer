currentDirectory=${PWD}
mkdir -p build/
cd build/
cmake ..
make
cd $currentDirectory
