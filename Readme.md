Accelerating an HTTP Server
===========================

The aim of this project is to optimize a primitive, serial, blocking HTTP web server written in C, which applies a median filter to a submitted PNG image file, by making it non-blocking, threaded, and possibly even asynchronous.

## Tasks

1. Upload/clone the repo with sources to our course server `auca.space`. You have to measure performance on `auca.space` and not on your computer.
2. `cd` into the repo.
3. Open the `server.c` file and change the `PORT` to the one specified by your instructor.
4. Compile `server.c` with `gcc -O3 -o server server.c`.
5. Submit a picture for processing with `curl -v -X POST --data-binary @./srv/front/test.png http://127.0.0.1:8080/images`. Get the processed image with `curl -o output.png -v http://127.0.0.1:8080/images/<UUID>/`. The UUID will be given in the first request.
6. Create a copy of the `server.c` file under the name `server_optimized.c`.
7. Optimize your code by using OS threads. To earn full credit, you must implement and use thread pooling. You can also try using a GNU/Linux non-blocking API, apply the median filter with SIMD, and reorganize your code to achieve better performance. Use all the knowledge gained from previous labs and projects.
8. Ensure that your server implementation not only performs well but also correctly processes requests and files across the Chromium family, Firefox, and Safari.

## Rules

* You MUST directly or indirectly utilize abstractions of the OS such as threads to get all points.
* Do NOT profile code anywhere but on our server at `auca.space`.
* Do NOT procrastinate and leave the work to the very last moment. If the server is overloaded close to the deadline, you will not be able to get good measurements. We will not give any extensions for that reason.
* Do NOT change any optimization flags. The code must be compiled with GCC with just the `-O3` optimization flag.

## What to Submit

Commit and push your changes to the private repository provided by your instructor. Submit the URL of your last commit to Moodle before the deadline.

## Deadline

Check Moodle for information about the deadlines.

## Documentation

    man gcc
    man pthread

## Links

### C, GDB

* [Beej's Guide to C Programming](https://beej.us/guide/bgc)
* [GDB Quick Reference](http://users.ece.utexas.edu/~adnan/gdb-refcard.pdf)

## Books

* C Programming: A Modern Approach, 2nd Edition by K. N. King
