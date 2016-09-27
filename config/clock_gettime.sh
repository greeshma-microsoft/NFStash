#!/bin/sh

echo "Finding clock_gettime() in libc:"
if `gcc clock_gettime.c`
then
    echo "ok"
    echo "" > clock_gettime.opt
elif `gcc -lrt clock_gettime.c`
then
    echo "Finding clock_gettime() in librt:"
    echo "ok"
    echo "-lrt" > clock_gettime.opt
else
    echo "Couldn't find clock_gettime()!"
    exit 1
fi

# remove the dummy output
rm a.out
