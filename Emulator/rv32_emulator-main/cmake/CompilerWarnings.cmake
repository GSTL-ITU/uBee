# Warning configuration shared by every target in the project.
#
# The important one is -Werror=switch. The ISA is described once in
# instr_table.def, which generates the InstrId enum; the executor switches over
# that enum with no `default:` label. Adding an instruction to the table then
# *fails the build* until the executor handles it. That gives us the
# exhaustiveness guarantee a macro-generated dispatch table would provide,
# without putting execution semantics inside a macro.

function(rv_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        # Escalated to errors: each of these has bitten emulator projects in
        # ways that surface as silently wrong execution rather than a crash.
        -Werror=switch
        -Werror=return-type
        -Werror=implicit-fallthrough
    )

    if(RV32_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    if(RV32_SANITIZE)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
