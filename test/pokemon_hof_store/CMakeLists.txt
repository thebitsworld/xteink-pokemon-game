add_executable(PokemonHallOfFameCodecTest
  PokemonHallOfFameCodecTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonHallOfFameCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
)

target_include_directories(PokemonHallOfFameCodecTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
)

target_link_libraries(PokemonHallOfFameCodecTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonHallOfFameCodecTest COMMAND PokemonHallOfFameCodecTest)

add_executable(PokemonHallOfFameStoreTest
  PokemonHallOfFameStoreTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonHallOfFameCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
  ${REPO_ROOT}/src/pokemon/PokemonHallOfFameStore.cpp
)

target_include_directories(PokemonHallOfFameStoreTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/Pokemon"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonHallOfFameStoreTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
)

target_link_libraries(PokemonHallOfFameStoreTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonHallOfFameStoreTest COMMAND PokemonHallOfFameStoreTest)
