Accelerating an HTTP Server
===========================

The aim of this project is to optimize a primitive, serial, blocking HTTP web server written in C, which applies a median filter to a submitted PNG image file, by making it non-blocking, threaded, and possibly even asynchronous.

## Tasks

1. Upload or clone the repository containing the sources to our course server at `auca.space`. Important: you must measure performance on `auca.space`, not on your local machine.

2. `cd` into the repository directory.

3. Open the `server.c` file and change the `SERVER_PORT` to your university ID.

4. Compile `server.c` using `gcc -O3 -o server server.c -luuid -lm`. You need the `uuid` library installed. On Debian-based distributions, you can install it by running `sudo apt install uuid-dev`. Our server environment already has the library installed. Some Unix systems, such as recent versions of macOS, include the library bundled with the OS.

5. Submit an image for processing using `curl -v -X 'POST' --data-binary '@srv/front/test.png' 'http://127.0.0.1:<SERVER_PORT>/images'`. Replace `<SERVER_PORT>` with the port number set in step 3. Note the returned job ID, which will be in the form of a [UUID](https://en.wikipedia.org/wiki/Universally_unique_identifier).

   Retrieve the processed image with `curl -o 'srv/front/test_processed.png' -v 'http://127.0.0.1:<SERVER_PORT>/images/<UUID>/'`. Again, replace `<SERVER_PORT>` and `<UUID>` accordingly.

   Finally, check whether the server can serve the processed static file by opening `http://127.0.0.1:<SERVER_PORT>/test_processed.png` in your browser.

6. Create a copy of `server.c` and name it `server_optimized.c`.

7. Optimize your code using OS threads. You may also explore using the GNU/Linux non-blocking I/O API (which may use threads internally), or specialized networking functions like [`sendfile`](https://man7.org/linux/man-pages/man2/sendfile.2.html) for efficient file transfers. Improve performance by making better use of CPU pipelines, caches, and memory, or by applying the median filter with SIMD for faster image processing. Use all the knowledge acquired in previous projects to optimize the program. Ensure the code follows basic security best practices.

8. Make sure your server not only performs efficiently but also correctly handles HTTP requests and responses in Chromium-based browsers, Firefox, and Safari. Additionally, verify that it works with `curl`, a widely used HTTP command-line tool.

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
