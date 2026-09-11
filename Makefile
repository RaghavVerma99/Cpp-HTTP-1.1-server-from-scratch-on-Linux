CXX      ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra
CPPFLAGS += -Iinclude
TARGET    = nexus-server
SRCS      = src/main.cpp src/HttpServer.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) -std=c++20 $(CPPFLAGS) $(CXXFLAGS) $(SRCS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) 8080

clean:
	rm -f $(TARGET)