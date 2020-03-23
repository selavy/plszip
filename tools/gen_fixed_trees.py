#!/usr/bin/env python

import pprint


MAX_BIT_LENGTH = 15


def flip_code(code, codelen):
    binstr = f'{code:0{codelen}b}'
    return int(binstr[::-1], 2)


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


def group_by(vals, amount):
    grps = []
    grp = []
    for v in vals:
        if len(grp) == amount:
            grps.append(grp)
            grp = [v]
        else:
            grp.append(v)
    if grp:
        grps.append(grp)
    return grps


def print_tree(dtype, name, vals, min_width=2, nums_per_row=8):
    print(f"constexpr {dtype} {name}[{len(vals)}] = {{")
    grps = group_by(vals, nums_per_row)
    for grp in grps:
        vs = [f'{v:{min_width}}' for v in grp]
        print(f"    {', '.join(vs)},")
    print("};")


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

codes = lits + dsts
codelens = lit_codelens + dst_codelens

print("// clang-format off")
print(f"constexpr int NumFixedTreeLiterals = {len(lit_codelens)};")
print(f"constexpr int NumFixedTreeDistances = {len(dst_codelens)};")
print_tree(
    dtype='uint16_t',
    name='fixed_codes',
    vals=codes,
    min_width=3,
    nums_per_row=8,
)
print("")
print_tree(
    dtype='uint8_t',
    name='fixed_codelens',
    vals=codelens,
    min_width=1,
    nums_per_row=16,
)
print("// clang-format on")
