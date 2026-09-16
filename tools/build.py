"""
build.py -- 在不依赖 vcvarsall.bat 的前提下调用 MSVC cl.exe 构建 ra2core。

为什么需要它：
  vcvarsall.bat 内部会调用 reg.exe 查注册表，而在受限沙箱里 reg.exe 被拦截，
  导致 `call vcvarsall.bat x64` 直接失败。这里改为自己拼 INCLUDE / LIB / PATH，
  效果等价但不需要碰注册表。

用法：
  python tools/build.py            # Release，输出到 build/ra2core.exe + build/ra2view.exe
  python tools/build.py --clean
  python tools/build.py --run      # 构建完立刻跑冒烟测试
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ra2core：无窗口的验证台（确定性基准 + MIX 解密回归测试）
CORE_SOURCES = [
    "src/main.cpp",
    "src/map/Map.cpp",
    "src/ai/PathFinder.cpp",
    "src/engine/FrameQueue.cpp",
    "src/engine/GameLoop.cpp",
    "src/threading/TaskSystem.cpp",
    "src/io/FileSystem.cpp",
    "src/io/MixCrypto.cpp",
    "src/io/Lzo1x.cpp",
    "src/io/Format80.cpp",
    "src/gfx/Palette.cpp",
    "src/gfx/ShpFile.cpp",
    "src/gfx/TmpFile.cpp",
    "src/gfx/PcxFile.cpp",
    "src/gfx/HvaFile.cpp",
    "src/gfx/VxlFile.cpp",
    "src/gfx/VxlNormals.cpp",
    "src/gfx/VoxelLight.cpp",
    "src/gfx/RemapTable.cpp",
    # ObjectSprite.cpp 不在这里：它要调 Dx12Renderer（体素烘焙 / 精灵上传），
    # 而 ra2core 是零系统依赖的验证台，不连 d3d12。
    "src/map/TheaterFile.cpp",
    "src/map/MapFile.cpp",
    "src/map/MapRenderer.cpp",
    "src/map/LatTiles.cpp",
    "src/data/Ini.cpp",
    "src/data/UnitModel.cpp",
    "src/core/GameVersion.cpp",
    "src/core/Subsystems.cpp",
]

# ra2view：DX12 素材查看器（Win32 窗口）
VIEW_SOURCES = [
    "src/viewer/ViewerMain.cpp",
    "src/io/FileSystem.cpp",
    "src/io/MixCrypto.cpp",
    "src/io/Lzo1x.cpp",
    "src/io/Format80.cpp",
    "src/gfx/Palette.cpp",
    "src/gfx/ShpFile.cpp",
    "src/gfx/TmpFile.cpp",
    "src/gfx/PcxFile.cpp",
    "src/gfx/HvaFile.cpp",
    "src/gfx/VxlFile.cpp",
    "src/gfx/VxlNormals.cpp",
    "src/gfx/VoxelLight.cpp",
    "src/gfx/RemapTable.cpp",
    "src/gfx/dx12/Dx12Renderer.cpp",
    "src/map/TheaterFile.cpp",
    "src/map/MapFile.cpp",
    "src/map/MapRenderer.cpp",
    "src/map/LatTiles.cpp",
    "src/data/Ini.cpp",
    "src/data/UnitModel.cpp",
    "src/core/GameVersion.cpp",
]

# ra2game：真正的游戏（打开就是原版那个界面）
GAME_SOURCES = [
    "src/game/GameMain.cpp",
    "src/game/AudioDevice.cpp",
    "src/game/GameShell.cpp",
    "src/game/SaveLoad.cpp",
    "src/game/World.cpp",
    "src/ai/PathFinder.cpp",
    "src/map/Map.cpp",
    "src/threading/TaskSystem.cpp",
    "src/io/FileSystem.cpp",
    "src/io/MixCrypto.cpp",
    "src/io/Lzo1x.cpp",
    "src/io/Format80.cpp",
    "src/gfx/Palette.cpp",
    "src/gfx/ShpFile.cpp",
    "src/gfx/TmpFile.cpp",
    "src/gfx/PcxFile.cpp",
    "src/gfx/HvaFile.cpp",
    "src/gfx/VxlFile.cpp",
    "src/gfx/VxlNormals.cpp",
    "src/gfx/VoxelLight.cpp",
    "src/gfx/RemapTable.cpp",
    "src/gfx/ObjectSprite.cpp",
    "src/gfx/dx12/Dx12Renderer.cpp",
    "src/map/TheaterFile.cpp",
    "src/map/MapFile.cpp",
    "src/map/MapRenderer.cpp",
    "src/map/LatTiles.cpp",
    "src/data/Ini.cpp",
    "src/data/UnitModel.cpp",
    "src/data/CsfFile.cpp",
    "src/core/GameVersion.cpp",
]

# 只有查看器需要这些库；ra2core 保持零系统依赖。
VIEW_LIBS = ["d3d12.lib", "dxgi.lib", "d3dcompiler.lib", "user32.lib", "gdi32.lib", "winmm.lib"]

CFLAGS = ["/nologo", "/std:c++17", "/EHsc", "/W3", "/O2", "/GL-", "/bigobj",
          "/D_CRT_SECURE_NO_WARNINGS", "/DUNICODE", "/D_UNICODE"]


def find_msvc() -> str:
    pats = [
        r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*",
        r"C:\Program Files (x86)\Microsoft Visual Studio\*\VC\Tools\MSVC\*",
    ]
    hits = []
    for p in pats:
        hits += glob.glob(p)
    hits = [h for h in hits if os.path.isdir(os.path.join(h, "bin", "Hostx64", "x64"))]
    if not hits:
        sys.exit("[x] 找不到 MSVC 工具链（VC/Tools/MSVC/*/bin/Hostx64/x64）")
    # 取版本号最大的（字符串排序对 14.xx.xxxxx 够用，先进位更长）
    hits.sort(key=lambda h: [int(x) if x.isdigit() else 0
                             for x in os.path.basename(h).split(".")])
    return hits[-1]


def find_sdk() -> tuple[str, str]:
    inc = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Include\*"))
    lib = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Lib\*"))
    if not inc or not lib:
        sys.exit("[x] 找不到 Windows 10 SDK")
    return inc[-1], lib[-1]


def build_env() -> dict:
    msvc = find_msvc()
    sdk_inc, sdk_lib = find_sdk()
    msvc_inc = os.path.join(msvc, "include")
    msvc_lib = os.path.join(msvc, "lib", "x64")
    bin_dir = os.path.join(msvc, "bin", "Hostx64", "x64")

    env = dict(os.environ)
    env["INCLUDE"] = os.pathsep.join([
        msvc_inc,
        os.path.join(sdk_inc, "ucrt"),
        os.path.join(sdk_inc, "um"),
        os.path.join(sdk_inc, "shared"),
        os.path.join(sdk_inc, "winrt"),
    ])
    env["LIB"] = os.pathsep.join([
        msvc_lib,
        os.path.join(sdk_lib, "ucrt", "x64"),
        os.path.join(sdk_lib, "um", "x64"),
    ])
    env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    return env


def compile_target(env, cl, name, sources, extra_libs=(), obj_dir=None) -> str:
    build = os.path.join(ROOT, "build")
    odir = obj_dir or build
    # 注意：cl 的 /Fo /Fe 必须写成 /Fo:xxx 形式；写成 "/Fo" "xxx" 两个参数时
    # cl 不会把第二个参数当路径，而是当成源文件，报 D9024。
    cmd = [cl] + CFLAGS + ["/I", os.path.join(ROOT, "src"),
                           "/Fo:" + odir + os.sep,
                           "/Fe:" + os.path.join(build, name)]
    cmd += [os.path.join(ROOT, s) for s in sources]
    if extra_libs:
        cmd.append("/link")
        cmd += list(extra_libs)
    print("[.] %s ... (%d 个源文件)" % (name, len(sources)))
    r = subprocess.run(cmd, cwd=ROOT, env=env)
    if r.returncode != 0:
        sys.exit("[x] 构建失败：" + name)
    return os.path.join(build, name)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--run", action="store_true")
    a = ap.parse_args()

    build = os.path.join(ROOT, "build")
    os.makedirs(build, exist_ok=True)
    vobj = os.path.join(build, "view")
    gobj = os.path.join(build, "game")
    os.makedirs(vobj, exist_ok=True)
    os.makedirs(gobj, exist_ok=True)
    if a.clean:
        for f in glob.glob(os.path.join(build, "*.obj")):
            os.remove(f)
        for f in glob.glob(os.path.join(vobj, "*.obj")):
            os.remove(f)
        for f in glob.glob(os.path.join(gobj, "*.obj")):
            os.remove(f)

    env = build_env()
    cl = os.path.join(find_msvc(), "bin", "Hostx64", "x64", "cl.exe")

    core = compile_target(env, cl, "ra2core.exe", CORE_SOURCES)
    # 查看器单独放一份 .obj：两个目标都编 FileSystem.cpp 等，
    # 同名 .obj 会互相覆盖，导致链接错版本。
    view = compile_target(env, cl, "ra2view.exe", VIEW_SOURCES, VIEW_LIBS, obj_dir=vobj)
    game = compile_target(env, cl, "ra2game.exe", GAME_SOURCES, VIEW_LIBS, obj_dir=gobj)

    print("[OK] " + core)
    print("[OK] " + view)
    print("[OK] " + game)
    if a.run:
        sys.exit(subprocess.run([core], cwd=ROOT).returncode)


if __name__ == "__main__":
    main()
