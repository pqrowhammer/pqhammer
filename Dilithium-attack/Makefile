CC=gcc
LDFLAGS=-L./dilithium/ref 
LIBS = -lpqcrystals_dilithium5_ref -lpqcrystals_fips202_ref


# gcc key_gen.c ../dilithium/ref/randombytes.c -o key_gen -L../dilithium/ref -lpqcrystals_dilithium5_ref -lpqcrystals_fips202_ref

key_gen: ./attack/key_gen.c ./dilithium/ref/randombytes.c
	$(CC) -o key_gen $^ $(LDFLAGS) $(LIBS)

gen_msgs: ./attack/generate_msgs.c ./dilithium/ref/randombytes.c
	$(CC) -o gen_msgs $^ 

server: ./attack/signing_server.c ./dilithium/ref/randombytes.c
	$(CC) -g -o server $^ $(LDFLAGS) $(LIBS)

verify: ./attack/verify_sig.c ./dilithium/ref/randombytes.c
	$(CC) -o verify $^ $(LDFLAGS) $(LIBS)

hammer: ./attack/hammer-dil.c
	$(CC) -o hammer $^

recover: ./attack/recover.c ./dilithium/ref/randombytes.c ./dilithium/ref/poly.c ./dilithium/ref/polyvec.c ./dilithium/ref/packing.c ./dilithium/ref/reduce.c ./dilithium/ref/ntt.c ./dilithium/ref/rounding.c ./dilithium/ref/symmetric-shake.c ./dilithium/ref/fips202.c 
	$(CC) -DDILITHIUM_MODE=5  -o recover $^ $(LDFLAGS) -lpqcrystals_dilithium5_ref


.PHONY: clean

clean:
	rm key_gen
	rm server
	rm gen_msgs
	rm verify
	rm recover
	rm hammer
	

.PHONY: clean_files

clean_data:
	rm messages/*
	rm keys/*
	rm sigs/*
