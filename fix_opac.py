# 3) 透明度全面：全局 QWidget 背景改为半透明面板色（随透明度变化）
p = r"D:\lingjing\src\ui\Theme.cpp"
s = open(p, encoding="utf-8").read()
old = """/* === 全局 === */
QWidget {
    background-color: %1;
    color: %2;
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
}
"""
new = """/* === 全局 === */
QWidget {
    background-color: %18;
    color: %2;
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
}
"""
if old in s:
    s = s.replace(old, new, 1); print("global opacity fixed")
else:
    print("MISS opacity")
open(p, "w", encoding="utf-8", newline="").write(s)
