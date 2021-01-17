# TODO: Copyrights
# by default Goof is not built
IF (NOT WITH_GOOF)
  MESSAGE (STATUS "Not building with MyGoof storage engine, you must specify -DWITH_GOOF=1|ON|FORCE to cmake in order to build with MyGoof.")
  RETURN ()
ELSE ()
  MESSAGE (STATUS "Building with MyGoof storage engine = " ${WITH_GOOF})
ENDIF ()

# check platform support, no 32 bit
IF (NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  MESSAGE ("x86_64 is only platform supported. ${CMAKE_SYSTEM_PROCESSOR} found. Not building MyGoof")
  RETURN ()
ENDIF ()

# Suppress warnings for all compilers
remove_compile_flags(-Wundef)
append_cflags_if_supported(-Wno-unused-parameter)
MY_CHECK_CXX_COMPILER_FLAG("-Wno-unused-variable" HAVE_NO_UNUSED_VARIABLE)
IF(HAVE_NO_UNUSED_VARIABLE)
  ADD_COMPILE_FLAGS(ha_goof.cc COMPILE_FLAGS "-Wno-unused-variable")
ENDIF()

# Suppress warnings for gcc-5 or newer
IF(CMAKE_COMPILER_IS_GNUCXX AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
  append_cflags_if_supported(-Wno-suggest-attribute=format)
ENDIF()

IF(UNIX)
  IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    ADD_DEFINITIONS(-DOS_LINUX)

    CHECK_INCLUDE_FILES(linux/falloc.h HAVE_LINUX_FALLOC_H)
    CHECK_FUNCTION_EXISTS(fallocate HAVE_FALLOCATE)

    IF(HAVE_FALLOCATE AND HAVE_LINUX_FALLOC_H)
      ADD_DEFINITIONS(-DGOOFDB_FALLOCATE_PRESENT)
    ENDIF()
  ENDIF()
ENDIF()

SET(GOOFDB_SOURCES
  ha_goof.cc ha_goof.h
  ${GOOFDB_LIB_SOURCES}
)

MYSQL_ADD_PLUGIN(goofdb ${GOOFDB_SOURCES} STORAGE_ENGINE DEFAULT MODULE_ONLY
  LINK_LIBRARIES
)

SET(GOOFDB_TOOL_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/group-commit
  ${GOOFDB_LIB_SOURCES}
)

MYSQL_ADD_EXECUTABLE(sst_dump ${CMAKE_CURRENT_SOURCE_DIR}/tools/sst_dump.cc ${GOOFDB_TOOL_SOURCES})
TARGET_LINK_LIBRARIES(sst_dump ${goofdb_static_libs})
