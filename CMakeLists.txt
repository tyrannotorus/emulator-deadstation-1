cmake_minimum_required(VERSION 3.20)
# Deadstation 1: PCSX ReARMed (core/) for the TrimUI Brick (NextUI, tg5040). Brick only: build with
# shell/brick/build.sh, which passes shell/brick/tg5040.cmake as the toolchain file.
project(deadstation VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(COMMAND git rev-parse --short=8 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE DEADSTATION_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(DEADSTATION_VERSION "${PROJECT_VERSION}-${DEADSTATION_GIT_HASH}")

# --- the core: PCSX ReARMed's emulation, its libretro interface statically linked ------------------
# Sources and flags as upstream's Makefile.libretro builds them for aarch64 (ari64 dynarec with its
# compiler thread, NEON GPU in its SIMD build, threaded GPU, SPU and CD-ROM, CHD through libchdr),
# plus the libretro-common files a static build expects its frontend to bring.
set(C core)
set(LCHDR ${C}/deps/libchdr)
add_library(pcsx_core STATIC
		${C}/libpcsxcore/cdriso.c
		${C}/libpcsxcore/cdrom.c
		${C}/libpcsxcore/cdrom-async.c
		${C}/libpcsxcore/cheat.c
		${C}/libpcsxcore/database.c
		${C}/libpcsxcore/decode_xa.c
		${C}/libpcsxcore/mdec.c
		${C}/libpcsxcore/misc.c
		${C}/libpcsxcore/plugins.c
		${C}/libpcsxcore/ppf.c
		${C}/libpcsxcore/psxbios.c
		${C}/libpcsxcore/psxcommon.c
		${C}/libpcsxcore/psxcounters.c
		${C}/libpcsxcore/psxdma.c
		${C}/libpcsxcore/psxhw.c
		${C}/libpcsxcore/psxinterpreter.c
		${C}/libpcsxcore/psxmem.c
		${C}/libpcsxcore/psxevents.c
		${C}/libpcsxcore/r3000a.c
		${C}/libpcsxcore/sio.c
		${C}/libpcsxcore/spu.c
		${C}/libpcsxcore/gpu.c
		${C}/libpcsxcore/pad.c
		${C}/libpcsxcore/gte.c
		${C}/libpcsxcore/gte_nf.c
		${C}/libpcsxcore/gte_divider.c
		${C}/libpcsxcore/gte_arm64.S
		${C}/libpcsxcore/gte_nf_arm64.S
		${C}/libpcsxcore/new_dynarec/new_dynarec.c
		${C}/libpcsxcore/new_dynarec/pcsxmem.c
		${C}/libpcsxcore/new_dynarec/linkage_arm64.S
		${C}/libpcsxcore/new_dynarec/emu_if.c
		${C}/plugins/dfsound/dma.c
		${C}/plugins/dfsound/freeze.c
		${C}/plugins/dfsound/registers.c
		${C}/plugins/dfsound/spu.c
		${C}/plugins/dfsound/out.c
		${C}/plugins/dfsound/nullsnd.c
		${C}/plugins/gpulib/gpu.c
		${C}/plugins/gpulib/vout_pl.c
		${C}/plugins/gpulib/prim.c
		${C}/plugins/gpulib/gpu_async.c
		${C}/plugins/gpu_neon/psx_gpu_if.c
		${C}/plugins/gpu_neon/psx_gpu/psx_gpu_simd.c
		${LCHDR}/src/libchdr_bitstream.c
		${LCHDR}/src/libchdr_cdrom.c
		${LCHDR}/src/libchdr_chd.c
		${LCHDR}/src/libchdr_codec_cdfl.c
		${LCHDR}/src/libchdr_codec_cdlz.c
		${LCHDR}/src/libchdr_codec_cdzl.c
		${LCHDR}/src/libchdr_codec_cdzs.c
		${LCHDR}/src/libchdr_codec_flac.c
		${LCHDR}/src/libchdr_codec_huff.c
		${LCHDR}/src/libchdr_codec_lzma.c
		${LCHDR}/src/libchdr_codec_zlib.c
		${LCHDR}/src/libchdr_codec_zstd.c
		${LCHDR}/src/libchdr_flac.c
		${LCHDR}/src/libchdr_huffman.c
		${LCHDR}/deps/lzma-26.02/src/LzmaDec.c
		${LCHDR}/deps/lzma-26.02/Asm/arm64/LzmaDecOpt.S
		${LCHDR}/deps/zstd-1.5.7/zstddeclib.c
		${LCHDR}/deps/miniz-3.1.2/miniz.c
		${C}/deps/libretro-common/features/features_cpu.c
		${C}/deps/libretro-common/compat/compat_strl.c
		${C}/deps/libretro-common/file/file_path.c
		${C}/deps/libretro-common/file/file_path_io.c
		${C}/deps/libretro-common/string/stdstring.c
		${C}/deps/libretro-common/string/rstrtod.c
		${C}/deps/libretro-common/vfs/vfs_implementation.c
		${C}/frontend/cspace.c
		${C}/frontend/pcsxr-threads.c
		${C}/frontend/plugin.c
		${C}/frontend/libretro.c
		${C}/frontend/main.c)
target_compile_definitions(pcsx_core PRIVATE
		GIT_VERSION="${DEADSTATION_GIT_HASH}" NDEBUG P_HAVE_MMAP=1 P_HAVE_POSIX_MEMALIGN=1 DISABLE_MEM_LUTS=0
		GPU_NEON HAVE_CHD USE_MINIZ HAVE_LIBRETRO NO_FRONTEND _FILE_OFFSET_BITS=64)
# revision.h: upstream's Makefile writes it from git describe (the save states' header carries it)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/revision.h CONTENT "#define REV \"${DEADSTATION_GIT_HASH}\"\n")
target_include_directories(pcsx_core PRIVATE ${C}/include ${LCHDR}/include ${LCHDR}/deps/miniz-3.1.2
		${C}/deps/libretro-common/include ${CMAKE_BINARY_DIR}/generated)
target_include_directories(pcsx_core PUBLIC ${C}/deps/libretro-common/include)
target_compile_options(pcsx_core PRIVATE -O3 -ffast-math -Wall -Wno-unused-function)
set_source_files_properties(${C}/libpcsxcore/cdrom-async.c PROPERTIES COMPILE_DEFINITIONS USE_ASYNC_CDROM)
set_source_files_properties(${C}/libpcsxcore/misc.c PROPERTIES COMPILE_DEFINITIONS BUILTIN_GPU=neon)
set_source_files_properties(${C}/libpcsxcore/psxbios.c PROPERTIES COMPILE_OPTIONS -Wno-nonnull)
# upstream: -O2 for the dynarec ("less bloat")
set_source_files_properties(${C}/libpcsxcore/new_dynarec/new_dynarec.c PROPERTIES COMPILE_DEFINITIONS NDRC_THREAD COMPILE_OPTIONS -O2)
set_source_files_properties(${C}/libpcsxcore/new_dynarec/emu_if.c PROPERTIES COMPILE_DEFINITIONS NDRC_THREAD)
file(GLOB _dfsound ${C}/plugins/dfsound/*.c)
set_source_files_properties(${_dfsound} PROPERTIES COMPILE_DEFINITIONS USE_ASYNC_SPU)
file(GLOB _gpulib ${C}/plugins/gpulib/*.c)
set_source_files_properties(${_gpulib} PROPERTIES COMPILE_DEFINITIONS USE_ASYNC_GPU)
set_source_files_properties(${C}/plugins/gpu_neon/psx_gpu_if.c PROPERTIES
		COMPILE_DEFINITIONS "NEON_BUILD;TEXTURE_CACHE_4BPP;TEXTURE_CACHE_8BPP;SIMD_BUILD")
set_source_files_properties(${C}/plugins/gpu_neon/psx_gpu/psx_gpu_simd.c PROPERTIES COMPILE_DEFINITIONS SIMD_BUILD)
file(GLOB _lchdr ${LCHDR}/src/*.c)
set_source_files_properties(${_lchdr} PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/${LCHDR}/deps/lzma-26.02/include
		COMPILE_OPTIONS "-Wno-unused")
set_source_files_properties(${LCHDR}/deps/lzma-26.02/src/LzmaDec.c PROPERTIES COMPILE_DEFINITIONS Z7_LZMA_DEC_OPT)
set_source_files_properties(${LCHDR}/deps/miniz-3.1.2/miniz.c PROPERTIES
		COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_DEFLATE_APIS;MINIZ_NO_ARCHIVE_APIS")
set_source_files_properties(${C}/frontend/libretro.c PROPERTIES
		COMPILE_DEFINITIONS "USE_ASYNC_CDROM;USE_ASYNC_GPU;USE_ASYNC_SPU;NDRC_THREAD")
set_source_files_properties(${C}/frontend/main.c PROPERTIES COMPILE_DEFINITIONS "HAVE_RTHREADS;BUILTIN_GPU=neon")
target_link_libraries(pcsx_core PUBLIC m dl pthread)

# --- Deadstation: the menu library (shared with the recomp modules) and the frontend that runs the core
add_subdirectory(frontend)
add_subdirectory(core/deadstation)
