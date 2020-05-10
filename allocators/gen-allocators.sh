#!/bin/bash

MAKE_OPTIONS="--quiet -j$(nproc)"

./clean-allocators.sh

# IBMmalloc
echo "Compiling IBMmalloc..."
cd IBMmalloc
make ${MAKE_OPTIONS} 2>&1 > /dev/null
if [ $? -eq 0 ]; then
	echo "IBMmalloc compilation succeded!"
else
	echo "IBMmalloc compilation failed!"
fi
cd ..


# TCMalloc
echo "Compiling TCMalloc..."
cd gperftools
./autogen.sh 2>&1 > /dev/null
./configure --prefix=$PWD/build --enable-minimal \
						--enable-shared --enable-libunwind 2>&1 > /dev/null
make install $MAKE_OPTIONS 2>&1 > /dev/null
if [ $? -eq 0 ]; then
	echo "TCMalloc compilation succeded!"
else
	echo "TCMalloc compilation failed!"
fi
cd ..

