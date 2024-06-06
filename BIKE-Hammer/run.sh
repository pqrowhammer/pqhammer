#!/bin/bash


COUNTER=0
OUT_DIR="./RESULTS"

mkdir -p ${OUT_DIR}

cd ${OUT_DIR}

while true; do
    mkdir ${COUNTER}
    cd ${COUNTER}
    
    
    ../../attack >  bike_attack.txt
    if [ -f ./bike_out.txt ]; then
        ../../eat_mem
    fi
     
    
    let COUNTER=COUNTER+1
    cd ..
    
    if [ $COUNTER -gt 10000 ]; then
        break
    fi
done