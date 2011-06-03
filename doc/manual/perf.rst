Performance Tuning
==================

Tube is a high performance server, yet under the assumption that it's well tuned.  Tube has several factor that affect Tube's throughputs and concurrent performance.

Thread Pool Size
----------------

It is recommended to make thread pool size near the count of CPU's core number.  For "poll_in" and "write_back" thread, we suggest you make them the count of CPU's core number.  For instance under a 4-core i7 processor, make these two parameters both 4.  If your server has other loads and therefore you want to minimize the thread usage, half of the core number is also acceptable.

"parser" thread pool's size might be smaller.  It's recommended to be half of the core number, because it's all CPU bounded operation.

"http_handler" thread pool's size could varies under different work loads.  If you're application will perform a lot of IO operation, it's recommended to have large number of threads in this pool, maybe 1 or 2 times larger than CPU's core number.

IO Mode
-------

Tube provides two way for performing write IO operation: "poll" and "block".  Both of them performs well under my benchmark.  However, they have different characteristics.  

"poll" is using asynchronous IO for write operation.  It scales well, and usually high performance under whatever network condition.  However, it will use more CPU resource since it has larger number of system calls.

"block" is using blocking IO for write operation.  It also scales well, but only under good network condition, such as LAN.  For slow network conditions, it might not scale well.  Yet, it's more CPU friendly, that's the reason why it was kept in the implementation.

For public web server application, it's recommended to use "poll" rather than "block", because the condition of network is unknown.

Network Issue
-------------

Tube has a "enable_cork" option,  by default it on.  It's highly recommended as true, since it will reduce the fragment packets.
