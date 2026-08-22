#!/usr/bin/env bash

if [ -z "${SOFI_OUTSIDE}" ]
then
    echo "run this script in sofi".
    exit
fi

echo -en "\x00no-custom\x1ffalse\n"
echo -en "\x00data\x1fmonkey do, monkey did\n"
echo -en "\x00use-hot-keys\x1ftrue\n"
echo -en "${SOFI_RETV}\x00icon\x1ffirefox\x1finfo\x1ftest\n"

if [ -n "${SOFI_INFO}" ]
then
    echo "my info: ${SOFI_INFO} "
fi
if [ -n "${SOFI_DATA}" ]
then
    echo "my data: ${SOFI_DATA} "
fi
