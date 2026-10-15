# ---------------------------------------------------------------------------
# 探测 clang 资源头目录（内置头 stddef.h 所在），结果写入
# MESSION_CLANG_RESOURCE_DIR_RESOLVED。
#
# 为什么需要它：MHeaderTool 用 libtooling 解析源码，而 libtooling 不经过 clang
# driver——CompilerInvocation 的默认 -resource-dir 是按可执行文件路径推测的
# （<exe>/../../lib/clang/<major>）。clang 装在发行版标准位置时恰好命中；装在非
# 标准前缀（仓库内 Bin/、容器内拼装树、回归验证树）时必然探测失败。缺 stddef.h
# 会让每个 TU 解析失败成 error 节点，函数体遍历不到，await 生成物
# （*_AwaitImpl.mgenerated.cpp / *_AwaitStateMachine.h）一个都不产；而
# *AsyncFrames.h 不依赖函数体、照常生成，所以缺陷要到 add_executable 报
# "找不到源文件"才暴露。
#
# 为什么不能只拼一条路径：各发行版布局不同。Debian/Ubuntu 的资源头在
# ${LLVM_LIBRARY_DIRS}/clang/<major>；RHEL/Fedora 由独立包
# clang-resource-filesystem 提供，落在 /usr/lib/clang/<major>，与
# LLVM_LIBRARY_DIRS（/usr/lib64）前缀不同。因此按序试候选，取第一个含
# include/stddef.h 的：
#   1. -DMESSION_CLANG_RESOURCE_DIR=...（用户显式指定，优先级最高）
#   2. 同版本 clang driver 的 -print-resource-dir（最权威，且能消解资源目录用
#      minor 号还是 major 号的歧义）
#   3. ${LLVM_LIBRARY_DIRS}/clang/<major>
#   4. -DMESSION_CLANG_RESOURCE_DIR_EXTRA_PREFIXES 中每个前缀 + /clang/<major>
#      （默认 /usr/lib;/usr/lib64;/usr/local/lib）
#
# 输入：LLVM_VERSION_MAJOR、LLVM_LIBRARY_DIRS、LLVM_TOOLS_BINARY_DIR
#       MESSION_CLANG_RESOURCE_DIR、MESSION_CLANG_RESOURCE_DIR_EXTRA_PREFIXES（均可空）
# 输出：MESSION_CLANG_RESOURCE_DIR_RESOLVED——未命中时为空串，交由 clang 自身按
#       老逻辑探测（标准安装路径下仍能工作）
# ---------------------------------------------------------------------------

set(MESSION_CLANG_RESOURCE_DIR
    "" CACHE PATH "覆盖 clang 资源头目录（含 include/stddef.h）；留空则自动探测")

set(MESSION_CLANG_RESOURCE_DIR_EXTRA_PREFIXES
    "/usr/lib;/usr/lib64;/usr/local/lib" CACHE STRING
    "自动探测 clang 资源头目录时的兜底前缀（用于非标准发行版布局）")

# 候选校验：必须是 clang 资源根目录——内置头在其下 include/ 里。
# 注意 -resource-dir 的语义是"资源根"，不能指向 .../include，否则 clang 会去
# <dir>/include/ 下再找一层，路径翻倍成 include/include/stddef.h 而全部缺失。
function(_mht_accept_resource_dir _candidate _out_var)
    if(NOT _candidate)
        return()
    endif()
    if(EXISTS "${_candidate}/include/stddef.h")
        set(${_out_var}
            "${_candidate}"
            PARENT_SCOPE)
    endif()
endfunction()

# 问同版本 clang driver 要资源目录。只信后缀与 LLVM_VERSION_MAJOR 对得上的结果：
# driver 可能是另一套 clang（例如 /usr/bin/clang 是 17，而 Clang_DIR 指向 19）。
function(_mht_query_clang_resource_dir _out_var)
    set(_mht_driver "")
    foreach(_mht_cand "${LLVM_TOOLS_BINARY_DIR}/clang-${LLVM_VERSION_MAJOR}"
                      "${LLVM_TOOLS_BINARY_DIR}/clang")
        if(EXISTS "${_mht_cand}")
            set(_mht_driver "${_mht_cand}")
            break()
        endif()
    endforeach()
    if(NOT _mht_driver)
        return()
    endif()
    execute_process(
        COMMAND "${_mht_driver}" -print-resource-dir
        OUTPUT_VARIABLE _mht_printed
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _mht_rc
        TIMEOUT 30)
    if(NOT _mht_rc EQUAL 0 OR NOT _mht_printed)
        return()
    endif()
    # 接受 <major> 或 <major>.<minor>[.<patch>]，拒绝 190 / 2 这类碰巧的前缀。
    get_filename_component(_mht_leaf "${_mht_printed}" NAME)
    if(NOT _mht_leaf MATCHES "^${LLVM_VERSION_MAJOR}(\\.[0-9]+)*$")
        return()
    endif()
    set(${_out_var}
        "${_mht_printed}"
        PARENT_SCOPE)
endfunction()

set(MESSION_CLANG_RESOURCE_DIR_RESOLVED "")

# 1) 用户显式指定
_mht_accept_resource_dir("${MESSION_CLANG_RESOURCE_DIR}"
                         MESSION_CLANG_RESOURCE_DIR_RESOLVED)

# 2) 同版本 clang driver
if(NOT MESSION_CLANG_RESOURCE_DIR_RESOLVED)
    _mht_query_clang_resource_dir(_mht_queried)
    _mht_accept_resource_dir("${_mht_queried}"
                             MESSION_CLANG_RESOURCE_DIR_RESOLVED)
endif()

# 3)+4) 目录前缀：LLVM_LIBRARY_DIRS 优先，其后是发行版兜底前缀
if(NOT MESSION_CLANG_RESOURCE_DIR_RESOLVED)
    foreach(_mht_prefix "${LLVM_LIBRARY_DIRS}"
                        ${MESSION_CLANG_RESOURCE_DIR_EXTRA_PREFIXES})
        _mht_accept_resource_dir("${_mht_prefix}/clang/${LLVM_VERSION_MAJOR}"
                                 MESSION_CLANG_RESOURCE_DIR_RESOLVED)
        if(MESSION_CLANG_RESOURCE_DIR_RESOLVED)
            break()
        endif()
    endforeach()
    unset(_mht_prefix)
endif()

if(MESSION_CLANG_RESOURCE_DIR_RESOLVED)
    message(STATUS
        "MHeaderTool: clang resource dir = ${MESSION_CLANG_RESOURCE_DIR_RESOLVED}")
else()
    message(WARNING
        "MHeaderTool: 未找到 clang 资源头目录（缺 include/stddef.h），"
        "将退回 clang 默认按可执行文件路径推测的 -resource-dir。"
        "若 clang 装在非标准前缀下会导致 await 生成物静默缺失。"
        "请确认已安装内置头包（Debian/Ubuntu: libclang-common-<版本>-dev；"
        "RHEL/Fedora: clang-resource-filesystem），"
        "或用 -DMESSION_CLANG_RESOURCE_DIR=<含 include/stddef.h 的目录> 显式指定。")
endif()
