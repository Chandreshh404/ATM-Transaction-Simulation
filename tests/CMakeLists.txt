
find_package(SQLite3 REQUIRED)

# PIN hashing tests
add_executable(pin_hasher_test
    pin_hasher_test.cpp
    ../backend/src/security/PinHasher.cpp
)

target_include_directories(pin_hasher_test
    PRIVATE
    ../backend/include
)

target_link_libraries(pin_hasher_test
    PRIVATE
    OpenSSL::Crypto
)

add_test(
    NAME PINHasherTest
    COMMAND pin_hasher_test
)


# Session management tests
add_executable(session_manager_test
    session_manager_test.cpp
    ../backend/src/security/SessionManager.cpp
)

target_include_directories(session_manager_test
    PRIVATE
    ../backend/include
)

target_link_libraries(session_manager_test
    PRIVATE
    OpenSSL::Crypto
)

add_test(
    NAME SessionManagerTest
    COMMAND session_manager_test
)


# Database integration tests
add_executable(database_test
    database_test.cpp
    ../backend/src/database/Database.cpp
)

target_include_directories(database_test
    PRIVATE
    ../backend/include
)

target_link_libraries(database_test
    PRIVATE
    SQLite::SQLite3
)

target_compile_definitions(database_test
    PRIVATE
    ATM_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME DatabaseIntegrationTest
    COMMAND database_test
)
