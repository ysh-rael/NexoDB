# =============================================================================
# CMakeLists.txt (raiz)
# Ponto de entrada do build do NexoDB. Responsavel apenas por configurar o
# projeto, localizar o Node-API / node-addon-api e delegar a compilacao real
# para src/CMakeLists.txt, mantendo este arquivo curto e de facil leitura.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(nexodb_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Gera compile_commands.json para ferramentas de lint/IDE (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# cmake-js injeta CMAKE_JS_INC / CMAKE_JS_SRC / CMAKE_JS_LIB automaticamente.
include_directories(${CMAKE_JS_INC})
include_directories("${CMAKE_SOURCE_DIR}/include")

# node-addon-api (headers C++ sobre o Node-API em C)
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
include_directories(${NODE_ADDON_API_DIR})

add_definitions(-DNAPI_VERSION=9)

# Opcoes do projeto
option(NEXODB_BUILD_TESTS "Compilar a suite de testes em C++ (GoogleTest)" OFF)
option(NEXODB_BUILD_BENCHMARKS "Compilar os benchmarks em C++" OFF)

add_subdirectory(src)

if(NEXODB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test/cpp)
endif()
