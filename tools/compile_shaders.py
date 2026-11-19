#!/usr/bin/env python3
"""Offline HLSL compilation. Normal app builds use the checked-in embedded outputs.

Prefer SDL_shadercross (SPIR-V, DXIL, MSL and resource reflection). DXC plus
SPIRV-Cross produces the same three formats. --msl-only preserves existing binary outputs.
No compiler is linked into or shipped alongside the app.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/shaders/identity.frag.hlsl"
HEADER = ROOT / "src/engine/shaders/identity.generated.h"
MANIFEST = ROOT / "assets/shaders/identity.manifest.json"
NAMES = ("identity", "enhanced", "mode7", "focus", "fast_forward", "influence")


def embedded(header, name, kind):
    if kind == "msl":
        return re.search(rf'{name}_msl\[\] = R"SAWGODMSL\((.*?)\)SAWGODMSL";', header, re.S)[1].encode()
    body = re.search(rf'{name}_{kind}\[\] = \{{(.*?)\}};', header, re.S)[1]
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', body))


def check_metal(metal, abi):
    text = metal.decode()
    if not re.search(r'fragment\s+\w+\s+main0\(', text):
        raise ValueError("Missing Metal fragment entrypoint main0")
    # Preserve SPIR-V binding numbers: SDL binds each Metal resource table from zero.
    for kind, count in (("texture", abi['samplers']), ("sampler", abi['samplers']),
                        ("buffer", abi['uniform_buffers'])):
        indices = sorted(set(map(int, re.findall(r'\[\[' + kind + r'\((\d+)\)\]\]', text))))
        # A shader that only reads its textures (Load) has no samplers left; SDL's bound samplers go unused.
        if kind == "sampler" and not indices:
            continue
        if indices != list(range(count)):
            raise ValueError(f"Unexpected Metal {kind} bindings: {indices}, expected {count}")


def cross_metal(compiler, spirv, work):
    spv, msl = work / 'shader.spv', work / 'shader.msl'
    spv.write_bytes(spirv)
    # Independent texture/sampler/buffer tables use the authored binding indices.
    run([compiler, spv, '--msl', '--msl-version', '20000', '--msl-decoration-binding',
         '--output', msl])
    return msl.read_bytes().replace(b'\r\n', b'\n') # Windows builds of SPIRV-Cross write CRLF


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(cmd):
    subprocess.run([str(x) for x in cmd], check=True)


def reflect_spirv(data, name="identity"):
    """Check each shader's actual SPIR-V interface without runtime tooling."""
    enhanced = name != "identity"
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V magic")
    decorations, variables = {}, {}
    fragment = False
    cursor = 5
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 65535
        if size < 1 or cursor + size > len(words):
            raise ValueError("Malformed SPIR-V instruction")
        args = words[cursor + 1:cursor + size]
        if opcode == 15:  # OpEntryPoint; execution model Fragment = 4
            fragment = args[0] == 4
        elif opcode == 71:  # OpDecorate
            decorations.setdefault(args[0], {})[args[1]] = list(args[2:])
        elif opcode == 59:  # OpVariable (type, result ID, storage class)
            variables[args[1]] = args[2]
        cursor += size
    bindings, inputs, outputs = [], [], []
    for ident, storage in variables.items():
        d = decorations.get(ident, {})
        if 34 in d:  # DescriptorSet, Binding
            bindings.append([d[34][0], d[33][0]])
        if storage == 1 and 30 in d:  # Input, Location
            inputs.append(d[30][0])
        if storage == 3 and 30 in d:  # Output, Location
            outputs.append(d[30][0])
        if storage in (2, 12) and not enhanced:  # Uniform / StorageBuffer
            raise ValueError("Identity shader unexpectedly declares a buffer")
    # DXC may use separate texture + sampler variables with the same binding.
    samplers, uniforms = {"identity": (1, 0), "enhanced": (12, 1), "influence": (1, 1), "mode7": (13, 1), "focus": (1, 1), "fast_forward": (3, 1)}[name]
    expected = {(2, i) for i in range(samplers)} | {(3, i) for i in range(uniforms)}
    if not fragment or set(map(tuple, bindings)) != expected:
        raise ValueError(f"Unexpected fragment resource bindings: {bindings}")
    if sorted(inputs) != [0, 1] or outputs != [0]:
        raise ValueError(f"Unexpected renderer interface: inputs {inputs}, outputs {outputs}")
    return {"samplers": samplers, "uniform_buffers": uniforms, "storage_buffers": 0,
            "storage_textures": 0, "descriptor_bindings": bindings,
            "input_locations": sorted(inputs), "output_locations": outputs}


