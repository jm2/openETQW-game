# SPDX-License-Identifier: MIT

project(
  'openETQW-game',
  ['c', 'cpp'],
  version: '1.5.0',
  meson_version: '>=1.6.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'b_vscrt=static_from_buildtype',
  ],
)

host_system = host_machine.system()
host_cpu_family = host_machine.cpu_family()

binary_arch = host_cpu_family
if host_cpu_family in ['x86_64', 'x64']
  binary_arch = 'x64'
elif host_cpu_family == 'x86'
  binary_arch = 'x86'
elif host_cpu_family == 'aarch64'
  binary_arch = 'arm64'
endif

game_target_name = 'game_' + binary_arch

py = find_program('python3', 'python', native: true, required: true)
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

threads_dep = dependency('threads', required: true)
m_dep = cpp.find_library('m', required: false)

if host_system == 'darwin'
  macos_deployment_target = get_option('macos_deployment_target')
  add_project_arguments(
    '-mmacosx-version-min=' + macos_deployment_target,
    language: ['c', 'cpp'],
  )
  add_project_link_arguments(
    '-mmacosx-version-min=' + macos_deployment_target,
    language: ['c', 'cpp'],
  )
endif

# Floating-point contraction determinism
foreach fp_arg : ['-ffp-contract=off']
  if cpp.has_argument(fp_arg) and cc.has_argument(fp_arg)
    add_project_arguments(fp_arg, language: ['c', 'cpp'])
    add_project_link_arguments(fp_arg, language: ['c', 'cpp'])
  endif
endforeach

root_inc = include_directories('.')
src_inc = include_directories('src')
idlib_inc = include_directories('src/idlib')
game_inc = include_directories('src/game')

shared_defines = [
  '-DSD_SDK_BUILD',
  '-DSD_PUBLIC_BUILD',
  '-DSD_USE_DRAWVERT_SIZE_32',
  '-DSD_USE_INDEX_SIZE_16',
]

if host_system == 'darwin'
  shared_defines += ['-DMACOS_X']
elif host_system == 'windows'
  shared_defines += [
    '-DWIN32',
    '-D_WINDOWS',
    '-DNOMINMAX',
    '-DWIN32_LEAN_AND_MEAN',
    '-D_CRT_SECURE_NO_WARNINGS',
    '-D_CRT_NONSTDC_NO_WARNINGS',
  ]
endif

shared_cpp_args = shared_defines
if cpp.get_argument_syntax() == 'msvc'
  add_project_arguments(
    '/EHsc',
    '/permissive-',
    # /permissive- implies /Zc:strictStrings; the SDK passes string literals to char* in hundreds of places
    '/Zc:strictStrings-',
    '/Zc:__cplusplus',
    '/utf-8',
    '/bigobj',
    language: ['c', 'cpp'],
  )
else
  shared_cpp_args += [
    '-fno-strict-aliasing',
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-invalid-offsetof',
    '-Wno-register',
  ]
  # SDK code relies on 'this == NULL' checks (e.g. idClass::Cast is 'return this ? ... : NULL')
  # and on null checks after dereferences; keep optimizing compilers from deleting them.
  if cpp.has_argument('-fno-delete-null-pointer-checks')
    shared_cpp_args += ['-fno-delete-null-pointer-checks']
  endif
endif

# Force-include precompiled header when use_pch is false
idlib_pch_args = []
game_pch_args = []
if not get_option('use_pch')
  if cpp.get_argument_syntax() == 'msvc'
    idlib_pch_args += ['/FI' + (meson.project_source_root() / 'src' / 'idlib' / 'precompiled.h')]
    game_pch_args += ['/FI' + (meson.project_source_root() / 'src' / 'game' / 'precompiled.h')]
  else
    idlib_pch_args += ['-include', meson.project_source_root() / 'src' / 'idlib' / 'precompiled.h']
    game_pch_args += ['-include', meson.project_source_root() / 'src' / 'game' / 'precompiled.h']
  endif
endif

