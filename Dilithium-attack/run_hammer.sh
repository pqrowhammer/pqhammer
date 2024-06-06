#!/bin/bash


COUNTER=0




while true; do

 

    taskset 0x4 ./hammer > ./hammer_out/${COUNTER}.txt
    let COUNTER=COUNTER+1
    
    
    if [ $COUNTER -gt 10000 ]; then
        break
    fi
done