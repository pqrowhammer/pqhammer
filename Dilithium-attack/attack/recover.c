#include "api.h"
#include "params.h"
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "../dilithium/ref/poly.h"
#include "../dilithium/ref/polyvec.h"
#include "../dilithium/ref/packing.h"



// Integer inverse. Taken from https://github.com/tbuktu/libntru/blob/master/src/poly.c

// Calculates the multiplicative inverse of a mod modulus
//   using the extended Euclidean algorithm. 
//   return x = a^(-1) mod modulus 
int inv_integer(int32_t a, int32_t modulus) {
    int x = 0;
    int lastx = 1;
    int y = 1;
    int lasty = 0;
    int b = modulus;
    while (b != 0) {
        int quotient = a / b;

        int temp = a;
        a = b;
        b = temp % b;

        temp = x;
        x = lastx - quotient*x;
        lastx = temp;

        temp = y;
        y = lasty - quotient*y;
        lasty = temp;
    }
    if (lastx < 0)
        lastx += modulus;
    return lastx;
}


int32_t modInverse(int32_t A, int32_t M)
{
    for (int32_t X = 1; X < M; X++)
        if (((A % M) * (X % M)) % M == 1)
            return X;
}
 
////////////////////////////// Helper functions for doing polynomial division //////////////////////////////
// Initialize a polynomial
int Init_poly(poly* a){
  for(int i = N-1; i >= 0; i--){
    a->coeffs[i]=0;
  }
}

// copy a polynomial
int Copy_poly(poly* dst, poly* src){
  for(int i = N-1; i >= 0; i--){
    dst->coeffs[i]=src->coeffs[i];
  }
}

// test if a polynomial is zero
int test_poly_zero(poly* a){
  for(int j=0; j<N; j++){
    if(a->coeffs[j]!=0){
      return 0;
    }
  }
  return 1;
}

// make a poly positive
void positive_poly(poly* a){
  for(int j=0; j<N; j++){
    if(a->coeffs[j]<0){
      a->coeffs[j] = a->coeffs[j] + Q;
    }
  }
}


// print polynomial
int Print_poly(poly* a){
  printf("polynomial is: \n");
  for(int j=0; j<N; j++){
    printf(" %d", a->coeffs[j]);
  }
  printf("\n");
}

// print non-zero polynomial
int Print_hamming(poly* a){
  printf("The index of bits turned on in this polynomial is: \n");
  for(int j=0; j<N; j++){
    if(a->coeffs[j]!=0){
      printf("%d ",j);
    }
  }
  printf("\n");
}

// Return degree of a polynomial a
int get_degree(poly* a){
  int degree_a = 0;
  for(int i = N-1; i >=0; i--){
    if(a->coeffs[i]!=0){
      degree_a = i;
      break;
    }
  }
  return degree_a;
}

///////////////////// Big polynomial for ring Zq[x] /////////////////
#define bigN 3*256

typedef struct {
  int32_t coeffs[bigN];
} bigpoly;

int Init_bigpoly(bigpoly* a){
  for(int i = 0; i < bigN; i++){
    a->coeffs[i]=0;
  }
}

// copy a big polynomial to another big polynomial
int Copy_bigpoly(bigpoly* dst, bigpoly* src){
  for(int i = 0; i < bigN; i++){
    dst->coeffs[i]=src->coeffs[i];
  }
}



// test if a big polynomial is zero
int test_bigpoly_zero(bigpoly* a){
  for(int j=0; j<bigN; j++){
    if(a->coeffs[j]!=0){
      return 0;
    }
  }
  return 1;
}

// test if a big polynomial is one
int test_bigpoly_one(bigpoly* a){
  if(a->coeffs[0]!=1){
    return 0;
  }
  for(int j=1; j<bigN; j++){
    if(a->coeffs[j]!=0){
      return 0;
    }
  }
  return 1;
}

// print a big polynomial
int Print_bigpoly(bigpoly* a){
  printf("polynomial is: \n");
  for(int j=0; j<bigN; j++){
    printf(" %d", a->coeffs[j]);
  }
  printf("\n");
}

// print non-zero big polynomial
int Print_big_hamming(bigpoly* a){
  printf("The index of bits turned on in this big polynomial is: \n");
  for(int j=0; j<bigN; j++){
    if(a->coeffs[j]!=0){
      printf("%d ",j);
    }
  }
  printf("\n");
}

