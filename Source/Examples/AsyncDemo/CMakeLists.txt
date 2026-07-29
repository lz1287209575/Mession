# AsyncDemo - end-to-end C++17 async example (P4 wrap).
#
# Self-contained CMake for the AsyncDemo example. Declares only the link
# dependencies that AsyncDemo's source actually touches:
#   - mession_common   (provides SFutureResult<T>, MPromise, MAsync.h,
#                       AwaitMacros.h, FAppError, generated mession_common
#                       infra — all AsyncDemo.cpp / AsyncDemoImpl.cpp need)
#
# Notably does NOT link:
#   - mession_netdriver — would pull in MProperty/MClass vtables from the
#     shared generated group. AsyncDemo's generated header
#     AsyncDemo_FreeAsyncFrames.mgenerated.h is header-only and does not
#     instantiate those vtables. Verified empirically in Task 24 audit:
#     stripping mession_netdriver + mession_attach_generated_groups still
#     produces a working AsyncDemo binary (prints "AsyncDemo: result = 43").
#   - mession_attach_generated_groups — same reason. AsyncDemo only needs
#     its own generated header (header-only), not the shared group's .o files.
#
# Parent must have defined before add_subdirectory():
#   - mession_common (target)
#   - mession_reflection_codegen (custom target — runs MHeaderTool)
#   - MESSION_GENERATED_DIR (variable — where AsyncDemo_FreeAsyncFrames.mgenerated.h lands)
#   - configure_mession_compile_options (function)

# AsyncDemo: plain executable that drives ComputeAsync -> ChainAsync -> Wait().
add_executable(AsyncDemo
    AsyncDemo.cpp
)
target_include_directories(AsyncDemo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
)
target_link_libraries(AsyncDemo
    PRIVATE
        mession_common
)
configure_mession_compile_options(AsyncDemo)
add_dependencies(AsyncDemo mession_reflection_codegen)

# AsyncDemoTest: smoke coverage for the same scenario via TestHarness.h
# assertions. Links the impl TU (without main()) so it doesn't collide with
# AsyncDemo.cpp's main().
add_executable(AsyncDemoTest
    AsyncDemoImpl.cpp
    Tests/main.cpp
)
target_include_directories(AsyncDemoTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Source
        ${MESSION_GENERATED_DIR}
)
target_link_libraries(AsyncDemoTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(AsyncDemoTest)
add_dependencies(AsyncDemoTest mession_reflection_codegen)
