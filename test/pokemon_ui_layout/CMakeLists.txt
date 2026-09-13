add_executable(PokemonUiLayoutTest
  PokemonUiLayoutTest.cpp
  ${REPO_ROOT}/lib/Pokemon/PokemonUiLayout.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(PokemonUiLayoutTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(PokemonUiLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

add_test(NAME PokemonUiLayoutTest COMMAND PokemonUiLayoutTest)
