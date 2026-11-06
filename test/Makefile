CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter

run_tests: main.cpp mock/Arduino.h ../firmware/FuckBot_SNO/FuckBot_SNO.ino
	$(CXX) $(CXXFLAGS) -I mock -o run_tests main.cpp

test: run_tests
	./run_tests

clean:
	rm -f run_tests

.PHONY: test clean
