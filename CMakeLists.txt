cmake_minimum_required(VERSION 3.24)

project(tnuxmusic VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Multimedia
    Qml
)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

qt_standard_project_setup(REQUIRES 6.5)

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_executable(tnuxmusic
    src/main.cpp
    src/Track.h
    src/Track.cpp
    src/NcmImportService.h
    src/NcmImportService.cpp
    src/MetadataReader.h
    src/MetadataReader.cpp
    src/LibraryManager.h
    src/LibraryManager.cpp
    src/AlbumModel.h
    src/AlbumModel.cpp
    src/PlaybackQueue.h
    src/PlaybackQueue.cpp
    src/PlayerController.h
    src/PlayerController.cpp
    src/TlyParser.h
    src/TlyParser.cpp
    src/LyricModel.h
    src/LyricModel.cpp
    src/ScriptBridge.h
    src/ScriptBridge.cpp
)

qt_add_qml_module(tnuxmusic
    URI TnuxMusic
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(tnuxmusic PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Qml
    OpenSSL::Crypto
)

target_compile_definitions(tnuxmusic PRIVATE
    TNUXMUSIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

install(TARGETS tnuxmusic
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGE_NAME "tnuxmusic")
    set(CPACK_PACKAGE_VENDOR "tnuxmusic")
    set(CPACK_PACKAGE_CONTACT "tnuxmusic")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A Qt 6 desktop music player with NCM import support")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "tnuxmusic")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()

include(CPack)
