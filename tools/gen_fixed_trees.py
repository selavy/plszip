#!/usr/bin/env python

from utils import flip_code
from utils import group_by

MAX_BIT_LENGTH = 15


def init_tree(codelens):
    bl_count = [0]*MAX_BIT_LENGTH
    next_code = [0]*MAX_BIT_LENGTH
    codes = []
    max_bit_len = max(codelens)

    for codelen in codelens:
        bl_count[codelen] += 1

    code = 0
    for bits in range(1, max_bit_len+1):
        code = (code + bl_count[bits - 1]) << 1
        next_code[bits] = code

    for codelen in codelens:
        if codelen == 0:
            codes.append(0)
        else:
            code = flip_code(next_code[codelen], codelen)
            codes.append(code)
            next_code[codelen] += 1

    return codes


lit_codelens = []
while len(lit_codelens) < 144:
    lit_codelens.append(8)
while len(lit_codelens) < 256:
    lit_codelens.append(9)
while len(lit_codelens) < 280:
    lit_codelens.append(7)
while len(lit_codelens) < 288:
    lit_codelens.append(8)
assert len(lit_codelens) == 288

dst_codelens = [5]*32
assert len(dst_codelens) == 32

lits = init_tree(lit_codelens)
dsts = init_tree(dst_codelens)

num_fixed_tree_lits = len(lit_codelens)
num_fixed_tree_dists = len(dst_codelens)
fixed_codes = lits + dsts
fixed_codelens = lit_codelens + dst_codelens
