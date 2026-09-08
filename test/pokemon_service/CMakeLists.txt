find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(POKEMON_SERVICE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(POKEMON_SERVICE_SPECIES_HEADER "${POKEMON_SERVICE_GENERATED_DIR}/PokemonSpecies.generated.h")
set(POKEMON_SERVICE_MOVES_HEADER "${POKEMON_SERVICE_GENERATED_DIR}/PokemonMoves.generated.h")
set(POKEMON_SERVICE_STATS_HEADER "${POKEMON_SERVICE_GENERATED_DIR}/PokemonStats.generated.h")
set(POKEMON_SERVICE_LEARNSETS_HEADER "${POKEMON_SERVICE_GENERATED_DIR}/PokemonLearnsets.generated.h")
set(POKEMON_SERVICE_ITEMS_HEADER "${POKEMON_SERVICE_GENERATED_DIR}/PokemonItems.generated.h")
set(POKEMON_SERVICE_GYMS_HEADER "${POKEMON_SERVICE_GENERATED_DIR}/PokemonGyms.generated.h")

add_custom_command(
  OUTPUT "${POKEMON_SERVICE_SPECIES_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${POKEMON_SERVICE_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${REPO_ROOT}/scripts/generate_pokemon_v2_species.py"
          --input "${REPO_ROOT}/scripts/data/pokemon-kanto-v2.csv"
          --output "${POKEMON_SERVICE_SPECIES_HEADER}"
  DEPENDS
    "${REPO_ROOT}/scripts/generate_pokemon_v2_species.py"
    "${REPO_ROOT}/scripts/data/pokemon-kanto-v2.csv"
  VERBATIM
)

add_custom_command(
  OUTPUT "${POKEMON_SERVICE_MOVES_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${POKEMON_SERVICE_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${REPO_ROOT}/scripts/generate_pokemon_moves.py"
          --input "${REPO_ROOT}/scripts/data/pokemon-moves.csv"
          --output "${POKEMON_SERVICE_MOVES_HEADER}"
  DEPENDS
    "${REPO_ROOT}/scripts/generate_pokemon_moves.py"
    "${REPO_ROOT}/scripts/data/pokemon-moves.csv"
  VERBATIM
)

add_custom_command(
  OUTPUT "${POKEMON_SERVICE_STATS_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${POKEMON_SERVICE_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${REPO_ROOT}/scripts/generate_pokemon_stats.py"
          --input "${REPO_ROOT}/scripts/data/pokemon-stats.csv"
          --output "${POKEMON_SERVICE_STATS_HEADER}"
  DEPENDS
    "${REPO_ROOT}/scripts/generate_pokemon_stats.py"
    "${REPO_ROOT}/scripts/data/pokemon-stats.csv"
  VERBATIM
)

add_custom_command(
  OUTPUT "${POKEMON_SERVICE_LEARNSETS_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${POKEMON_SERVICE_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${REPO_ROOT}/scripts/generate_pokemon_learnsets.py"
          --learnsets-input "${REPO_ROOT}/scripts/data/pokemon-learnsets.csv"
          --tmhm-input "${REPO_ROOT}/scripts/data/pokemon-tmhm.csv"
          --output "${POKEMON_SERVICE_LEARNSETS_HEADER}"
  DEPENDS
    "${REPO_ROOT}/scripts/generate_pokemon_learnsets.py"
    "${REPO_ROOT}/scripts/data/pokemon-learnsets.csv"
    "${REPO_ROOT}/scripts/data/pokemon-tmhm.csv"
  VERBATIM
)

add_custom_command(
  OUTPUT "${POKEMON_SERVICE_ITEMS_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${POKEMON_SERVICE_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${REPO_ROOT}/scripts/generate_pokemon_items.py"
          --input "${REPO_ROOT}/scripts/data/pokemon-items.csv"
          --output "${POKEMON_SERVICE_ITEMS_HEADER}"
  DEPENDS
    "${REPO_ROOT}/scripts/generate_pokemon_items.py"
    "${REPO_ROOT}/scripts/data/pokemon-items.csv"
  VERBATIM
)

add_custom_command(
  OUTPUT "${POKEMON_SERVICE_GYMS_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${POKEMON_SERVICE_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${REPO_ROOT}/scripts/generate_pokemon_gyms.py"
          --input "${REPO_ROOT}/scripts/data/pokemon-gyms.csv"
          --output "${POKEMON_SERVICE_GYMS_HEADER}"
  DEPENDS
    "${REPO_ROOT}/scripts/generate_pokemon_gyms.py"
    "${REPO_ROOT}/scripts/data/pokemon-gyms.csv"
  VERBATIM
)

add_executable(PokemonServiceTest
  PokemonServiceTest.cpp
  "${POKEMON_SERVICE_SPECIES_HEADER}"
  "${POKEMON_SERVICE_MOVES_HEADER}"
  "${POKEMON_SERVICE_STATS_HEADER}"
  "${POKEMON_SERVICE_LEARNSETS_HEADER}"
  "${POKEMON_SERVICE_ITEMS_HEADER}"
  "${POKEMON_SERVICE_GYMS_HEADER}"
  ${REPO_ROOT}/lib/Pokemon/PokemonTypes.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonSpecies.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGame.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonStoreCodec.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTracker.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonMoveData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBaseStats.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonLearnsets.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonItemData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonGymData.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonTypeChart.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBattle.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonBattleStoreCodec.cpp
  ${REPO_ROOT}/src/pokemon/PokemonStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonBattleStore.cpp
  ${REPO_ROOT}/src/pokemon/PokemonService.cpp
)

target_include_directories(PokemonServiceTest PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../pokemon_store/stubs"
  "${POKEMON_SERVICE_GENERATED_DIR}"
  "${REPO_ROOT}/src"
)

target_compile_definitions(PokemonServiceTest PRIVATE
  CROSSINK_ENABLE_POKEMON=1
  POKEMON_SERVICE_HOST_TEST=1
)

target_link_libraries(PokemonServiceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

add_test(NAME PokemonServiceTest COMMAND PokemonServiceTest)