// Return degree of a big polynomial a
int get_big_degree(bigpoly* a){
  int degree_a = 0;
  for(int i = bigN-1; i >=0; i--){
    if(a->coeffs[i]!=0){
      degree_a = i;
      break;
    }
  }
  return degree_a;
}


// big polynomial multiplication in Zq[x]
// x = a*b in Zq[x], old school O(n*n) style, nothing fancy
void big_poly_mul(bigpoly* x, bigpoly* a, bigpoly* b){
  int degree_a = get_big_degree(a);
  int degree_b = get_big_degree(b);

  for(int i=0; i<=degree_a; i++){ // loop a
    for(int j=0; j<=degree_b; j++){ // loop b
      if((i+j) >= bigN){
        printf("multiplication outof range %d\n", (i+j));
        exit(0);
      }
      int64_t curr_coeff = (int64_t)x->coeffs[i+j] + (int64_t)a->coeffs[i]*(int64_t)b->coeffs[j];
      curr_coeff = curr_coeff % (int64_t)Q;
      x->coeffs[i+j] = (int32_t)curr_coeff;
    }
  }
}

// big polynomial addition in Zq[x]
// x = a+b in Zq[x]
void big_poly_add(bigpoly* x, bigpoly* a, bigpoly* b){
  for(int i=0; i<bigN; i++){ 
      int64_t curr_coeff =  (int64_t)a->coeffs[i]+(int64_t)b->coeffs[i];
      curr_coeff = curr_coeff % Q;
      x->coeffs[i] = (int32_t)curr_coeff;
  }
}

// big polynomial subtraction in Zq[x]
// x = a-b in Zq[x]
void big_poly_sub(bigpoly* x, bigpoly* a, bigpoly* b){
  for(int i=0; i<bigN; i++){ 
      int64_t curr_coeff =  (int64_t)a->coeffs[i]-(int64_t)b->coeffs[i];
      while(curr_coeff<0){
        curr_coeff = curr_coeff + Q;
      }
      curr_coeff = curr_coeff % Q;
      x->coeffs[i] = (int32_t)curr_coeff;
  }
}


// copy a big polynomial to a polynomial
int Copy_bigpoly_to_poly(poly* dst, bigpoly* src){
  for(int i = 0; i < N; i++){
    dst->coeffs[i]=src->coeffs[i];
  }
}

// copy a polynomial to a big polynomial
int Copy_poly_to_bigpoly(bigpoly* dst, poly* src){
  for(int i = 0; i < N; i++){
    dst->coeffs[i]=src->coeffs[i];
  }
}


// Polynomial division. Following https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=4800404
// output q and r such that a = q*b + r
void poly_div(bigpoly* a, bigpoly* b, bigpoly* remainder, bigpoly* quotient){

  bigpoly r;
  Init_bigpoly(&r);
  Copy_bigpoly(&r, a);

  bigpoly q;
  Init_bigpoly(&q);

  // u = b[degree_b] ^ -1
  int64_t u = 0;
  int degree_b = get_big_degree(b);
  u = inv_integer(b->coeffs[degree_b], Q);

  // Get degree of r
  int d = get_big_degree(&r);
  
  while(d>=degree_b){
    
    // v = u*r_d*X^{d-degree_b}
    bigpoly v;
    Init_bigpoly(&v);
    // r_d = (r_d * u ) % Q
    int64_t r_d = r.coeffs[d]; 
    r_d = (r_d * u) % (int64_t)Q;
    int d_minus_N = d - degree_b;
    v.coeffs[d_minus_N] = (int32_t)r_d;
    
    // q := q + v
    big_poly_add(&q, &q, &v);

    // r := r – v × b
    bigpoly vb;
    Init_bigpoly(&vb);
    big_poly_mul(&vb, &v, b);

    big_poly_sub(&r, &r, &vb);   

    // Update d
    d = get_big_degree(&r);
    if(test_bigpoly_zero(&r)){
      break;
    }
  }

  Copy_bigpoly(remainder, &r);
  Copy_bigpoly(quotient, &q);

}

