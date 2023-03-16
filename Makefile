CXX=g++
CXXFLAGS=-g -Og -Wall -Werror -pedantic -std=c++17 -fsanitize=address -fsanitize=undefined -D_GLIBCXX_DEBUG

all: libfat.a fat_test fat_shell

fat_test: fat_test.o libfat.a
	$(CXX) $(CXXFLAGS) -o $@ $^

fat_shell: fat_shell.o libfat.a
	$(CXX) $(CXXFLAGS) -o $@ $^

fat_internal.h: fat.h

fat.o: fat.cc fat_internal.h

libfat.a: fat.o
	ar cr $@ $^
	ranlib $@

fat_test.o: fat_test.cc fat.h

clean:
	rm -f *.o

.PHONY: clean
