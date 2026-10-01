p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

old = """        static const char* vsSrc =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };\\n"
            "VSOut mainVS(uint id : SV_VertexID) {\\n"
            "  VSOut o;\\n"
            "  float2 pos = float2((id << 1) & 2, id & 2);\\n"
            "  o.pos = float4(pos * 2.0 - 1.0, 0.0, 1.0);\\n"
            "  o.uv = float2(pos.x, 1.0 - pos.y);\\n"
            "  return o;\\n"
            "}\\n";
"""
new = """        static const char* vsSrc =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };\\n"
            "VSOut mainVS(uint id : SV_VertexID) {\\n"
            "  VSOut o;\\n"
            "  float2 uv = float2((id << 1) & 2, id & 2);\\n"
            "  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\\n"
            "  o.uv = uv;\\n"
            "  return o;\\n"
            "}\\n";
"""
if old in s:
    s = s.replace(old, new); print("vs fixed")
else:
    print("MISS vs")
open(p, "w", encoding="utf-8", newline="").write(s)
