#!/bin/bash
rm -f ldext.so luadeploy.so
cd sqlext
./build.sh clean
./build.sh buildext
./build.sh buildfpica
cp ldext.so ..
cd ../luadeploy_module
./build.sh clean
./build.sh build
cp luadeploy.so ..
cd ../luadeploy_app
./build.sh > ../luadeploy
chmod 755 ../luadeploy
cd ..
