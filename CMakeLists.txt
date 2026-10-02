cmake_minimum_required(VERSION 3.24)
project(wordspring-runner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)
find_package(KF6 REQUIRED COMPONENTS CoreAddons Config Runner KCMUtils)
find_package(SQLite3 REQUIRED)

add_library(wordspring_core STATIC src/store.cpp src/store.h)
set_target_properties(wordspring_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wordspring_core PUBLIC Qt6::Core KF6::ConfigCore SQLite::SQLite3)
target_include_directories(wordspring_core PUBLIC src)

kcoreaddons_add_plugin(krunner_wordspring SOURCES src/runner.cpp src/runner.h INSTALL_NAMESPACE "kf6/krunner")
target_link_libraries(krunner_wordspring PRIVATE wordspring_core KF6::Runner)
target_compile_definitions(krunner_wordspring PRIVATE WORDSPRING_APP_PATH="${CMAKE_INSTALL_FULL_BINDIR}/wordspring")

add_library(wordspring_settings STATIC src/settingswidget.cpp src/settingswidget.h)
set_target_properties(wordspring_settings PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(wordspring_settings PUBLIC wordspring_core Qt6::Widgets)
target_compile_definitions(wordspring_settings PRIVATE WORDSPRING_SOURCE_IMPORTER="${CMAKE_SOURCE_DIR}/importer/import_dictionary.py")

kcoreaddons_add_plugin(kcm_krunner_wordspring SOURCES src/kcm.cpp INSTALL_NAMESPACE "kf6/krunner/kcms")
target_link_libraries(kcm_krunner_wordspring PRIVATE wordspring_settings KF6::KCMUtils)

add_executable(wordspring src/main.cpp)
target_link_libraries(wordspring PRIVATE wordspring_settings Qt6::Widgets)
install(TARGETS wordspring RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES importer/import_dictionary.py DESTINATION ${CMAKE_INSTALL_DATADIR}/wordspring)

if(BUILD_TESTING)
  add_executable(store_test tests/store_test.cpp)
  target_link_libraries(store_test PRIVATE wordspring_core Qt6::Test)
  add_test(NAME store_test COMMAND store_test)
endif()
