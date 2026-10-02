add_executable(PokemonSaveBundleCodecTest
  PokemonSaveBundleCodecTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSaveBundleCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
)

target_include_directories(PokemonSaveBundleCodecTest PRIVATE
  "${REPO_ROOT}/lib/Pokemon"
)

target_link_libraries(PokemonSaveBundleCodecTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonSaveBundleCodecTest COMMAND PokemonSaveBundleCodecTest)

add_executable(PokemonSaveTransferTest
  PokemonSaveTransferTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSaveBundleCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonCrc32.cpp
  ${REPO_ROOT}/src/pokemon/PokemonSaveTransfer.cpp
)

target_include_directories(PokemonSaveTransferTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/Pokemon"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonSaveTransferTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
)

target_link_libraries(PokemonSaveTransferTest PRIVATE
  crosspoint_test_common
)

add_test(NAME PokemonSaveTransferTest COMMAND PokemonSaveTransferTest)
