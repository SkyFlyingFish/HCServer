HEADERS += \
    MyServer.h \
    Def.h

SOURCES += \
    MyServer.cpp \
    main.cpp

QT += network
CONFIG += C++11

RESOURCES += \
    res.qrc

mac{
INCLUDEPATH += /usr/local/include
LIBS += -L/usr/local/lib -ltufao1
}
linux{
LIBS += -ltufao1
}

LIBS += -lcurl
