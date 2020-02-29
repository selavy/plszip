# from: https://github.com/lefticus/cpp_starter_project/blob/master/cmake/Sanitizers.cmake

function(enable_sanitizers project_name)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL
                                             "Clang")
    option(ENABLE_COVERAGE "Enable coverage reporting for gcc/clang" FALSE)

    if(ENABLE_COVERAGE)
      target_compile_options(project_options INTERFACE --coverage -O0 -g)
      target_link_libraries(project_options INTERFACE --coverage)
    endif()

    set(SANITIZERS "")

    option(ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" TRUE)
    if(ENABLE_SANITIZER_ADDRESS)
      list(APPEND SANITIZERS "address")
    endif()

    option(ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" FALSE)
    if(ENABLE_SANITIZER_MEMORY)
      list(APPEND SANITIZERS "memory")
    endif()

    option(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
           "Enable undefined behavior sanitizer" TRUE)
    if(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR)
      list(APPEND SANITIZERS "undefined")
    endif()

    option(ENABLE_SANITIZER_THREAD "Enable thread sanitizer" FALSE)
    if(ENABLE_SANITIZER_THREAD)
      list(APPEND SANITIZERS "thread")
    endif()

	# NOTE: implementation of list(JOIN <list> <glue> <output>) since it
    # was added in cmake 3.12
    # list(JOIN SANITIZERS "," LIST_OF_SANITIZERS)
	# string(JOIN SANITIZERS "," LIST_OF_SANITIZERS)

	list(LENGTH SANITIZERS NUM_SANITIZERS)
	set(LIST_OF_SANITIZERS "")
	if(${NUM_SANITIZERS} GREATER 0)
		list(GET SANITIZERS 0 LIST_OF_SANITIZERS)
		list(REMOVE_AT SANITIZERS 0)
		foreach(sanitizer ${SANITIZERS})
			string(APPEND LIST_OF_SANITIZERS ",${sanitizer}")
		endforeach(sanitizer)
    endif()

  endif()

  if(LIST_OF_SANITIZERS)
    if(NOT "${LIST_OF_SANITIZERS}" STREQUAL "")
      target_compile_options(${project_name}
                             INTERFACE -fsanitize=${LIST_OF_SANITIZERS})
      target_link_libraries(${project_name}
                            INTERFACE -fsanitize=${LIST_OF_SANITIZERS})
    endif()
  endif()

endfunction()
