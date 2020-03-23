#!/usr/bin/env python


def flip_code(code, codelen):
    binstr = f'{code:0{codelen}b}'
    return int(binstr[::-1], 2)


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


def print_array(dtype, name, vals, min_width=2, nums_per_row=8):
    print(f"constexpr {dtype} {name}[{len(vals)}] = {{")
    grps = group_by(vals, nums_per_row)
    for grp in grps:
        vs = [f'{v:{min_width}}' for v in grp]
        print(f"    {', '.join(vs)},")
    print("};")

