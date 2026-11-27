function(phantomx_apply_warnings target)
  if(NOT PHANTOMX_ENABLE_WARNINGS)
    return()
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      /permissive-
      /Zc:__cplusplus
      /Zc:preprocessor
    )
    if(PHANTOMX_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wformat=2
    )
    if(PHANTOMX_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
