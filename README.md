# Particle simulation
This repository contains my solution to the [student challenge by IPS](https://github.com/industrialpathsolutions/student-challenge-particle-simulation).
# Building
To build the binary on a Linux system, run `make`, and to execute the binary, run `./main -f <data file>`. To specify the number of threads to use for parsing the input file and counting the particle pairs, the optional arguments `-p <num>` and `-c <num>` can also be passed on the command line. If they are not specified, they will both default to 4. It is recommended to pass both flags with the number of threads/virtual cores of the CPU as the arguments.
To build with profile guided optimizations, run `make pgo`. This is not recommended since the resulting binary runs slower than when compiling without PGO.
