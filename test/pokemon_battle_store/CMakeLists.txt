add_executable(PokemonBattleStoreCodecTest
  PokemonBattleStoreCodecTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBattleStoreCodec.cpp
)

target_include_directories(PokemonBattleStoreCodecTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
)

target_link_libraries(PokemonBattleStoreCodecTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonBattleStoreCodecTest COMMAND PokemonBattleStoreCodecTest)

add_executable(PokemonBattleStoreTest
  PokemonBattleStoreTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBattleStoreCodec.cpp
  ${REPO_ROOT}/src/pokemon/PokemonBattleStore.cpp
)

target_include_directories(PokemonBattleStoreTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/Pokemon"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonBattleStoreTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
)

target_link_libraries(PokemonBattleStoreTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonBattleStoreTest COMMAND PokemonBattleStoreTest)
