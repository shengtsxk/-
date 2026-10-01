import re
p = r"D:\lingjing\src\pipeline\Presenter.cpp"
lines = open(p, encoding="utf-8").read().splitlines(keepends=True)
out = []
removed = 0
for ln in lines:
    if re.search(r'\[dbg\]', ln):
        removed += 1
        continue
    out.append(ln)
open(p, "w", encoding="utf-8", newline="").write("".join(out))
print(f"removed {removed} debug lines")