def array(name, data):
    lines = ["    " + ", ".join(f"0x{x:02x}" for x in data[i:i + 16]) + ","
             for i in range(0, len(data), 16)]
    return f"alignas(4) inline constexpr unsigned char {name}[] = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    selection = p.add_mutually_exclusive_group()
    selection.add_argument("--name", choices=NAMES, default="identity")
    selection.add_argument("--all", action="store_true", help="process every embedded shader")
    p.add_argument("--msl-only", action="store_true", help="add MSL from existing SPIR-V, preserving SPIR-V/DXIL bytes")
    p.add_argument("--shadercross", type=Path)
    p.add_argument("--dxc", type=Path)
    p.add_argument("--spirv-cross", type=Path)
    p.add_argument("--check", action="store_true", help="validate source/output hashes, no compiler needed")
    p.add_argument("--allow-missing-msl", action="store_true",
                   help="bootstrap validation before the first Metal generation (not for CI)")
    args = p.parse_args()
    if args.all:
        forwarded = [x for x in sys.argv[1:] if x != '--all']
        for name in NAMES:
            run([sys.executable, __file__, *forwarded, '--name', name])
        return
    global SOURCE, HEADER, MANIFEST
    SOURCE = ROOT / f"assets/shaders/{args.name}.frag.hlsl"
    HEADER = ROOT / f"src/engine/shaders/{args.name}.generated.h"
    MANIFEST = ROOT / f"assets/shaders/{args.name}.manifest.json"
    if args.check:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        if manifest["source_sha256"] != digest(SOURCE.read_bytes()):
            p.error("HLSL changed: regenerate the embedded shaders")
        if manifest["header_sha256"] != digest(HEADER.read_bytes()):
            p.error("Embedded shader header changed: regenerate it")
        header = HEADER.read_text(encoding='utf-8')
        abi = reflect_spirv(embedded(header, args.name, 'spirv'), args.name)
        if abi != manifest['abi']:
            p.error('SPIR-V reflection differs from the manifest')
        for kind in ('spirv', 'dxil', 'msl'):
            if kind == 'msl' and manifest['msl_sha256'] is None:
                if args.allow_missing_msl:
                    continue
                p.error('Missing Metal output: run --all --msl-only --spirv-cross PATH')
            if manifest[f'{kind}_sha256'] != digest(embedded(header, args.name, kind)):
                p.error(f'{kind} output hash mismatch')
        if not args.allow_missing_msl or manifest['msl_sha256'] is not None:
            check_metal(embedded(header, args.name, 'msl'), abi)
        if args.spirv_cross:
            with tempfile.TemporaryDirectory(prefix='sawgod-metal-check-') as scratch:
                actual = cross_metal(args.spirv_cross, embedded(header, args.name, 'spirv'), Path(scratch))
                if actual != embedded(header, args.name, 'msl'):
                    p.error('SPIRV-Cross MSL output changed: regenerate and review')
        if manifest['msl_sha256'] is None:
            print(f'{args.name}: binary hashes match; MSL not generated (bootstrap only)')
        else:
            print(f"{args.name}: embedded shader hashes and Metal ABI match")
        return
    if args.msl_only:
        if not args.spirv_cross:
            p.error('--msl-only requires --spirv-cross PATH')
        header = HEADER.read_text(encoding='utf-8')
        manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        if manifest['source_sha256'] != digest(SOURCE.read_bytes()) or manifest['header_sha256'] != digest(HEADER.read_bytes()):
            p.error('Stale embedded outputs: regenerate SPIR-V/DXIL first')
        abi = reflect_spirv(embedded(header, args.name, 'spirv'), args.name)
        if abi != manifest['abi']:
            p.error('SPIR-V reflection differs from the manifest')
        with tempfile.TemporaryDirectory(prefix='sawgod-metal-') as scratch:
            metal = cross_metal(args.spirv_cross, embedded(header, args.name, 'spirv'), Path(scratch))
        check_metal(metal, abi)
        header = re.sub(rf'({args.name}_msl\[\] = R"SAWGODMSL\().*?(\)SAWGODMSL";)',
                        lambda m: m[1] + metal.decode() + m[2], header, flags=re.S)
        HEADER.write_text(header, encoding='utf-8', newline='')
        manifest.update(header_sha256=digest(HEADER.read_bytes()), msl_sha256=digest(metal),
                        spirv_cross_sha256=digest(args.spirv_cross.read_bytes()))
        MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='')
        print(f'{args.name}: embedded {len(metal)} MSL bytes; preserved SPIR-V/DXIL')
        return
    compiler = args.shadercross or args.dxc
    mode = "shadercross" if args.shadercross else "dxc"
    if not compiler:
        found = shutil.which("shadercross") or shutil.which("dxc")
        if not found:
            p.error("provide --shadercross PATH or --dxc PATH (offline tool only)")
        compiler = Path(found)
        mode = "shadercross" if compiler.name.startswith("shadercross") else "dxc"
    compiler = compiler.resolve()
    with tempfile.TemporaryDirectory(prefix="sawgod-shaders-") as scratch:
        work = Path(scratch)
        spv, dxil, msl = [work / f"identity.{ext}" for ext in ("spv", "dxil", "msl")]
        if mode == "shadercross":
            for fmt, dest in (("SPIRV", spv), ("DXIL", dxil), ("MSL", msl)):
                run([compiler, SOURCE, "-s", "HLSL", "-t", "fragment", "-e", "main",
                     "-d", fmt, "-o", dest])
            reflection = work / "identity.json"
            run([compiler, spv, "-s", "SPIRV", "-t", "fragment", "-d", "JSON", "-o", reflection])
            tool_reflection = json.loads(reflection.read_text(encoding="utf-8"))
        else:
            common = [compiler, SOURCE, "-T", "ps_6_0", "-E", "main", "-O3"]
            run(common + ["-spirv", "-fspv-flatten-resource-arrays", "-fspv-preserve-bindings",
                          "-fspv-preserve-interface", "-Fo", spv])
            run(common + ["-Fo", dxil])
            tool_reflection = None
            if args.spirv_cross:
                msl.write_bytes(cross_metal(args.spirv_cross, spv.read_bytes(), work))
        spirv, directx = spv.read_bytes(), dxil.read_bytes()
        abi = reflect_spirv(spirv, args.name)
        if not directx.startswith(b"DXBC") or b"DXIL" not in directx:
            raise ValueError("Missing DXIL container")
        metal = msl.read_text(encoding="utf-8") if msl.exists() else ""
        check_metal(metal.encode(), abi)
        header = ("// Generated by tools/compile_shaders.py; do not edit.\n#pragma once\n"
                  "namespace sawgod::shaders {\n" + array(f"{args.name}_spirv", spirv) +
                  array(f"{args.name}_dxil", directx) +
                  f'inline constexpr char {args.name}_msl[] = R"SAWGODMSL(' + metal + ')SAWGODMSL";\n}\n')
        HEADER.parent.mkdir(parents=True, exist_ok=True)
        HEADER.write_text(header, encoding="utf-8", newline="")
        manifest = dict(source_sha256=digest(SOURCE.read_bytes()), header_sha256=digest(HEADER.read_bytes()),
                        compiler=mode, compiler_sha256=digest(compiler.read_bytes()),
                        spirv_sha256=digest(spirv), dxil_sha256=digest(directx),
                        msl_sha256=digest(metal.encode()) if metal else None,
                        abi=abi, tool_reflection=tool_reflection)
        MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="")
        print(f"Embedded {len(spirv)} SPIR-V bytes, {len(directx)} DXIL bytes, {len(metal)} MSL characters")


if __name__ == "__main__":
    main()
