add_executable(NearbySyncCodecTest
  NearbySyncCodecTest.cpp
  ${REPO_ROOT}/lib/NearbySync/NearbySyncCodec.cpp
)

target_include_directories(NearbySyncCodecTest PRIVATE
  "${REPO_ROOT}/lib/NearbySync"
)

target_link_libraries(NearbySyncCodecTest PRIVATE
  crosspoint_test_common
)

add_test(NAME NearbySyncCodecTest COMMAND NearbySyncCodecTest)

add_executable(NearbySyncStoreTest
  NearbySyncStoreTest.cpp
  ${REPO_ROOT}/lib/NearbySync/NearbySyncCodec.cpp
  ${REPO_ROOT}/src/network/NearbySyncStore.cpp
)

target_include_directories(NearbySyncStoreTest PRIVATE
  "${REPO_ROOT}/test/pokemon_store/stubs"
  "${REPO_ROOT}/lib/NearbySync"
  "${REPO_ROOT}/src"
)

target_link_libraries(NearbySyncStoreTest PRIVATE
  crosspoint_test_common
)

add_test(NAME NearbySyncStoreTest COMMAND NearbySyncStoreTest)
