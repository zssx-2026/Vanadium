# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding="utf-8")
src = open("tools/codegen/bootstrap_schema.py", encoding="utf-8").read()
q = chr(34) * 3
blk = src.split("IDENT = " + q)[1].split(q)[0]
chars = sorted(set(blk.split()))
print("count", len(chars))
print("".join(chars))
