# Warning and sanitizer flags shared by every nodesort target.

function(nodesort_configure_target target)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(NODESORT_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
      -Wstrict-prototypes -Wmissing-prototypes -Wcast-align -Wformat=2)
    if(NODESORT_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  if(NODESORT_SANITIZE)
    if(MSVC)
      target_compile_options(${target} PRIVATE /fsanitize=${NODESORT_SANITIZE})
    else()
      target_compile_options(${target} PRIVATE
        -fsanitize=${NODESORT_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
      target_link_options(${target} PRIVATE -fsanitize=${NODESORT_SANITIZE})
    endif()
  endif()
endfunction()
