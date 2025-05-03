clang++ -std=c++2c -Wc23-extensions $(pkg-config --cflags --libs libpipewire-0.3) boop.cpp -o boop
clang++ -std=c++2c -Wc23-extensions $(pkg-config --cflags --libs libpipewire-0.3) a_machine.cpp
clang++ -std=c++2c -Wc23-extensions $(pkg-config --cflags --libs libpipewire-0.3) b_machine.cpp -o b.out
