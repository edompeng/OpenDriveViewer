file(READ "CMakeLists.txt" content)
string(REPLACE "-O3" "" content "${content}")
file(WRITE "CMakeLists.txt" "${content}")