/*
// Polynomial Extended Euclidean Algorithm. Following https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=4800404
// Input a, b
// Output u,v,d, with d = GCD(a, b) and a × u + b × v = d.
int extended_gcd(bigpoly* a, bigpoly* b, bigpoly* u, bigpoly* v, bigpoly* d){
  if(test_bigpoly_zero(b)){
    return -1;
  }
  // u := 1
  u->coeffs[0]=1;
  // d := a
  Copy_bigpoly(d, a);
  
  bigpoly v1, v3, t1;
  // v1 := 0
  Init_bigpoly(&v1);
  Init_bigpoly(&t1);
  // v3 := b
  Copy_bigpoly(&v3, b);

  while(!test_bigpoly_zero(&v3)){ // v3!=0
    printf("v3 is \n");
    Print_bigpoly(&v3);
    Print_big_hamming(&v3);

    // poly_div: d = v3 × q + t3 with deg t3 < deg v; a = d,  b= v3
    // q is the quotient, t3 is the remainder
    bigpoly q, t3;
    Init_bigpoly(&q);
    Init_bigpoly(&t3);
    poly_div(d, &v3, &t3, &q);

    int degree_t3 = get_big_degree(&t3);
    int degree_v3 = get_big_degree(&v3);
    //printf("degree_t3 v3 is %d %d\n", degree_t3, degree_v3);

    // t1 := u – q × v1
    big_poly_mul(&t1, &q, &v1);
    big_poly_sub(&t1, u, &t1);   

    // u := v1
    Copy_bigpoly(u, &v1);

    // d := v3
    Copy_bigpoly(d, &v3);

    // v1 := t1
    Copy_bigpoly(&v1, &t1);

    // v3 := t3
    Copy_bigpoly(&v3, &t3);
  }

  // v := (d – a × u)/b

  // av = d-a*u
  bigpoly av;
  Init_bigpoly(&av);
  big_poly_mul(&av, a, u);
  big_poly_sub(&av, d, &av);  // v = d-a*u 

  // v = av/b
  // poly_div: av =  v*b + r
  // v is the quotient, r is the remainder (should be 0)
  bigpoly r;
  Init_bigpoly(&r);
  poly_div(&av, b, &r, v);

  printf("r is zero %d\n", test_bigpoly_zero(&r));
}
*/

// as + bt = d, deg a >= deg b
int eGCD(bigpoly* a, bigpoly* b, bigpoly* s, bigpoly* t, bigpoly* d){
  bigpoly oldr, r, olds, oldt;

  // oldr = a, r = b
  Copy_bigpoly(&oldr, a);
  Copy_bigpoly(&r, b);
  // olds = 1, s = 0
  Init_bigpoly(&olds);
  olds.coeffs[0]=1;
  Init_bigpoly(s);
  // oldt = 0, t = 1
  Init_bigpoly(&oldt);
  Init_bigpoly(t);
  t->coeffs[0]=1;


  while(!test_bigpoly_zero(&r)){ // r!=0

      bigpoly quotient, remainder;
      Init_bigpoly(&quotient);
      Init_bigpoly(&remainder);
      poly_div(&oldr, &r, &remainder, &quotient); // a, b, remainder, quotient, a = b*quo+r
      
      bigpoly prev;
      Init_bigpoly(&prev);

      // (old_r, r) := (r, old_r − quotient × r)
      Copy_bigpoly(&prev, &r); // prev = r
      bigpoly quo_r;
      Init_bigpoly(&quo_r);
      big_poly_mul(&quo_r, &quotient, &prev); // quo_r = quotient*r
      big_poly_sub(&r, &oldr, &quo_r);   // r = oldr - quotient*r
      Copy_bigpoly(&oldr, &prev);            // old_r = prev

      // (old_s, s) := (s, old_s − quotient × s)
      Copy_bigpoly(&prev, s); // prev = s
      bigpoly quo_s;
      Init_bigpoly(&quo_s);
      big_poly_mul(&quo_s, &quotient, &prev); // quo_s = quotient*s
      big_poly_sub(s, &olds, &quo_s);   // s = olds - quotient*s
      Copy_bigpoly(&olds, &prev);            // old_s = prev

      
      // (old_t, t) := (t, old_t − quotient × t)
      Copy_bigpoly(&prev, t); // prev = t
      bigpoly quot;
      Init_bigpoly(&quot);
      big_poly_mul(&quot, &quotient, &prev); // quo_t = quotient*t
      big_poly_sub(t, &oldt, &quot);   // t = oldt - quotient*t
      Copy_bigpoly(&oldt, &prev);            // old_t = prev

  }

  int d_degree = get_big_degree(&oldr);
  if(d_degree==0){
    int32_t coeff_0 = oldr.coeffs[0];
    int64_t coeff_inv = 0;
    coeff_inv = inv_integer(coeff_0, Q);

    int64_t mul = coeff_inv*coeff_0;
    mul = mul % Q;
    oldr.coeffs[0] = mul;
    Copy_bigpoly(d, &oldr);  // oldr is gcd

    for(int i=0; i<bigN; i++){
      int32_t coeff_s = olds.coeffs[i];
      mul = coeff_inv*coeff_s;
      mul = mul % Q;
      olds.coeffs[i] = mul;

      int32_t coefft = oldt.coeffs[i];
      mul = coeff_inv*coefft;
      mul = mul % Q;
      oldt.coeffs[i] = mul;
    }

    Copy_bigpoly(s, &olds); 
    Copy_bigpoly(t, &oldt);  
    return;
  }
  Copy_bigpoly(d, &oldr);  // oldr is gcd
  Copy_bigpoly(s, &olds); 
  Copy_bigpoly(t, &oldt);  
  return;

}


