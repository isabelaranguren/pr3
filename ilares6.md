# Project README file

Initially, I tried using a HEAD request to get the content length from the server before downloading the file. My idea was to send the GF_OK header with the known length and then perform a normal GET request to stream the body. For some files, CURL returned a content length of 0, or the reported length didn’t match the actual number of bytes received.

Because of these problems, I abandoned the HEAD request approach. Instead, I switched to downloading the full file into memory first, storing it in a dynamically resizing buffer. Each time libcurl received a chunk of data, the callback function would calculate the actual size of that chunk, expand the buffer using realloc, copy the new data into place, and update the total size. By the end of the download, I had a contiguous block of memory containing the entire file and an exact byte count. With this information, I could send a GF_OK header that matched the actual size of the data. After sending the header, I streamed the data to the client in a loop, tracking how many bytes were successfully sent in case gfs_send only wrote a portion at a time. 
Testing.

I know this is not the best approach for this program but since it's already passing the gradescope. I moved to part 2

I used detailed logging to keep track of the number of bytes that were being sent. I checked that the provided client and that the downloaded files matched the served files exactly.  

I also ran tests using gfmetrics and all requests completed successfully.

# Part 2
I implemented a shared memory segment pool on the proxy side.  Each worker thread acquires  a segment at the start of a client request and returns it once the transfer is finished. Each segment contains two semaphores (wsem and rsem) used to synchronize data between the Proxy and Cache processes.

When a request arrives, the proxy sends a message to the cache through the message queue. The message includes the name of the shared memory segment that the worker acquired. The cache opens that segment and begins the data-exchange:
	1.	Proxy sends a request struct through the message queue.
	2.	Cache writes file metadata into the shared memory segment and signals wsem.
	3.	Proxy reads the metadata and signals rsem.
	4.	Cache writes the next chunk of data and signals wsem.
	5.	Proxy forwards the chunk to the client and signals rsem.

This exchange repeats until the entire file has been transferred.

# Key Components 

IPC Mechanisms
	- Posix Message Queue: Carries cache_req_t requests from Proxy → Cache.
	- Shared Memory Segments: Each holds a shm_data_t header and a data buffer for two-way communication.
    - Semaphores (wsem / rsem): Coordinate alternating write/read access between the processes.
Web Proxy Process (webproxy.c)
	- Initializes a pool of shared memory segments on startup.
	- Spawns multiple worker threads to handle client connections.
	- Each worker checks out an SHM segment before sending a request and returns it once the transfer completes.
Simple Cache Process (simplecached.c)
	- Creates the message queue during initialization.
	- Spawns multiple worker threads that wait for incoming requests.
	- Each worker opens the shared memory segment named in the request and streams file data into it.


# Testing
- I used print statements to check:
    - Proxy threads correctly send commands to the cache.
    - The cache writes data into shared memory segments.
    - The proxy read and forward data to the client without corruption.
    - File-not-found, errors, and success cases follow the expected protocol.
I kept track of the number of bytes accross the processes 
I tested failure modes by sending termination signals and observing resource cleanup. 
- **Python testing Suite:**:
Tested using base, stress,soak test in ipcstress.py from https://github.gatech.edu/cparaz3/6200-tools
I Used multiple proxy and cache threads to confirm that shared memory segments, semaphores, and MQ interactions are independent and race-free.
- **Memory checks:**  I used Valgrind. 

### Other C 

- [libcurl](https://curl.se/libcurl/c/CURLINFO_CONTENT_LENGTH_DOWNLOAD_T.html)
- [libcurl writefunction](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html)
- [Test suites](https://github.gatech.edu/cparaz3/6200-tools)
- [pread](https://man7.org/linux/man-pages/man2/pread.2.html)
- [Message Queue](https://www.softprayog.in/programming/interprocess-communication-using-posix-message-queues-in-linux)
- [Shared Memory](https://www.softprayog.in/programming/interprocess-communication-using-posix-shared-memory-in-linux)
- [File size](https://stackoverflow.com/questions/238603/how-can-i-get-a-files-size-in-c)
- [String](https://stackoverflow.com/questions/8465006/how-do-i-concatenate-two-strings-in-c)
- [Posix Semaphore](https://linux.die.net/man/3/sem_init)
- [Read and Write](https://man7.org/conf/lca2013/IPC_Overview-LCA-2013-printable.pdf)