# -----------------------------------------------------------------------------
# 1. Target: idLib (static)
# -----------------------------------------------------------------------------
idlib_sources = files(
  'src/idlib/BitMsg.cpp',
  'src/idlib/Callable.cpp',
  'src/idlib/Color.cpp',
  'src/idlib/Dict.cpp',
  'src/idlib/Heap.cpp',
  'src/idlib/ImagePacker.cpp',
  'src/idlib/LangDict.cpp',
  'src/idlib/Lib.cpp',
  'src/idlib/MapFile.cpp',
  'src/idlib/PoolAllocator.cpp',
  'src/idlib/Properties.cpp',
  'src/idlib/TextUtilities.cpp',
  'src/idlib/Timer.cpp',
  'src/idlib/bv/Bounds.cpp',
  'src/idlib/bv/Bounds2D.cpp',
  'src/idlib/bv/BoundsShort.cpp',
  'src/idlib/bv/Box.cpp',
  'src/idlib/bv/Frustum.cpp',
  'src/idlib/bv/Sphere.cpp',
  'src/idlib/geometry/JointTransform.cpp',
  'src/idlib/geometry/Surface.cpp',
  'src/idlib/geometry/Surface_Patch.cpp',
  'src/idlib/geometry/Surface_Polytope.cpp',
  'src/idlib/geometry/Surface_SweptSpline.cpp',
  'src/idlib/geometry/Surface_Traceable.cpp',
  'src/idlib/geometry/TraceModel.cpp',
  'src/idlib/geometry/TraceSurface.cpp',
  'src/idlib/geometry/Winding.cpp',
  'src/idlib/geometry/Winding2D.cpp',
  'src/idlib/hashing/CRC16.cpp',
  'src/idlib/hashing/CRC32.cpp',
  'src/idlib/hashing/CRC8.cpp',
  'src/idlib/hashing/Honeyman.cpp',
  'src/idlib/hashing/MD4.cpp',
  'src/idlib/hashing/MD5.cpp',
  'src/idlib/math/Angles.cpp',
  'src/idlib/math/Complex.cpp',
  'src/idlib/math/Lcp.cpp',
  'src/idlib/math/Mat3x4.cpp',
  'src/idlib/math/Math.cpp',
  'src/idlib/math/Matrix.cpp',
  'src/idlib/math/Ode.cpp',
  'src/idlib/math/Perlin.cpp',
  'src/idlib/math/Plane.cpp',
  'src/idlib/math/Pluecker.cpp',
  'src/idlib/math/Polynomial.cpp',
  'src/idlib/math/Quat.cpp',
  'src/idlib/math/Radians.cpp',
  'src/idlib/math/Random.cpp',
  'src/idlib/math/Rotation.cpp',
  'src/idlib/math/Simd.cpp',
  'src/idlib/math/Simd_Generic.cpp',
  'src/idlib/math/Vector.cpp',
  'src/idlib/text/Base64.cpp',
  'src/idlib/text/CmdArgs.cpp',
  'src/idlib/text/Lexer.cpp',
  'src/idlib/text/LexerBinary.cpp',
  'src/idlib/text/Parser.cpp',
  'src/idlib/text/Str.cpp',
  'src/idlib/text/StrBuilder.cpp',
  'src/idlib/text/StrSimple.cpp',
  'src/idlib/text/Token.cpp',
  'src/idlib/text/UTF8.cpp',
  'src/idlib/text/WLexer.cpp',
  'src/idlib/text/WStr.cpp',
  'src/idlib/text/WToken.cpp',
  'src/idlib/threading/Lock.cpp',
  'src/idlib/threading/Signal.cpp',
  'src/idlib/threading/Thread.cpp',
)

if host_system in ['darwin', 'linux']
  idlib_sources += files(
    'src/idlib/sys/threading/SysLock_posix.cpp',
    'src/idlib/sys/threading/SysSignal_posix.cpp',
    'src/idlib/sys/threading/SysThread_posix.cpp',
  )
elif host_system == 'windows'
  idlib_sources += files(
    'src/idlib/sys/threading/SysLock_win.cpp',
    'src/idlib/sys/threading/SysSignal_win.cpp',
    'src/idlib/sys/threading/SysThread_win.cpp',
  )
endif

idlib_lib = static_library(
  'idLib',
  idlib_sources,
  include_directories: [root_inc, src_inc, idlib_inc],
  cpp_args: shared_cpp_args + idlib_pch_args + ['-D__IDLIB__', '-D_LIB'],
  dependencies: [threads_dep],
  install: false,
)

# -----------------------------------------------------------------------------
# 2. Target: libGameDecl (static)
# -----------------------------------------------------------------------------
gamedecl_sources_res = run_command(
  py,
  '-c',
  '''
import pathlib, sys
root = pathlib.Path(sys.argv[1]) / "src" / "game" / "decls"
sources = [p.as_posix() for p in root.glob("*.cpp")]
sources.append((pathlib.Path(sys.argv[1]) / "src" / "decllib" / "DeclType.cpp").as_posix())
for s in sorted(sources):
    print(s)
''',
  meson.project_source_root(),
  check: true,
)

gamedecl_source_paths = gamedecl_sources_res.stdout().strip().split('\n')

libgamedecl_lib = static_library(
  'libGameDecl',
  gamedecl_source_paths,
  include_directories: [root_inc, src_inc, game_inc],
  cpp_args: shared_cpp_args + ['-D_LIB'],
  install: false,
)

# -----------------------------------------------------------------------------
# 3. Target: game_<arch> (shared library, e.g. game_arm64.dylib)
# -----------------------------------------------------------------------------
game_sources_res = run_command(
  py,
  '-c',
  '''
import pathlib, sys
root = pathlib.Path(sys.argv[1]) / "src" / "game"
excludes = {
    "Callbacks.cpp", "SaveGame.cpp",
    "GameRules_DeathMatch.cpp", "GameRules_Multiplayer.cpp",
    "ClientProjectile.cpp"
}
sources = []
for p in root.rglob("*.cpp"):
    if "decls" in p.parts:
        continue
    if p.name in excludes:
        continue
    sources.append(p.as_posix())
sources.append((pathlib.Path(sys.argv[1]) / "src" / "framework" / "BuildVersion.cpp").as_posix())
for s in sorted(sources):
    print(s)
''',
  meson.project_source_root(),
  check: true,
)

game_source_paths = game_sources_res.stdout().strip().split('\n')

game_link_args = []
game_link_depends = []
if host_system == 'darwin'
  exp_file = meson.project_source_root() / 'tools' / 'build' / 'darwin_game_module.exp'
  game_link_args += ['-Wl,-exported_symbols_list,' + exp_file]
  game_link_depends += [exp_file]
elif host_system == 'linux'
  map_file = meson.project_source_root() / 'tools' / 'build' / 'linux_game_module.map'
  game_link_args += ['-Wl,--version-script=' + map_file]
  game_link_depends += [map_file]
elif host_system == 'windows'
  game_link_args += ['/DEF:' + (meson.project_source_root() / 'src' / 'game' / 'Game.def')]
endif

game_module = shared_library(
  game_target_name,
  game_source_paths,
  name_prefix: '',
  include_directories: [root_inc, src_inc, game_inc],
  cpp_args: shared_cpp_args + game_pch_args + ['-DGAME_DLL'],
  dependencies: [threads_dep, m_dep],
  link_with: [libgamedecl_lib, idlib_lib],
  link_args: game_link_args,
  link_depends: game_link_depends,
  install: true,
)
