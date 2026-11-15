# The libraries for the app as it is shared (tools/make_dmg.sh, the macos-dist preset): like
# vcpkg's arm64-osx, built for macOS 14.4 and later (what Homebrew's Qt needs) instead of the
# macOS this Mac runs, so the app opens on other Macs too. Installed into its own folder
# (build/vcpkg_installed_dist): vcpkg keeps one set of packages per folder.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.4)
