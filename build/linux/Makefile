CXXSOURCES = \
    MJPGServer.cpp \
    V4L2Source.cpp \
    LogLevel.cpp \
    JSON.cpp \

CPCXXSOURCES = \
    Threading/Threads.cpp \
    Threading/Lock.cpp \
    Hash/HashKey.cpp \
    Logger/Logger.cpp \
    Strings/BString/bstrwrap.cpp \
    Strings/Strings.cpp \
    Utils/MemoryBlock.cpp \
    Platform/Linux.cpp \
    File/File.cpp \
    Encoding/Encode.cpp \
    Streams/Streams.cpp \
    __version__.cpp \
    Time/Time.cpp \
    Network/Socket.cpp \
    Network/Server.cpp \
    Network/Connection.cpp \
    Network/Address.cpp \
    Network/Servers/HTTP.cpp \
    Crypto/AES.cpp \
    Protocol/HTTP/HTTPCode.cpp \
    Network/Clients/TextualHeaders.cpp \
    Network/LinuxSpecific.cpp \
    Crypto/Random.cpp \

CPCSOURCES= \
    Strings/BString/bstrlib.c \

DFLAGS=-D_LINUX=1 -DCONSOLE=1 -D_FILE_OFFSET_BITS=64 -DDEBUG=1 -DHasClassPathConfig=1 -DWantAES=1 -DWantMD5Hashing=1 -DWantThreadLocalStorage=1 -DWantBaseEncoding=1 -DWantFloatParsing=1 -DWantRegularExpressions=1 -DWantTimedProfiling=1 -DWantAtomicClass=1 -DWantExtendedLock=1 -DWantCompression=1 -DDontWantUPNPC=1

OUTPUT = mjpgsrv

CXXFLAGS := -g -O0
CXXFLAGS += $(DFLAGS)

ifeq ($(CONFIG),Release)
CXXFLAGS += -O2
CFLAGS += -O2
endif

CXXFLAGS += -pthread -march=native -mtune=native
CFLAGS += -pthread -march=native -mtune=native

INCPATH = -I../../ClassPath/include
LDFLAGS = -pthread -lrt -ldl

# Don't touch anything below this line
OBJ = $(notdir $(CXXSOURCES:.cpp=.o)) $(notdir $(CSOURCES:.c=.o)) $(addprefix ClassPath/, $(CPCXXSOURCES:.cpp=.o)) $(addprefix ClassPath/, $(CPCSOURCES:.c=.o))
Q=@


$(OUTPUT): $(OBJ)
	@echo Linking $@
	@-rm -f ./$(OUTPUT)
	$(Q)$(CXX) $(LDFLAGS) -o $(OUTPUT) $(OBJ) $(CPBUILDFLAGS)


%.d: ../../src/%.cpp
	@echo ">  Computing dependencies for $*.cpp"
	$(Q)$(CXX) $(CXXFLAGS) $(DFLAGS) $(INCPATH) -MM -MT '$(notdir $(patsubst %.cpp,%.o,$<))' $< > $@

%.d: %.c
	@echo ">  Computing dependencies for $*.c"
	$(Q)$(CC) $(CFLAGS) $(DFLAGS) -MM -MT '$(patsubst %.c,%.o,$<)' $< > $@

.c.o:
	@echo ">  Compiling $(notdir $*.c)"
	$(Q)$(CC) $(CFLAGS) $(DFLAGS) -c $(addprefix ../../src/,$*.c) -o $*.o


ClassPath/%.o: ../../ClassPath/src/%.cpp
	@echo ">  Compiling $(notdir $*.cpp) into $(dir $@)"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DFLAGS) $(INCPATH) -c $(addprefix ../../ClassPath/src/,$*.cpp) -o $@

ClassPath/%.o: ../../ClassPath/src/%.c
	@echo ">  Compiling $(notdir $*.c) into $(dir $@)"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(DFLAGS) $(INCPATH) -c $(addprefix ../../ClassPath/src/,$*.c) -o $@


%.o: ../../src/%.cpp
	@echo ">  Compiling $(notdir $*.cpp)"
	$(Q)$(CXX) $(CXXFLAGS) $(DFLAGS) $(INCPATH) -c $(addprefix ../../src/,$*.cpp) -o $*.o

install: $(OUTPUT)
	sudo cp mjpgsrv /usr/local/bin
	sudo mkdir /etc/mjpgserver
	sudo cp ../../defaultConfig.json /etc/mjpgserver/config.json
	sudo cp ../../mjpgserver.service /lib/systemd/system/

uninstall:
	-sudo rm /usr/local/bin/mjpgsrv
	-sudo rm -r /etc/mjpgserver
	-sudo rm /lib/systemd/system/mjpgserver.service 


clean:
	@-rm $(OBJ)
	@-rm $(OBJ:.o=.d)
	@-rm $(OUTPUT)
	@-rm -r ClassPath
	@echo Done cleaning!


-include $(CXXSOURCES:.cpp=.d)
-include $(CSOURCES:.c=.d)


