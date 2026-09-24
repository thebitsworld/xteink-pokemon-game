add_executable(PokemonMovesetStoreCodecTest
  PokemonMovesetStoreCodecTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonMovesetStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
)

target_include_directories(PokemonMovesetStoreCodecTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
)

target_compile_definitions(PokemonMovesetStoreCodecTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
)

target_link_libraries(PokemonMovesetStoreCodecTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonMovesetStoreCodecTest COMMAND PokemonMovesetStoreCodecTest)

add_executable(PokemonMovesetStoreTest
  PokemonMovesetStoreTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonMovesetStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
  ${REPO_ROOT}/src/pokemon/PokemonMovesetStore.cpp
)

target_include_directories(PokemonMovesetStoreTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/Pokemon"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonMovesetStoreTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
)

target_link_libraries(PokemonMovesetStoreTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonMovesetStoreTest COMMAND PokemonMovesetStoreTest)
