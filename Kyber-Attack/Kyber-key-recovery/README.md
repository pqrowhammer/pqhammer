# Kyber secret key recovery

## Requirements

- System-wide sagemath installation (preferably by Linux package manager)
- Kyber reference code from the official repository
- git patch to apply json processing to CT files (included here)
- kyber python implementation by Giacomo Pope: https://github.com/GiacomoPope/kyber-py
- modified kyber.py file (included here)
- recover_kyber_key.py file (included here)
- python multiprocess library

## Instructions
- Apply git patch to kyber reference code
- cd ref and run `make process_cts`
- run `process_cts <pk_file> <ct_directory>`
- replace kyber.py with modified file
- Modify paths in recover_kyber_key.py to point to the locations of keys and ciphertexts
- run recover_kyber_key.py from the same directory as the python (modified) kyber implementation
