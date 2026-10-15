# MHeaderTool configure 期引导工程——只编一个宿主可执行文件 bootstrap_mht。
#
# 谁在用它:顶层 CMakeLists.txt 在 Bin/MHeaderTool 不存在时(即全新检出)调用本工程,
# 编出一个临时宿主工具来跑 codegen、产出 Build/Generated/**。产物由发布流程后续
# 覆盖到 Bin/MHeaderTool;本次引导只求"让第一次 configure 能拿到生成代码"。
#
# 为什么是独立小工程,而不是顶层 CMakeLists 里拼一条编译器命令行:
#   1. libtooling 的链接必须走 Clang/LLVM 的导入目标。Ubuntu 的 LLVM 19 只提供
#      libclang-cpp.so.19.1,没有 libclang-cpp.so 别名,手写 -lclang-cpp 会链接失败;
#      直接写死 .so.19.1 又跟着 LLVM 版本号漂移。find_package(Clang CONFIG) 一次解决。
#   2. 顶层已经跑过 find_package(Clang 19.1.7 REQUIRED CONFIG),这里通过 MHT_CLANG_DIR
#      复用同一个包,保证引导工具与正式构建用的是同一套 Clang 头文件与库。
#   3. 本工程用**默认生成器**(与顶层生成器无关),因此全新机器上 Ninja 不在 PATH
#      也不影响 bootstrap——这正是 Scripts/build_from_scratch.sh 依赖的性质。
#
# 调用方式(由顶层 CMakeLists.txt 发起):
#   cmake -S <本目录> -B <引导构建目录> -DMHT_SOURCE_ROOT=<仓库>/Source ...
#   cmake --build <引导构建目录> --target bootstrap_mht --parallel <N>
# 产物:<引导构建目录>/[<配置名>/]bootstrap_mht
cmake_minimum_required(VERSION 3.15)

project(MHeaderToolBootstrap LANGUAGES CXX)

# 与顶层一致:项目整体是 C++17。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_LIST_DIR}/../Sources.cmake")

# 顶层会显式传入 Source 根;单独使用本工程时退回仓库默认布局。
if(NOT MHT_SOURCE_ROOT)
    get_filename_component(MHT_SOURCE_ROOT "${MHT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()

# 复用顶层 find_package(Clang) 的结果(Clang_DIR 是 cache 变量),避免二次探测到别的版本。
if(MHT_CLANG_DIR)
    set(Clang_DIR "${MHT_CLANG_DIR}" CACHE PATH "Clang config package location" FORCE)
endif()
# 版本同样不写死：引导工具与正式构建必须是同一套 Clang（上层已把 Clang_DIR 传进来），
# 写死 19.1.7 会让装有其它 clang 版本的机器 configure 期直接失败。
find_package(Clang REQUIRED CONFIG)

# clang 资源头目录：与顶层 mht_lib 共用同一份探测逻辑，保证引导工具与正式构建用同
# 一个 -resource-dir（顶层通常已把探测结果作为 MESSION_CLANG_RESOURCE_DIR 传进来）。
include("${CMAKE_CURRENT_LIST_DIR}/../DetectClangResourceDir.cmake")

# 引导工具只用 Core 的头文件与 inline 实现,不需要重编 mession_core 的实现 TU
# (顶层在 configure 期也还没有可用的 mession_core 目标)。留一个 INTERFACE 占位,
# 使共享的 Sources.cmake 之外若追加 target_link_libraries(mession_core) 也不会报错。
if(NOT TARGET mession_core)
    add_library(mession_core INTERFACE)
endif()

add_executable(bootstrap_mht ${MHT_ALL_SOURCES})

target_include_directories(bootstrap_mht PRIVATE
    "${MHT_SOURCE_DIR}"
    "${MHT_SOURCE_ROOT}"
)

# CLANG_INCLUDE_DIRS 指向发行版前缀(在 Debian/Ubuntu 上是 /usr/include,里面没有
# clang/ 目录),libtooling 的头文件实际在 LLVM_INCLUDE_DIRS 下。
if(LLVM_INCLUDE_DIRS)
    target_include_directories(bootstrap_mht PRIVATE ${LLVM_INCLUDE_DIRS})
endif()

# clang-cpp 是 Clang 包导出的"整个 clang + LLVM 共享库"目标。若某个发行版的包没导出它,
# 退回到与 mht_lib 相同的细粒度目标列表。
if(TARGET clang-cpp)
    target_link_libraries(bootstrap_mht PRIVATE clang-cpp)
else()
    target_link_libraries(bootstrap_mht PRIVATE
        clangTooling clangFrontend clangFrontendTool clangSerialization clangParse
        clangAST clangASTMatchers clangBasic clangSema clangLex clangEdit clangDriver
        clangRewrite clangAnalysis clangDynamicASTMatchers
    )
endif()

# Ubuntu 的 Clang 包导出 clang-cpp 时不把 LLVM 带上链接行(CLANG_LINK_CLANG_DYLIB=ON
# 的语义就是"clang 库都缩成 clang-cpp 这一个"),结果 libclang-cpp 里对 llvm::* 的
# 引用没人提供,链接期报 "DSO missing from command line"。显式补上 LLVM 共享库。
if(TARGET LLVM)
    target_link_libraries(bootstrap_mht PRIVATE LLVM)
elseif(LLVM_LIBRARIES)
    target_link_libraries(bootstrap_mht PRIVATE ${LLVM_LIBRARIES})
endif()

# 复刻顶层 PCH。顶层用 target_precompile_headers 前置 Source/Common/Runtime/MLib.h,
# 而 Core/Types.h 等头文件并不自包含(依赖 MLib.h 先引入的公共头),所以引导编译
# 也必须前置同一份头,否则会以一堆"未知类型"编译失败。这里用 -include 而不是 PCH:
# 单个可执行文件、一次编译,省掉 PCH 的额外开销与路径约束。
set(MHT_PCH_HEADER "${MHT_SOURCE_ROOT}/Common/Runtime/MLib.h")
if(EXISTS "${MHT_PCH_HEADER}")
    if(MSVC)
        target_compile_options(bootstrap_mht PRIVATE /FI "${MHT_PCH_HEADER}")
    else()
        target_compile_options(bootstrap_mht PRIVATE -include "${MHT_PCH_HEADER}")
    endif()
    message(STATUS "MHeaderTool bootstrap: forcing include ${MHT_PCH_HEADER}")
else()
    message(WARNING "MHeaderTool bootstrap: PCH header not found at ${MHT_PCH_HEADER}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bootstrap_mht PRIVATE -Wno-unused-parameter)
endif()

# 资源头目录宏：ASTPipeline.cpp 据此给 libtooling 显式传 -resource-dir
# （探测逻辑见 ../DetectClangResourceDir.cmake）。
if(MESSION_CLANG_RESOURCE_DIR_RESOLVED)
    target_compile_definitions(bootstrap_mht PRIVATE
        MESSION_CLANG_RESOURCE_DIR="${MESSION_CLANG_RESOURCE_DIR_RESOLVED}"
    )
endif()
