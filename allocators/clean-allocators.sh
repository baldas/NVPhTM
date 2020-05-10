#!/bin/bash

MAKE_OPTIONS="--quiet"

# IBMmalloc
cd IBMmalloc
make clean ${MAKE_OPTIONS} 2>&1 > /dev/null
cd ..

# TCMalloc
cd gperftools
make clean ${MAKE_OPTIONS} 2>&1 > /dev/null
make mostlyclean ${MAKE_OPTIONS} 2>&1 > /dev/null
make distclean ${MAKE_OPTIONS} 2>&1 > /dev/null
rm -rf build $(cat .gitignore | sed 's|^|./|g')
git checkout src/windows/gperftools/tcmalloc.h
cd ..

