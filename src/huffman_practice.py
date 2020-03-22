#/usr/bin/env python

from collections import Counter

litlens = [
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 7, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	4, 0, 8, 0, 0, 0, 0, 0,
	8, 7, 0, 0, 6, 9, 7, 0,
	0, 0, 8, 8, 0, 7, 9, 8,
	9, 0, 9, 9, 9, 0, 9, 0,
	0, 9, 0, 0, 0, 9, 0, 0,
	9, 0, 0, 9, 9, 0, 0, 0,
	0, 0, 0, 0, 9, 0, 0, 0,
	0, 0, 9, 0, 0, 0, 0, 0,
	0, 4, 6, 5, 6, 4, 7, 8,
	6, 5, 9, 8, 6, 6, 5, 5,
	6, 9, 5, 5, 5, 5, 7, 7,
	9, 7, 9, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	9, 4, 4, 4, 5, 6, 6, 6,
	7, 6, 8, 6, 7, 7, 9,
]

dstlens = [
	0, 0, 0, 8, 8, 0, 6, 7,
	5, 5, 5, 4, 3, 4, 4, 4,
	3, 3, 3, 4, 4,
]

lens = litlens + dstlens
counts = Counter(lens)
print(counts)