// Output inverse of a: a_inv
int poly_inv(poly* a_inv, poly* a){
  bigpoly a_big;
  Init_bigpoly(&a_big);
  Copy_poly_to_bigpoly(&a_big, a);

  bigpoly modulo; // x^N+1
  Init_bigpoly(&modulo);
  modulo.coeffs[0]=1;
  modulo.coeffs[N]=1;

  bigpoly u, v, d;
  Init_bigpoly(&u);
  Init_bigpoly(&v);
  Init_bigpoly(&d);
  eGCD(&modulo, &a_big, &u, &v, &d); // a × u + b × v = d. degree(a) > degree(b)


  bigpoly au, bv;
  Init_bigpoly(&au);
  Init_bigpoly(&bv);
  big_poly_mul(&au, &modulo, &u);
  big_poly_mul(&bv, &a_big, &v);
  big_poly_add(&au, &au, &bv);   
  
  // d should be 1, au should be 1
  int test_one = test_bigpoly_one(&d);
  test_one &= test_bigpoly_one(&au);
  if(test_one!=1){
    printf("Inversion failed \n");
    return 0;
  }
  // v is my inverse
  printf("Inversion succeed \n");
  Copy_bigpoly_to_poly(a_inv, &v);
  return 1;
}

int test_poly(){

  // Test integer inversion
  int32_t num = Q-1;
  int64_t num_inv = inv_integer(num, Q);
  int64_t mul = num*num_inv;
  printf("num_inv %d\n", num_inv);
  mul = mul % Q;
  printf("num_inv*num mod Q %" PRIu64 "\n", mul);

  // Test polynomial multiplication: ab_c = a*b+c
  bigpoly a, b, b_copy, c, ab_c;
  Init_bigpoly(&a);
  Init_bigpoly(&b);
  Init_bigpoly(&b_copy);
  Init_bigpoly(&c);
  Init_bigpoly(&ab_c);

  a.coeffs[N/2] = 1;
  a.coeffs[N/4] = 1;
  a.coeffs[N/8] = 1;
  a.coeffs[N/16] = 1;
  a.coeffs[N/32] = 1;


  printf("a is \n");
  Print_bigpoly(&a);
  Print_big_hamming(&a);

  b.coeffs[N/2] = 1;
  b.coeffs[N/8] = 1;
  b.coeffs[N/16] = 1;
  b.coeffs[N/32] = 1;
  b.coeffs[N/64] = 1;

  printf("b is \n");
  Print_bigpoly(&b);
  Print_big_hamming(&b);

  c.coeffs[N/8] = 1;
  c.coeffs[N/16] = 1;
  c.coeffs[N/32] = 1;
  c.coeffs[N/64] = 1;
  c.coeffs[N/128] = 1;

  printf("c is \n");
  Print_bigpoly(&c);
  Print_big_hamming(&c);

  Copy_bigpoly(&b_copy, &b);

  big_poly_mul(&ab_c, &a, &b);
  big_poly_add(&ab_c, &ab_c, &c);   
  printf("a*b+c is \n");
  Print_bigpoly(&ab_c);
  Print_big_hamming(&ab_c);

  // Result: poly coefficient at index i corresponds to the coefficient of x^i

  // Test polynomial division ab_c/b, should give back q=a and r=c
  bigpoly r, q;
  poly_div(&ab_c, &b_copy, &r, &q);

  printf("remainder is (should be c) \n");
  Print_bigpoly(&r);
  Print_big_hamming(&r);
  printf("quotient is (should be a)\n");
  Print_bigpoly(&q);
  Print_big_hamming(&q);

  bigpoly qb_r;
  Init_bigpoly(&qb_r);
  big_poly_mul(&qb_r, &q, &b);
  big_poly_add(&qb_r, &qb_r, &r);   
  printf("q*b+r is (should be a*b+c)\n");
  Print_bigpoly(&qb_r);
  Print_big_hamming(&qb_r);

  poly my_poly, poly_inverse;
  Init_poly(&my_poly);
  Init_poly(&poly_inverse);
  my_poly.coeffs[N/2] = 45736;
  my_poly.coeffs[N/4] = 576;
  my_poly.coeffs[N/8] = 87694;
  my_poly.coeffs[N/16] = 2658376;
  my_poly.coeffs[N/32] = 1;
  my_poly.coeffs[N/2-4] = 7648347;
  my_poly.coeffs[N/4-8] = 4326;
  my_poly.coeffs[N/8-12] = 95476;
  my_poly.coeffs[N/16+5] = 456;
  my_poly.coeffs[N/32-4] = 54736;
  poly_inv(&poly_inverse, &my_poly);

}


