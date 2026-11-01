# =============================================================================
# src/CMakeLists.txt
# Agrega todos os modulos do core (cada um com responsabilidade unica) e o
# binding Node-API em um unico alvo .node. Cada pasta abaixo representa um
# "servico" isolado do banco de dados (storage, indices, transacoes, etc.).
# =============================================================================

file(GLOB_RECURSE NEXODB_CORE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/core/common/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/storage/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/document/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/index/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/transaction/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/query/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/aggregation/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/io/json/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/io/sql/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/backup/*.cpp"
)

file(GLOB NEXODB_NAPI_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/napi/*.cpp"
)

add_library(${PROJECT_NAME} SHARED
  ${NEXODB_CORE_SOURCES}
  ${NEXODB_NAPI_SOURCES}
  ${CMAKE_JS_SRC}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".node"
)

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
  target_compile_definitions(${PROJECT_NAME} PRIVATE _HAS_EXCEPTIONS=1)
  target_compile_options(${PROJECT_NAME} PRIVATE /EHsc /W4)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB})
