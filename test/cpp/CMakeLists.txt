# =============================================================================
# test/cpp/CMakeLists.txt
# Suite de testes em C++ (GoogleTest) para o core do NexoDB, independente do
# Node.js -- util para testar storage/index/query diretamente, com builds
# mais rapidos do que passar pelo addon nativo.
# Ativar com: cmake -DNEXODB_BUILD_TESTS=ON ..
# =============================================================================
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_executable(nexodb_core_tests
  storage_engine_test.cpp
  document_test.cpp
)

target_include_directories(nexodb_core_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(nexodb_core_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(nexodb_core_tests)