int substract_sig(const uint8_t *sm,
                  const uint8_t *sm_faulty,
                  const uint8_t *pk,
                  const uint8_t *sk){

    uint8_t rho[SEEDBYTES];
    uint8_t c_correct[SEEDBYTES];
    uint8_t c_faulty[SEEDBYTES];

    poly cp_correct, cp_faulty;
    polyvecl z_correct, z_faulty, z_delta;
    polyveck t1, h_correct, h_faulty;
    
    unpack_pk(rho, &t1, pk);
    
    // get correct z
    if(unpack_sig(c_correct, &z_correct, &h_correct, sm)){
        return -1;
    }
    if(polyvecl_chknorm(&z_correct, GAMMA1 - BETA)){
        return -1;
    }
    poly_challenge(&cp_correct, c_correct);

    // get faulty z
    if(unpack_sig(c_faulty, &z_faulty, &h_faulty, sm_faulty))
      return -1;
    if(polyvecl_chknorm(&z_faulty, GAMMA1 - BETA))
      return -1;
    poly_challenge(&cp_faulty, c_faulty);

    polyvecl_sub(&z_delta, &z_correct, &z_faulty);

    printf("Delta z is: \n");
    int count_100 = 0;
    for(int i=0; i<L; i++){
      for(int j=0; j<N; j++){
        if( (z_delta.vec[i].coeffs[j]<100) && (z_delta.vec[i].coeffs[j]>-100) ){
          count_100 = count_100 + 1;
        }
        printf(" %d", z_delta.vec[i].coeffs[j]);
      }
      printf("\n");
    }
    if(count_100>100){
      // Get c
      poly c_delta, c_delta_copy;
      poly_sub(&c_delta, &cp_correct, &cp_faulty);
      Copy_poly(&c_delta_copy, &c_delta);
      positive_poly(&c_delta_copy);

      // Get s1
      uint8_t seedbuf[2*SEEDBYTES + 3*CRHBYTES];
      uint8_t *rho, *tr, *key;
      polyvecl s1;
      polyveck t0, s2;
      rho = seedbuf;
      tr = rho + SEEDBYTES;
      key = tr + CRHBYTES;
      unpack_sk(rho, tr, key, &t0, &s1, &s2, sk);

      // print s1
      printf("s1 is: \n");
      for(int i=0; i<L; i++){
        for(int j=0; j<N; j++){
          printf(" %d", s1.vec[i].coeffs[j]);
        }
        printf("\n");
      }

      polyvecl_ntt(&s1);
      poly_ntt(&c_delta);
      polyvecl s1_c_delta;
      polyvecl_pointwise_poly_montgomery(&s1_c_delta, &c_delta, &s1);
      polyvecl_invntt_tomont(&s1_c_delta);
      polyvecl_reduce(&s1_c_delta);

      // print z_delta
      printf("Delta z is: \n");
      for(int i=0; i<L; i++){
        for(int j=0; j<N; j++){
          printf(" %d", z_delta.vec[i].coeffs[j]);
        }
        printf("\n");
      }

      // print s1_c_delta
      printf("s1*c_delta is: \n");
      for(int i=0; i<L; i++){
        for(int j=0; j<N; j++){
          printf(" %d", s1_c_delta.vec[i].coeffs[j]);
        }
        printf("\n");
      }

      poly c_inverse;
      Init_poly(&c_inverse);
      int inv_ret = poly_inv(&c_inverse, &c_delta_copy);
      if(inv_ret == 0){
        return 0;
      }
      
      // z_delta* c_inverse
      polyvecl s1_recover;
      polyvecl_ntt(&z_delta);
      poly_ntt(&c_inverse);
      polyvecl_pointwise_poly_montgomery(&s1_recover, &c_inverse, &z_delta);
      polyvecl_invntt_tomont(&s1_recover);
      polyvecl_reduce(&s1_recover);

      // print s1_recover
      printf("s1_recover is: \n");
      for(int i=0; i<L; i++){
        for(int j=0; j<N; j++){
          printf(" %d", s1_recover.vec[i].coeffs[j]);
        }
        printf("\n");
      }

      

      return 1;
    }
    return 0;
}

