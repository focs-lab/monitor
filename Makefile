monitor: Monitor.cpp
	g++ Monitor.cpp -o Monitor -lpthread

test: test.cpp
	~/llvm-project/build/bin/clang++ -fsanitize=thread -g test.cpp -O2 -o test

run:
	rm -f test
	make monitor
	make test
	./test
