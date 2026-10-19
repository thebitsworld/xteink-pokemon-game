find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
  NAME PokemonArtPackTest
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_package_pokemon_v2_release.py
)

add_test(
  NAME PokemonEffectArtTest
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_generate_pokemon_effect_art.py
)
