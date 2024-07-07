cmake_minimum_required(VERSION 3.5)

project(test LANGUAGES C)

add_executable(test test.c ../json.c)
