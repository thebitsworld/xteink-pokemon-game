add_executable(PokemonIvEvStoreCodecTest
  PokemonIvEvStoreCodecTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonIvEvStoreCodec.cpp
)

target_include_directories(PokemonIvEvStoreCodecTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
)

target_link_libraries(PokemonIvEvStoreCodecTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonIvEvStoreCodecTest COMMAND PokemonIvEvStoreCodecTest)

add_executable(PokemonIvEvStoreTest
  PokemonIvEvStoreTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonIvEvStoreCodec.cpp
  ${REPO_ROOT}/src/pokemon/PokemonIvEvStore.cpp
)

target_include_directories(PokemonIvEvStoreTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/Pokemon"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonIvEvStoreTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
)

target_link_libraries(PokemonIvEvStoreTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonIvEvStoreTest COMMAND PokemonIvEvStoreTest)
