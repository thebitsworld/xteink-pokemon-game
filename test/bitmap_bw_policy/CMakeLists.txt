add_executable(BitmapBwPolicyTest
  BitmapBwPolicyTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)

target_include_directories(BitmapBwPolicyTest PRIVATE
  ${REPO_ROOT}/lib/GfxRenderer
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_link_libraries(BitmapBwPolicyTest PRIVATE
  crosspoint_test_common
)

add_test(NAME BitmapBwPolicyTest COMMAND BitmapBwPolicyTest)
