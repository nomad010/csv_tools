CXX=g++-4.9
CXXFLAGS=-Wall -Wextra -Wno-unused -Wno-unused-parameter -std=c++14 -I./src/ 
CXXLIBS=-lm 
RELEASE_FLAGS=-O3
DEBUG_FLAGS=-g -DDEBUG

rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

DIRS := ${shell find src/ -type d -print}
OBJ_DIRS = $(patsubst src/%, obj/%, $(DIRS))
SRC = $(call rwildcard,,*.cpp)

# SRC  = $(wildcard src/*.cpp)
OBJS = $(patsubst %.cpp, %.o, $(SRC))
RELEASE_OBJS = $(patsubst src/%.cpp, obj/release/%.o, $(SRC))
DEBUG_OBJS = $(patsubst src/%.cpp, obj/debug/%.o, $(SRC))

TESTS = $(patsubst src/test_%.cpp, %, $(wildcard src/test_*.cpp))
TEST_RESULTS = $(patsubst %, %_perform, $(TESTS))

EXECS = $(patsubst src/main_%.cpp, %, $(wildcard src/main_*.cpp))
RELEASE_EXECS = $(EXECS)
DEBUG_EXECS = $(patsubst %, %_debug, $(EXECS))

build: debug release

debug: obj/debug $(DEBUG_EXECS)
	
release: obj/release $(RELEASE_EXECS)

test: tests $(TEST_RESULTS) 

tests:
	@mkdir -p tests

obj/debug:
	@mkdir -p obj/debug

$(DEBUG_EXECS): $(DEBUG_OBJS)
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $@ obj/debug/main_$(subst _debug,,$@).o $(filter-out obj/debug/test_%.o, $(filter-out obj/debug/main_%.o, $(DEBUG_OBJS))) $(CXXLIBS)

obj/debug/%.o: src/%.cpp
	@mkdir -p `dirname $@`
	$(CXX) $(DEBUG_FLAGS) $(CXXFLAGS) -c -o $@ $<

obj/release:
	@mkdir -p obj/release

$(RELEASE_EXECS): $(RELEASE_OBJS)
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -o $@ obj/release/main_$@.o $(filter-out obj/release/test_%.o, $(filter-out obj/release/main_%.o, $(RELEASE_OBJS))) $(CXXLIBS)

obj/release/%.o: src/%.cpp
	@mkdir -p `dirname $@`
	$(CXX) $(RELEASE_FLAGS) $(CXXFLAGS) -c -o $@ $<

$(TESTS): $(RELEASE_OBJS)
	$(CXX) $(RELEASE_FLAGS) $(CXXFLAGS) -o tests/$@ obj/release/test_$@.o $(filter-out obj/release/test_%.o, $(filter-out obj/release/main_%.o, $(RELEASE_OBJS))) $(CXXLIBS)

$(TEST_RESULTS): $(TESTS)
	@(./tests/$(subst _perform,,$@) && (echo "\033[92m$(subst _perform,,$@)\033[0m") || (echo "\033[91m$(subst _perform,,$@)\033[0m";))

clean:
	rm -f $(RELEASE_EXECS) $(RELEASE_OBJS) $(DEBUG_EXECS) $(DEBUG_OBJS) $(TESTS)
	rm -rf obj
	rm -rf tests

check-syntax:
	$(CXX) $(CXXFLAGS) -Wextra -Wno-sign-compare -fsyntax-only $(CHK_SOURCES)
