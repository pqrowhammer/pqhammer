
# Sage reset. Clears active variables and resets execution.
from sage.all import *

## Imports ##
import os
import sys
import json
import numpy as np
import bitstring
from tqdm import tqdm
from frodokem import FrodoKEM
from time import process_time
from scipy.stats import truncnorm
from kyber import Kyber1024
from multiprocess import Pool
from functools import partial
## Sage loads ##

## Function Definitions ##
def col_to_index_set(col):
    j = Cols.index(col)
    indices = np.arange(0, 64, 8) + j
    return Rows[j], indices

def vec(x):
    # Is this a 2-dim object, with only one row
    try:
        x[0][0]
        try:
            x[1]
        except:
            return matrix(x)
        raise ValueError(
            " The object has more than one line: can't convert to vec.")
    except:
        # Then it should be a 1 dim object
        return matrix([x])

def concatenate(L1, L2=None):
    """
    concatenate vecs
    """
    if L2 is None:
        return vec(sum([list(vec(x)[0]) for x in L1], []))

    return vec(list(vec(L1)[0]) + list(vec(L2)[0]))

def recenter(elt):
    if elt > q / 2:
        return elt - q
    return elt

# Convert a 1*1 matrix into a scalar
def scal(M):
    assert M.nrows() == 1 and M.ncols() == 1, "This doesn't seem to be a scalar."
    return M[0, 0]

# Flatten a polyvec loaded from ct_data json file
def polyvec_to_vec(polyvec, k):
    v = vec(polyvec['0'])
    v_m = [R(polyvec['0'])] # Module element
    for i in range(1, k):
        v = concatenate([v, polyvec[str(i)]])
        v_m.append(R(polyvec[str(i)]))

    return v, matrix(v_m)

def find_opt_rotation(ct_data, poison_vec, k, n):
    # Load ct_data into vectors
    sp = ct_data["sp"]
    rotations = R(0)
    
    for i in range(k):
        spp = P(sp[str(i)])
        poison_poly = P(poison_vec[i*n:(i+1)*n])
        rotations += R(spp*poison_poly)
        # print(vec(R(spp).matrix().column(17))*vec(R(poison_poly).list()).T)
    
    epp = R(ct_data['epp'])
    dv = R(ct_data['v']) - R(ct_data['v_comp'])
    
    _, sp_m = polyvec_to_vec(sp, k) 
    _, ep_m = polyvec_to_vec(ct_data['ep'], k) 
    _, du_m = polyvec_to_vec(ct_data['b'], k)
    _, du_comp_m = polyvec_to_vec(ct_data['b_comp'], k) 

    rotations = matrix(ZZ, (rotations + epp - dv).list()).apply_map(recenter)
    true_rotations = R(ct_data["v_comp"]) - s*(du_comp_m).T
    true_rotations = matrix(ZZ, true_rotations[0, 0].list()).apply_map(recenter)
    rotation = np.argmax(np.abs(rotations))
    true_rotation = np.argmax(np.abs(true_rotations))
    sign = 1 if rotations[0, rotation] > 0 else -1
    correct = 1 if rotation == true_rotation else 0
    
    # print(rotations[0, rotation], true_rotations[0, true_rotation])
    # print(rotation, true_rotation)
    # print("============")

    return rotation, rotations[0, rotation], sign, correct

def rotate_poly(a, rotation):
    return list(R(a).matrix().column(rotation))

def rotate_polyvec(a, rotation, k):
    a_rot = []
    for i in range(k):
        a_rot += rotate_poly(a[str(i)], rotation)

    return a_rot

def process_ct_file(ctfile, k=4, n=256):
    with open(ctfile) as ctext:
        ct_data = json.load(ctext)
        
        # Calculate the most likely failure coefficient (rotation)
        # rotation, threshold_offset, sign = find_opt_rotation(ct_data, poison_vec, k, n)
        rotation, threshold_offset, sign, _ = find_opt_rotation(ct_data, poison_vec, k, n)
        
        # # for r in range(n):
        #     # print(f"Rotation {r}:")
        sp = matrix(RDF, rotate_polyvec(ct_data["sp"], rotation, k)).apply_map(recenter)
        # print(sp*vec(poison_vec).T.apply_map(recenter))

        ep = matrix(RDF, rotate_polyvec(ct_data["ep"], rotation, k)).apply_map(recenter)
        # epp = vec(ct_data["epp"]).apply_map(recenter)[0, rotation]
        # b = polyvec_to_vec(ct_data["b"], k).apply_map(recenter) 
        # b_comp = polyvec_to_vec(ct_data["b_comp"], k).apply_map(recenter)
        b = matrix(RDF, rotate_polyvec(ct_data["b"], rotation, k))
        b_comp = matrix(RDF, rotate_polyvec(ct_data["b_comp"], rotation, k))
        # b_comp = polyvec_to_vec(ct_data["b_comp"], k).apply_map(recenter)

        # v = vec(ct_data["v"])
        # v_comp = vec(ct_data["v_comp"])

        # dv = matrix(ZZ, (R(ct_data['v']) - R(ct_data['v_comp'])).list()).apply_map(recenter)
        # v_rounding_err = dv[0, rotation]

        b_rounding_err = (b - b_comp).apply_map(recenter)
        # avg_rounding_error += b_rounding_err 
        combined_vec = (sign * concatenate([sp, -(ep - b_rounding_err)]))
        threshold = (round(q/4) - sign*threshold_offset)
        
        return combined_vec, threshold

