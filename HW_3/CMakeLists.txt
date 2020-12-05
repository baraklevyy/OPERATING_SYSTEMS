cmake_minimum_required(VERSION 3.17)
project(HW3 C)

set(CMAKE_C_STANDARD 11)

add_executable(HW3 message_slot.h message_slot.c message_sender.c message_reader.c)
include_directories(/usr/src/linux-hwe-5.4-headers-5.4.0-52/include)