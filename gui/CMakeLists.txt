# 6.5+ for QQmlApplicationEngine::loadFromModule (used in main.cpp)
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# resources.qrc puts the QML under qrc:/qt/qml/BoardGui (with its qmldir),
# which is how loadFromModule("BoardGui", "Main") finds it — same as gui.pro
qt_add_executable(gui
    main.cpp
    resources.qrc
)

target_link_libraries(gui PRIVATE Qt6::Quick Qt6::QuickControls2 common)
