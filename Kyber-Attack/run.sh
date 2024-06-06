#!/bin/bash


COUNTER=0
OUT_DIR="./RESULTS"

mkdir -p ${OUT_DIR}

cd ${OUT_DIR}

while true; do
    mkdir ${COUNTER}
    cd ${COUNTER}
    
    sudo taskset 0x1 ../../attack >  attack_out.txt
    sudo killall attack
    
    let COUNTER=COUNTER+1
    cd ..
    
    if [ $COUNTER -gt 10000 ]; then
        break
    fi
done