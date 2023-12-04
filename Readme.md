Accelerating an HTTP Server
===========================

![Save Walter](https://i.imgur.com/j5ORLuU.png)

The aim of this project is to optimize a primitive serial blocking HTTP Web server written in C by making it non-blocking, threaded, and maybe even asynchronous.

## Tasks

1. Upload/clone the repo with sources to our course server `auca.space`. You have to measure performance on `auca.space` and not on your computer.
2. `cd` into the repo.
3. Open the `server.c` file and change the `PORT` to your AUCA university ID. If the server program does not work with your ID, change it to any unoccupied port higher than 1024.
4. Compile `server.c` with `gcc -O3 -o server server.c`.
5. Create a copy of the `server.c` file under the name `server_optimized.c`.
6. Open the `server_optimized.c` file and find the main `// TODO` comment. Follow it. Optimize your code by using OS threads.
7. Change the code to improve `Requests per second` measurement for the Apache HTTP server benchmarking tool `ab -n 1000000 -c 64 http://auca.space:<YOUR PORT>/` against `server_optimized.c` in comparison to the same command performed against the `server.c`. To compile the optimized code, use the `gcc -O3 -o server_optimized server_optimized.c` command. You MUST make the optimized program handle at least twice the number of requests per second in `ab`.
8. Ensure that your server implementation not just performs well, but also correctly sends a sample web site to most popular browsers (Chromium family, Firefox, and Safari).

## Rules

* You MUST directly or indirectly utilize abstractions of the OS such as processes to get all points.
* Do NOT profile code anywhere but on our server at `auca.space`. You may use `peer.auca.space` located at a nearby data center to look at the influence of network latency on the performance, but the final measurements must be performed on the master `auca.space` server.
* Do NOT procrastinate and leave the work to the very last moment. If the server is overloaded close to the deadline, you will not be able to get good measurements. We will not give any extensions for that reason.
* Do NOT change any optimization flags. The code must be compiled with GCC with just the `-O3` optimization flag.
* Do NOT just benchmark your server with `ab`. Test that the server works correctly in the browser too.
* Do NOT use any other dir except for `/srv/walter` to test and benchmark your server.

## What to Submit

1. In your private course repository that was given to you by the instructor during the lecture, create the path `project-3/`.
2. Put the `server_optimized.c` file into that directory.
3. Commit and push your repository through Git. Submit the last commit URL to Canvas before the deadline.

## Deadline

Check Canvas for information about the deadlines.

## Documentation

    man gcc
    man pthread

## Links

### C, GDB

* [Beej's Guide to C Programming](https://beej.us/guide/bgc)
* [GDB Quick Reference](http://users.ece.utexas.edu/~adnan/gdb-refcard.pdf)

## Books

* C Programming: A Modern Approach, 2nd Edition by K. N. King
