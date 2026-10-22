"""Read a bounded HOTD2 bat trace: python tools/read_bat_trace.py path-to-trace.json."""
import json
import math
from pathlib import Path
import struct
import sys


def read_trace(path):
    metadata = json.loads(path.read_text())
    assert metadata["version"] == 1 and 0 <= metadata["count"] <= 256
    raw = path.with_suffix(".bin").read_bytes()
    assert len(raw) == metadata["count"] * metadata["record_size"], "Incomplete trace"
    offsets = metadata["offsets"]
    lighting_offsets = metadata["lighting_offsets"]
    results = []

    def transform(p, m):
        return [sum(p[j] * m[j * 4 + k] for j in range(3)) + m[12 + k] for k in range(3)]

    for index in range(metadata["count"]):
        base = index * metadata["record_size"]

        def values(fmt, at):
            return struct.unpack_from("<" + fmt, raw, base + at)

        fields = values("13I", 0)
        frame, model, obj, render_pass, primitive, fvf, native_n, captured_n, n, count, first, texture, truncated = fields
        assert n <= 256 and count <= 768 and fvf == 0x112
        state = offsets["lighting"]
        world = values("16f", state + lighting_offsets["world"])
        view = values("16f", state + lighting_offsets["view"])
        material = values("17f", state + lighting_offsets["material"])
        flags = values("14I", state + lighting_offsets["ambient"])
        normal_matrix = values("9f", state + lighting_offsets["normal_matrix"])
        native = [values("8f", offsets["input"] + i * 32) for i in range(n)]
        captured = [values("3fI7f4If", offsets["output"] + i * 64) for i in range(count)]
        expected = [transform(transform(v[:3], world), view) for v in native]
        expected = [(v[0], v[1], -v[2]) for v in expected]
        error = None
        if expected and captured and not truncated:
            error = max(min(math.dist(v[:3], p) for p in expected) for v in captured)
        normal_lengths = [math.sqrt(sum(sum(v[3+j] * normal_matrix[j*3+k] for j in range(3)) ** 2
                                        for k in range(3))) for v in native]
        lights = []
        light_count = values("I", state + lighting_offsets["light_count"])[0]
        assert light_count <= 16 and metadata["light_size"] == 104
        for i in range(light_count):
            light = values("I25f", state + lighting_offsets["lights"] + i * metadata["light_size"])
            lights.append({"type": light[0], "diffuse": light[1:5], "specular": light[5:9],
                           "ambient": light[9:13], "position": light[13:16], "direction": light[16:19],
                           "range_falloff_attenuation_theta_phi": light[19:]})
        results.append({"frame": frame, "model": hex(model), "object": hex(obj), "pass": render_pass,
                        "primitive": primitive, "native_vertices": native_n, "captured_vertices": captured_n,
                        "first_vertex": first, "texture_surface": hex(texture), "truncated": bool(truncated),
                        "max_capture_transform_error": error,
                        "transformed_normal_length_range": [min(normal_lengths), max(normal_lengths)] if normal_lengths else [],
                        "white_specular_vertices": sum((v[13] & 0xFFFFFF) == 0xFFFFFF for v in captured),
                        "material": {"diffuse": material[:4], "ambient": material[4:8], "specular": material[8:12],
                                     "emissive": material[12:16], "power": material[16]},
                        "ambient": hex(flags[0]), "lighting": flags[1], "colorvertex": flags[2],
                        "material_sources": flags[3:7], "specular_enabled": flags[7],
                        "local_viewer": flags[8], "normalize_normals": flags[9], "fog_states": flags[10:],
                        "world": world, "view": view,
                        "native_record_matrix": values("16f", offsets["native_record"] + 52),
                        "projection": values("16f", offsets["projection"]), "lights": lights})
    return {"metadata": metadata, "draws": results}


if __name__ == "__main__":
    print(json.dumps(read_trace(Path(sys.argv[1])), indent=2, allow_nan=False))