# Set Kyber-1024 parameters
n = 256
m = n 
k = 4
q = 3329
F = GF(q)
z = var('z')
P = PolynomialRing(F, 'z')
R = QuotientRing(P, P.ideal(z**n + 1))
inv_mont = R(2**16).inverse()
variance = 1
expected_norm = RR(sqrt(2*n*k)*variance)
# Poisoned Key Modified Coordinate Positions
poison_vec = (k*n)*[0]
poison_vec[137] = 256  # Flipped a 256-bit in position e[0][137] 496
poison_vec[590] = 64  # Flipped a 64-bit in position e[2][78] 1002
# poison_vec = vec(poison_vec)


# File information
prefix = "../kyber/failedcts-ddr4" # Change to parent directory of ciphertext DB and key files
pkfile = "../kyber/pk-ddr4.bin"
skfile = "../kyber/sk-ddr4.bin"


## Load keys from files ##
with open(pkfile, 'rb') as keyfile:
    pk = keyfile.read()
    # Decode Public key into A, b
    At_mat, bt_mat = Kyber1024.decode_pk(pk)
    
    # Convert A into Ring elements
    A = []
    for i in range(k):
        row = []
        for j in range(k):
            row.append(R(At_mat[i][j].coeffs)*inv_mont)

        A.append(row)

    # Convert B into ring elements
    b = []
    for i in range(k):
        b.append(R(bt_mat[0][i].coeffs)*inv_mont)

    A = matrix(A).T #A in transpose form
    b = matrix(b)

## Generate real s, e for checking experimental secret
with open(skfile, 'rb') as keyfile:
    sk = keyfile.read()
    sk_mat = Kyber1024.decode_sk(sk)
    sk_list = []
    s = []
    for i in range(k):
        s.append(R(sk_mat[0][i].coeffs)*inv_mont)
        sk_list += (R(sk_mat[0][i].coeffs)*R(2^16).inverse()).list()
    
    s = matrix(s)
    sk_vec = matrix(ZZ, sk_list).apply_map(recenter)


e = b - s*A.T
e_list = []
for i in range(k):
    e_list += e[0, i].list()

e_vec = matrix(ZZ, e_list).apply_map(recenter)

## Open ctext files.
f = partial(process_ct_file, k=k, n=n)
ct_json_files = [ct_file.path for ct_file in os.scandir(prefix) if ".json" in ct_file.name]
num_cts = len(ct_json_files)

pool = Pool(15)
pool_result = []
for result in tqdm(pool.imap_unordered(f, ct_json_files), total=len(ct_json_files)):
    pool_result.append(result)

pool.close()
pool.join()

combined_vecs, thresholds = zip(*pool_result)
combined_vecs = [x for x in combined_vecs if x is not None]
thresholds = [x for x in thresholds if x is not None]

combined_vec_avg = sum(combined_vecs) / num_cts
threshold_avg = sum(thresholds) / num_cts

# print(f"Correct Rotation rate: {RR(total_correct/num_cts)}")
save(combined_vec_avg, "combined_vec_avg")
save(threshold_avg, "threshold_avg")

## Uncomment to load from previously processed cts
# combined_vec_avg = load("combined_vec_avg.sobj")
# threshold_avg = load("threshold_avg.sobj")

combined_vec_avg = combined_vec_avg.delete_columns([137, 590])

print(threshold_avg)
print(combined_vec_avg)

# Find experimental secret, checking against known key

print("Solving for key...")
print("==========================")
print("variance = 1, numerical alpha")
var = 1

expected_norm = RR(sqrt(2*n*k)*var)
for offset in range(round(threshold_avg)):
    for i in np.arange(-2, 3):
        for j in np.arange(-2, 3):
            print(f"Trying: {offset}, {i}, {j}...")
            a = float((threshold_avg - i*2 - j*2 - offset) / (expected_norm*sqrt(var)))
            alpha = truncnorm.mean(a, np.inf)*sqrt(var)
            experimental_secret = matrix((expected_norm/alpha) * combined_vec_avg)
            # Use james-stein estimator for more accurate parameter estimation
            js_coeff = float(1 - (((2*k*n)-3)*(var)*(expected_norm/alpha)**2/num_cts)/scal(experimental_secret*experimental_secret.T))
            experimental_secret = matrix(RDF, experimental_secret)
            experimental_secret = matrix(ZZ, np.round(js_coeff*experimental_secret))
            experimental_secret = matrix(ZZ, np.insert(np.round(experimental_secret), 137, i+256, axis=1))
            experimental_secret = matrix(ZZ, np.insert(np.round(experimental_secret), 590, j+64, axis=1))
            assert experimental_secret[0, 137] == i+256 and experimental_secret[0, 590] == j+64
            # experimefrom sage.all import oo, sqrt, log, RR, floor, cached_functiontal_secret = matrix(ZZ, np.insert(np.round(js_coeff*experimental_secret), row, i, axis=1))
            # print(np.sum(np.abs(experimental_secret[0, 1024:] - sk_vec)))
        # if (experimental_secret * IA.T) % q == b:
            # true_secret = experimental_secret
            # break
            # print((concatenate([e_vec, sk_vec]) - experimental_secret).norm())
            if sum(np.abs(experimental_secret[0, 0:1024] - e_vec)[0]) == 0:
                print("Secret found!")
                print(f"e = {experimental_secret[0, 0:1024]}")
                sys.exit(0)


print("Secret not found. Check ciphertexts!")
sys.exit(1)

