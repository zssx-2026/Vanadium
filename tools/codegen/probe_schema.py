import json, sys, os
sys.path.insert(0, os.path.join(r"D:\dev\Infinity File Version Control System", "tools", "codegen"))
from data_messages import MSG
from data_errors import ERR
from data_commands import CMD, GLOBAL_OPTS
base = os.path.join(r"D:\dev\Infinity File Version Control System", "schema")
m = json.load(open(os.path.join(base, "messages.json"), encoding="utf-8"))
e = json.load(open(os.path.join(base, "errors.json"), encoding="utf-8"))
c = json.load(open(os.path.join(base, "commands.json"), encoding="utf-8"))
jm = set(m["messages"].keys()); dm = set(k for k, _, _ in MSG)
je = set(x["code"] for x in e["errors"]); de = set(k for k, _, _ in ERR)
jc = [x["name"] for x in c["commands"]]
print("MSG json=%d data=%d missing_in_json=%d extra_in_json=%d" % (len(jm), len(dm), len(dm-jm), len(jm-dm)))
print("  extra sample:", sorted(jm-dm)[:8])
print("ERR json=%d data=%d missing=%d extra=%d" % (len(je), len(de), len(de-je), len(je-de)))
print("  extra sample:", sorted(je-de)[:8])
print("CMD json=%d data=%d" % (len(jc), len(CMD)))
print("  json names:", jc)
print("  data names:", [r["name"] for r in CMD])
print("GLOB json=%d data=%d" % (len(c["global_options"]), len(GLOBAL_OPTS)))
print("  glob json:", [o["flag"] for o in c["global_options"]])
