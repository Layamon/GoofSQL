# TODO: Copyrights
# by default Goof is not built
IF (NOT WITH_GOOF)
  MESSAGE (STATUS "Not building with MyGoof storage engine, you must specify -DWITH_GOOF=1|ON|FORCE to cmake in order to build with MyGoof.")
  RETURN ()
ELSE ()
  MESSAGE (STATUS "Building with MyGoof storage engine = " ${WITH_GOOF})
ENDIF ()

# Suppress warnings for all compilers
remove_compile_flags(-Wundef)
append_cflags_if_supported(-Wno-unused-parameter)
MY_CHECK_CXX_COMPILER_FLAG("-Wno-unused-variable" HAVE_NO_UNUSED_VARIABLE)
IF(HAVE_NO_UNUSED_VARIABLE)
  ADD_COMPILE_FLAGS(ha_goof.cc COMPILE_FLAGS "-Wno-unused-variable")
ENDIF()

find_package(gflags REQUIRED)

enable_testing()
find_package(GTest REQUIRED)

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
  message(fatal "openssl not found") 
endif(NOT OPENSSL_FOUND)


SET(GOOFDB_SOURCES
  ha_goof.cc ha_goof.h
  ${GOOFDB_LIB_SOURCES}
)

MYSQL_ADD_PLUGIN(goofdb ${GOOFDB_SOURCES} STORAGE_ENGINE DEFAULT MODULE_ONLY
  LINK_LIBRARIES
)

MYSQL_ADD_EXECUTABLE(gooftest ${CMAKE_CURRENT_SOURCE_DIR}/test/gooftest.cc)
message("debug1--" ${GTEST_INCLUDE_DIRS})
TARGET_INCLUDE_DIRECTORIES(gooftest PRIVATE ${GTEST_INCLUDE_DIRS})
TARGET_LINK_LIBRARIES(gooftest gflags ${GMOCK} ${GTEST_BOTH_LIBRARIES})

MYSQL_ADD_EXECUTABLE(group_commit ${CMAKE_CURRENT_SOURCE_DIR}/vendor/group-commit/group_commit_bench.cc)
TARGET_INCLUDE_DIRECTORIES(group_commit PRIVATE ${GTEST_INCLUDE_DIRS})
TARGET_LINK_LIBRARIES(group_commit gflags ${GMOCK} ${GTEST_BOTH_LIBRARIES})

MYSQL_ADD_EXECUTABLE(compress_test ${CMAKE_CURRENT_SOURCE_DIR}/vendor/suffix-array/compress_test.cc)
TARGET_INCLUDE_DIRECTORIES(compress_test PRIVATE ${GTEST_INCLUDE_DIRS})
TARGET_LINK_LIBRARIES(compress_test gflags ${GMOCK} ${GTEST_BOTH_LIBRARIES} ${OPENSSL_LIBRARIES})