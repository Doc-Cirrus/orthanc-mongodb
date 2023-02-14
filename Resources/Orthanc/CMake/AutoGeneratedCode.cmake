# Taken from https://hg.orthanc-server.com/orthanc-databases/

set(EMBED_RESOURCES_PYTHON "${CMAKE_CURRENT_LIST_DIR}/../EmbedResources.py"
  CACHE INTERNAL "Path to the EmbedResources.py script from Orthanc")
set(AUTOGENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/AUTOGENERATED")
set(AUTOGENERATED_SOURCES)

file(MAKE_DIRECTORY ${AUTOGENERATED_DIR})
include_directories(${AUTOGENERATED_DIR})

macro(EmbedResources)
  # Convert a semicolon separated list to a whitespace separated string
  set(SCRIPT_OPTIONS)
  set(SCRIPT_ARGUMENTS)
  set(DEPENDENCIES)
  set(IS_PATH_NAME false)

  set(TARGET_BASE "${AUTOGENERATED_DIR}/EmbeddedResources")

  # Loop over the arguments of the function
  foreach(arg ${ARGN})
    # Extract the first character of the argument
    string(SUBSTRING "${arg}" 0 1 FIRST_CHAR)
    if (${FIRST_CHAR} STREQUAL "-")
      # If the argument starts with a dash "-", this is an option to
      # EmbedResources.py
      if (${arg} MATCHES "--target=.*")
        # Does the argument starts with "--target="?
        string(SUBSTRING "${arg}" 9 -1 TARGET)  # 9 is the length of "--target="
        set(TARGET_BASE "${AUTOGENERATED_DIR}/${TARGET}")
      else()
        list(APPEND SCRIPT_OPTIONS ${arg})
      endif()
    else()
      if (${IS_PATH_NAME})
        list(APPEND SCRIPT_ARGUMENTS "${arg}")
        list(APPEND DEPENDENCIES "${arg}")
        set(IS_PATH_NAME false)
      else()
        list(APPEND SCRIPT_ARGUMENTS "${arg}")
        set(IS_PATH_NAME true)
      endif()
    endif()
  endforeach()

  add_custom_command(
    OUTPUT
    "${TARGET_BASE}.h"
    "${TARGET_BASE}.cpp"
    COMMAND ${PYTHON_EXECUTABLE} ${EMBED_RESOURCES_PYTHON}
            ${SCRIPT_OPTIONS} "${TARGET_BASE}" ${SCRIPT_ARGUMENTS}
    DEPENDS
    ${EMBED_RESOURCES_PYTHON}
    ${DEPENDENCIES}
    )

  list(APPEND AUTOGENERATED_SOURCES
    "${TARGET_BASE}.cpp"
    )
endmacro()
