#pragma once
#include <chrono>
#define TIMESTART auto start = std::chrono::high_resolution_clock::now();
#define TIMEEND {auto end = std::chrono::high_resolution_clock::now();auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);std::cout << "####################cost time : " << duration.count() / 1000.0 << "ms##################" << std::endl;}
