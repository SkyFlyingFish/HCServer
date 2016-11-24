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

#INCLUDEPATH += /usr/local/include

LIBS += -L/usr/lib -ltufao1
