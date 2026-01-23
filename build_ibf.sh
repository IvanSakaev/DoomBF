#! /bin/bash
cd ./bf/industrial-bf
make ibf
cd ../..
if [ ! -d /tmp ]; then
    mkdir ./tmp
fi
mv ./bf/industrial-bf/ibf ./tmp/
