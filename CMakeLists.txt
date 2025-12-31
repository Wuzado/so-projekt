cmake_minimum_required(VERSION 4.1)
project(so_projekt)

set(CMAKE_CXX_STANDARD 20)

include_directories(.)

add_executable(so_projekt
        main.cpp
        dyrektor/dyrektor.cpp
        dyrektor/dyrektor.h
        petent/petent.cpp
        petent/petent.h
        rejestracja/rejestracja.cpp
        rejestracja/rejestracja.h
        common.h
        urzednik/urzednik.cpp
        urzednik/urzednik.h)
