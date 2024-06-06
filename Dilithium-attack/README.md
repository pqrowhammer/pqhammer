# Rowhammer Attack on Dilithium
Flipping rho

## Setup
Signing server listens and generates signatures

    mkdir sigs
    mkdir messages
    mkdir keys
    export LD_LIBRARY_PATH=./dilithium/ref
    make key_gen
    make messages
    make sigs
    make hammer

    ./key_gen

    // start server in separte terminal
    ./server 

    // this will save the messages and the server will save signatures in sigs (move these correct signatures to another dir)
    ./gen_msgs 

    // start server in separte terminal
    taskset 0x4 ./server

    ./run_hammer 

./run_hammer will run the hammering process repeatedly. When a suitable page is found the hammering process will proceed to connecting with the server and sending the mesages. Server will terminate after it receives a message and signs them.


## Result
Flipped 1 bit in rho. Below is the diff between the original sk and the hammered sk. 
```
diff hmrd.hex og.hex
```
```
1c1
< 00000000: dc4d c22d b2d2 ff0a 3fed d2db 1190 b286  .M.-....?.......
---
> 00000000: dc4d c22d b2d2 ff08 3fed d2db 1190 b286  .M.-....?.......
```
