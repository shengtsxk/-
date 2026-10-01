import io
p = r"D:\lingjing\src\main.cpp"
s = open(p, encoding="utf-8").read()
main_hdr = "int main(int argc, char* argv[]) {"
idx = s.index(main_hdr)
start = s.index("#ifdef _WIN32", idx)
end_marker = "AddVectoredExceptionHandler(1, CrashHandler);\n#endif"
end = s.index(end_marker, start) + len(end_marker)
block = s[start:end]  # the crash-handler block (has its own #ifdef/#endif)
after = s[end:]
rest = s[idx+len(main_hdr):start] + after
new = s[:idx] + block + "\n\n" + main_hdr + "\n\n#ifdef _WIN32\n    AddVectoredExceptionHandler(1, CrashHandler);\n#endif\n" + rest
open(p, "w", encoding="utf-8", newline="").write(new)
print("done")
