p = r"D:\lingjing\src\main.cpp"
lines = open(p, encoding="utf-8").read().splitlines(keepends=True)
out = []
skip_next_endif = False
for i, ln in enumerate(lines):
    # remove top-level "    AddVectoredExceptionHandler(1, CrashHandler);" (the one right after CrashHandler def, inside the #ifdef block before main)
    if "AddVectoredExceptionHandler(1, CrashHandler);" in ln and i < 120:
        continue
    out.append(ln)
open(p, "w", encoding="utf-8", newline="").write("".join(out))
print("done")
