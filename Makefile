CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread -I.
SRCS = main.cpp dyrektor/dyrektor.cpp dyrektor/clock.cpp petent/petent.cpp rejestracja/rejestracja.cpp urzednik/urzednik.cpp
TARGET = so_projekt

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

lint:
	clang-tidy $(SRCS) -- -std=c++20 -I.

.PHONY: clean lint
