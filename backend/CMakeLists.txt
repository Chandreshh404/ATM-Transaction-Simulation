add_executable(atm_backend
    src/main.cpp
    src/database/Database.cpp
    src/security/PinHasher.cpp
    src/security/SessionManager.cpp
    src/services/AuthService.cpp
)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)

target_include_directories(atm_backend
    PRIVATE
    include
    external/httplib
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

target_link_libraries(atm_backend
    PRIVATE
    SQLite::SQLite3
    OpenSSL::Crypto
    Threads::Threads
)

if(WIN32)
    target_link_libraries(atm_backend PRIVATE ws2_32)
endif()
