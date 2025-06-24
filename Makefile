# Makefile for building the test application with a header-only library.
# This version is specifically tailored for Linux and Windows (using a GNU toolchain).

# Compiler and default flags
CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -Wpedantic -I. -pthread -O2
LDFLAGS =
LIBS =

# Source file for the executable
TEST_SRC = main.cpp
TEST_OBJ = $(TEST_SRC:.cpp=.o)
# The executable will be named 'run_tests' to avoid conflict with the 'test' phony target.
TEST_TARGET = run_tests

# ----------------- OS-specific settings -----------------

# Default to Linux settings
OS_LIBS = -lodbc
RM = rm -f

# Check if we are on Windows (specifically, a GNU environment on Windows)
ifeq ($(OS),Windows_NT)
    # Windows settings (e.g., using MinGW/MSYS2)
    OS_LIBS = -lodbc32
    TEST_TARGET := $(TEST_TARGET).exe
    RM = del /Q /F
endif

# Append the OS-specific libraries to the main LIBS variable
LIBS += $(OS_LIBS)

# ----------------------- Build Rules ----------------------

# Default target builds the executable
all: $(TEST_TARGET)

# Rule to build the test executable
$(TEST_TARGET): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

# Generic rule for building object files
# Note that main.o now depends on odbc_wrapper.h
$(TEST_OBJ): $(TEST_SRC) odbc_wrapper.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Phony target to run the tests. Depends on the executable being built.
test: $(TEST_TARGET)
	./$(TEST_TARGET)

# Clean up build artifacts
clean:
	$(RM) $(TEST_OBJ) $(TEST_TARGET)

.PHONY: all clean test
