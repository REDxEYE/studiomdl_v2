"""Compares the structure (bones, textures, skins, sequences, flexes, meshes) and the vertices of two MDLs.

usage: vertcmp.py a.mdl b.mdl [--max-unmatched N]
With --max-unmatched the exit code is 1 if there are structure differences or more than N unmatched vertices.
Bone weights below 0.05 are culled on both sides (Valve's studiomdl drops them, v2 keeps them).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'golden'))
import mdl_struct  # noqa: E402
import sfm_diff  # noqa: E402


def main():
    args = sys.argv[1:]
    max_unmatched = None
    if '--max-unmatched' in args:
        i = args.index('--max-unmatched')
        max_unmatched = int(args[i + 1])
        del args[i:i + 2]
    a = mdl_struct.summarize(args[0], weight_cull=0.05)
    b = mdl_struct.summarize(args[1], weight_cull=0.05)
    va, vb = sfm_diff.normalize(a), sfm_diff.normalize(b)
    diffs = sfm_diff.diff(a, b, '', [], 50)
    unmatched = sum(len(sfm_diff.unmatched_vertices(va[k], vb.get(k, []))) for k in va)
    total = sum(len(v) for v in va.values())
    print(f'  => structure diffs {len(diffs)}, unmatched vertices {unmatched} of {total}')
    for d in diffs[:10]:
        print('     ', d)
    if max_unmatched is not None:
        sys.exit(1 if diffs or unmatched > max_unmatched else 0)


main()
