# Rowhammer attack on BIKE-KEM binary
Hammering BIKE binary to result in a deterministic key

```
./run.sh
```
^ runs eat_mem and the hammering process

Creates RESULTS directory which will contain a directory for each trial (hammering process exits if no suitable target page found). 
eat_mem runs before each process to evict the binary from memory before each trial.



## Target flips:
Needs at least a single bit flip of any of the following in page 0x1000 of the binary:

- offset 2121 (0x1849):
  - 89 -> 88
 
- offset 2122 (0x184a):
  - df -> de
  - df -> db

This leads to a seed of 0 for crypto_sign_keypair(key generation). A simulation result (by writiing to the binary) is shown in sim_result.txt. h0 and h1 (which make the private key are equal in the simulated result and the attack result)

