QT += quick qml quickcontrols2 network

CONFIG += c++17

INCLUDEPATH += ../common/include

SOURCES += \
    main.cpp \
    backend/virtual_remote.cpp \
    backend/backend.cpp

HEADERS += \
    backend/virtual_remote.hpp \
    backend/backend.hpp

RESOURCES += \
    resources.qrc

TARGET = race_gui
TEMPLATE = app