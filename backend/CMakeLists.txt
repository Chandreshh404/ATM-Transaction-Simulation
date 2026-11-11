
add_executable(atm_backend
    src/main.cpp
    src/database/Database.cpp
    src/security/PinHasher.cpp
    src/security/SessionManager.cpp
    src/services/AuthService.cpp
)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

target_include_directories(atm_backend
    PRIVATE
    include
    external/httplib
)

target_link_libraries(atm_backend
    PRIVATE
    SQLite3::SQLite3
    OpenSSL::Crypto
    ws2_32
)

