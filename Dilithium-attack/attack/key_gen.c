#include "api.h"
#include <stdio.h>
#include "../dilithium/ref/randombytes.h"
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include "params.h"

/*
Generate keypair and save to files.
*/

void main(void){
    
    uint8_t pk[PUBLICKEY_BYTES];
    uint8_t sk[SECRETKEY_BYTES];    

    printf("Public Key Bytes: %d\n", PUBLICKEY_BYTES);
    printf("Secret Key Bytes: %d\n", SECRETKEY_BYTES);
    pqcrystals_dilithium5_ref_keypair(pk, sk);
    printf("CRYPTO: %d\n", CRYPTO_BYTES);

  

    // save keys to file
    FILE *pk_out = fopen("./keys/pk.bin","w+");
    FILE *sk_out = fopen("./keys/original_sk.bin","w+");
    fwrite(pk, sizeof(uint8_t), PUBLICKEY_BYTES, pk_out);
    fclose(pk_out);

    fwrite(sk, sizeof(uint8_t), SECRETKEY_BYTES, sk_out);
    fclose(sk_out);

  
}