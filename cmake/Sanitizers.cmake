# Sanitizer wiring. Apply with bencode_apply_sanitizers(<target>).
#
# Mutual exclusion is enforced at configure time -- documented in §9 of the
# project-principles reference. MSan is Clang-only.

function(bencode_apply_sanitizers target)
    set(_flags "")

    if(BENCODE_ENABLE_ASAN AND BENCODE_ENABLE_TSAN)
        message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously")
    endif()
    if(BENCODE_ENABLE_MSAN AND (BENCODE_ENABLE_ASAN OR BENCODE_ENABLE_TSAN))
        message(FATAL_ERROR "MSan is incompatible with ASan/TSan")
    endif()

    if(NOT MSVC)
        if(BENCODE_ENABLE_ASAN)
            list(APPEND _flags -fsanitize=address -fno-omit-frame-pointer)
        endif()
        if(BENCODE_ENABLE_UBSAN)
            list(APPEND _flags -fsanitize=undefined -fno-sanitize-recover=undefined)
        endif()
        if(BENCODE_ENABLE_TSAN)
            list(APPEND _flags -fsanitize=thread)
        endif()
        if(BENCODE_ENABLE_MSAN)
            if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
                message(FATAL_ERROR "MSan is only supported by Clang")
            endif()
            list(APPEND _flags
                -fsanitize=memory
                -fsanitize-memory-track-origins=2
                -fno-omit-frame-pointer
            )
        endif()
        if(BENCODE_ENABLE_COVERAGE)
            list(APPEND _flags --coverage)
        endif()
    elseif(BENCODE_ENABLE_ASAN)
        list(APPEND _flags /fsanitize=address)
    endif()

    if(_flags)
        target_compile_options(${target} PRIVATE ${_flags})
        target_link_options(${target} PRIVATE ${_flags})
    endif()
endfunction()
