#!/bin/sh
cp imageSource.st imageSource
./imageBuilder
mv LittleSmalltalk.image testImage
rm imageSource