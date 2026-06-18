#!/bin/bash
LOGS_FILE=output.valgrind
echo "[!] make sure to run this script from the scripts folder";
CUR_DIR=$(pwd)
cd ../build/linux/x86_64/debug/
DELAY=5s
xmake && echo "[!] please wait for $DELAY" && timeout $DELAY valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./terranova  &> $LOGS_FILE 
echo "[!] report:";
tail -n 8 $LOGS_FILE
cd $CUR_DIR