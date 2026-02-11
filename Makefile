CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I.
SRCS = main.cpp dyrektor/dyrektor.cpp dyrektor/clock.cpp dyrektor/process.cpp petent/petent.cpp petent/generator.cpp petent/dziecko.cpp rejestracja/rejestracja.cpp urzednik/urzednik.cpp
TARGET = so_projekt

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

lint:
	clang-tidy $(SRCS) -- -std=c++17 -I.

.PHONY: clean lint
