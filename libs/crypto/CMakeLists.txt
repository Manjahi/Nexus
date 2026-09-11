# libnexus-crypto : safe wrapper around libsodium (Argon2id, XChaCha20-Poly1305,
# secure memory, RNG, password generation). Used only by components that
# actually need cryptography - today, that's the vault (apps/vault).

find_package(unofficial-sodium CONFIG REQUIRED)

add_library(nexus_crypto src/crypto.cpp)
add_library(nexus::crypto ALIAS nexus_crypto)

target_sources(nexus_crypto PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/nexus/crypto/crypto.hpp)

target_include_directories(nexus_crypto PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")

target_compile_features(nexus_crypto PUBLIC cxx_std_20)
target_link_libraries(nexus_crypto
    PUBLIC
        unofficial-sodium::sodium
    PRIVATE
        nexuspc::warnings)
