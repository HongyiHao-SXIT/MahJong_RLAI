CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra

SRCDIR := src
BUILDDIR := build
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
LDLIBS := -lws2_32
else
EXEEXT :=
CXXFLAGS += -pthread
LDLIBS := -pthread
endif
TARGET := $(BUILDDIR)/server$(EXEEXT)

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

run: $(TARGET)
	$(TARGET) -H 0.0.0.0 -P 9999 -A 0 -ob

debug: CXXFLAGS += -g -DDEBUG
debug: all
