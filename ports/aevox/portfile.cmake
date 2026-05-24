# Private overlay skeleton for Aevox release source artifacts.
# The SHA512 value must be replaced with the hash printed by vcpkg after the
# first real GitHub Release asset is published.

set(AEVOX_PORT_VERSION 0.3.0)

vcpkg_download_distfile(AEVOX_SOURCE_ARCHIVE
    URLS "https://github.com/MohammadMokhalled/Aevox/releases/download/v${AEVOX_PORT_VERSION}/aevox-${AEVOX_PORT_VERSION}-source.tar.gz"
    FILENAME "aevox-${AEVOX_PORT_VERSION}-source.tar.gz"
    SHA512 0
)

vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${AEVOX_SOURCE_ARCHIVE}"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DAEVOX_BUILD_TESTS=OFF
        -DAEVOX_BUILD_EXAMPLES=OFF
        -DAEVOX_BUILD_BENCHMARKS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME aevox CONFIG_PATH lib/cmake/aevox)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
