# Project warning flags, applied via an INTERFACE target.
# Usage: nexuspc_set_project_warnings(<target> <warnings_as_errors:BOOL>)

function(nexuspc_set_project_warnings target warnings_as_errors)
    set(msvc_warnings
        /W4
        /permissive-
        /w14242 /w14254 /w14263 /w14265 /w14287
        /w14296 /w14311 /w14545 /w14546 /w14547
        /w14549 /w14555 /w14619 /w14640 /w14826
        /w14905 /w14906 /w14928
        /EHsc)

    set(gnu_clang_warnings
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)

    if(MSVC)
        set(warnings ${msvc_warnings})
        if(warnings_as_errors)
            list(APPEND warnings /WX)
        endif()
    else()
        set(warnings ${gnu_clang_warnings})
        if(warnings_as_errors)
            list(APPEND warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} INTERFACE ${warnings})
endfunction()
