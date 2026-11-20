add_executable(DeskLibTest
  DeskLibTest.cpp
  OpenMeteoTest.cpp
  ${REPO_ROOT}/lib/Desk/Lunar.cpp
  ${REPO_ROOT}/lib/Desk/DeskWeather.cpp
  ${REPO_ROOT}/lib/Desk/OpenMeteo.cpp
)

target_include_directories(DeskLibTest PRIVATE
  ${REPO_ROOT}/lib/Desk
)

target_link_libraries(DeskLibTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  ArduinoJson
)

gtest_discover_tests(DeskLibTest)
