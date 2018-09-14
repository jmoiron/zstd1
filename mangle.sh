#!/usr/bin/env bash

for f in *.{c,h,go}; do
    sed -i 's/ZSTD_/ZSTD1_/g' $f
    sed -i 's/ZBUFF_/ZBUFF1_/g' $f
    sed -i 's/HUF_/HUF1_/g' $f
    sed -i 's/FSE_/FSE1_/g' $f
    sed -i 's/divsufsort(/divsufsort1(/g' $f
    sed -i 's/divbwt(/divbwt1(/g' $f
done

for f in *.go; do
    sed -i 's/Zstd/Zstd1/g' $f
    sed -i 's/package zstd/package zstd1/g' $f
done