void main(void){

    // Read messages
    char m[MLEN*SIGNUM];
    memset(m,0x00,MLEN*SIGNUM);
    char filename[200];
    FILE *msg_out;
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        sprintf(filename, "./messages/message_%d.bin", sig_index);
        msg_out = fopen(filename,"r+");
        fread(m+(MLEN*sig_index), 1, MLEN, msg_out); 
        fclose(msg_out);
    }

    // Read correct signatures
    uint8_t* corect_sm = (uint8_t*)malloc((MLEN + CRYPTO_BYTES)*SIGNUM*sizeof(uint8_t));
    FILE *sig_out;
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        sprintf(filename, "./correct_sigs/sig_%d.bin", sig_index);
        sig_out = fopen(filename,"r+");
        fread(corect_sm + ((MLEN+CRYPTO_BYTES)*sig_index), 1, (MLEN+CRYPTO_BYTES), sig_out);
        fclose(sig_out);
    }

    // Read faulty signatures
    uint8_t* faulty_sm = (uint8_t*)malloc((MLEN + CRYPTO_BYTES)*SIGNUM*sizeof(uint8_t));
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        sprintf(filename, "./faulty_sigs/sig_%d.bin", sig_index);
        sig_out = fopen(filename,"r+");
        fread(faulty_sm + ((MLEN+CRYPTO_BYTES)*sig_index), 1, (MLEN+CRYPTO_BYTES), sig_out);
        fclose(sig_out);
    }

    // Read in PK and SK
    uint8_t pk[PUBLICKEY_BYTES];
    uint8_t sk[SECRETKEY_BYTES];   
    FILE *pk_out = fopen("./keys/pk.bin","r+");
    FILE *sk_out = fopen("./keys/original_sk.bin","r+");
    fread(pk, sizeof(uint8_t), PUBLICKEY_BYTES, pk_out);
    fclose(pk_out);

    fread(sk, sizeof(uint8_t), SECRETKEY_BYTES, sk_out);
    fclose(sk_out);

    printf("\n pk \n");
    for(int i = 0; i < PUBLICKEY_BYTES; i++){
        printf("%x ", pk[i] & 0xff);
    }

    printf("\n sk \n");
    for(int i = 0; i < SECRETKEY_BYTES; i++){
        printf("%x ", sk[i] & 0xff);
    }

    // Recover

    for(int sig_index = 0; sig_index < 10; sig_index++){
        printf("Index: %d \n", sig_index);
        
        int sig_ret = substract_sig(&corect_sm[sig_index*(MLEN + CRYPTO_BYTES)], &faulty_sm[sig_index*(MLEN + CRYPTO_BYTES)], pk, sk);
        printf("The delta z differnece is: %d \n", sig_ret);
    }
    return 0;
}