#!/usr/bin/env bash
# sha256 of the protocol objects and libmipfpsi.a: audit_r5/build vs impl/build12.
R=<ARTIFACT_ROOT>
OUT=$R/experiments_r6/e5_abort/sha256_compare.txt
: > "$OUT"
all=1
for o in fpsi HamCpsi oep osn circuits mip ot_threshold; do
  a=$(sha256sum "$R/audit_r5/build/CMakeFiles/mipfpsi.dir/src/$o.cpp.o" | cut -d' ' -f1)
  b=$(sha256sum "$R/impl/build12/CMakeFiles/mipfpsi.dir/src/$o.cpp.o" | cut -d' ' -f1)
  s=SAME; [ "$a" = "$b" ] || { s=DIFF; all=0; }
  echo "$o.cpp.o audit_r5=$a build12=$b $s" >> "$OUT"
done
a=$(sha256sum "$R/audit_r5/build/libmipfpsi.a" | cut -d' ' -f1)
b=$(sha256sum "$R/impl/build12/libmipfpsi.a" | cut -d' ' -f1)
s=SAME; [ "$a" = "$b" ] || s=DIFF
echo "libmipfpsi.a audit_r5=$a build12=$b $s" >> "$OUT"
for f in fpsi_bench net_bench unit_tests; do
  a=$(sha256sum "$R/audit_r5/build/$f" | cut -d' ' -f1)
  b=$(sha256sum "$R/impl/build12/$f" | cut -d' ' -f1)
  s=SAME; [ "$a" = "$b" ] || s=DIFF
  echo "$f audit_r5=$a build12=$b $s" >> "$OUT"
done
echo "protocol_objects_all_same=$all" >> "$OUT"
cat "$OUT"
