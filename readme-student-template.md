# Project README file

https://stackoverflow.com/questions/8465006/how-do-i-concatenate-two-strings-in-c

Initially, I tried using a HEAD request to get the content length from the server before downloading the file. My idea was to send the GF_OK header with the known length and then perform a normal GET request to stream the body. For some files, CURL returned a content length of 0, or the reported length didn’t match the actual number of bytes received.

Because of these problems, I abandoned the HEAD request approach. Instead, I switched to downloading the full file into memory first, storing it in a dynamically resizing buffer. Each time libcurl received a chunk of data, the callback function would calculate the actual size of that chunk, expand the buffer using realloc, copy the new data into place, and update the total size. By the end of the download, I had a contiguous block of memory containing the entire file and an exact byte count. With this information, I could send a GF_OK header that matched the actual size of the data. After sending the header, I streamed the data to the client in a loop, tracking how many bytes were successfully sent in case gfs_send only wrote a portion at a time. 
Testing

For this is one I used detailed logging to keep track of the number of bytes that were being sent. I checked that the provided client and that the downloaded files matched the served files exactly.  

I also ran tests using gfmetrics and all requests completed successfully.

Responsibility Area
Proxy (Owner/Initializer)
Cache (Responder)
Data Channel (Shared Memory)
- Creates shared memory segment(s)  - Pre-creates synchronization objects (mutex + semaphores)  - Determines segment size & number  - Cleans up/unlinks segments after use
- Opens proxy-created shared memory segment  - Writes file chunks into the segment  - Uses mutex + semaphores to synchronize writes
Command Channel (IPC)
- Sends requests to cache with file path & segment name
- Listens on command channel  - Receives requests and retrieves file from cache  - Provides file chunks back via data channel
Synchronization
- Owns initialization of mutex and semaphores  - Uses semaphores to wait for cache signals (chunk ready/consumed)
- Locks/unlocks mutex for atomic updates  - Posts semaphores to signal proxy when chunk is ready/consumed
Error Handling / Robustness
- Retries if cache not running yet  - Cleans up shared memory even if cache dies
- Retries if shared memory segment not ready  - Does not crash if proxy is not yet running
Lifecycle Management
- Responsible for creating, using, and destroying shared memory segments
- Only accesses the segments for the duration of the request; does not create or destroy them
