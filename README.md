# bplib

[1. Overview](#1-overview)  
[2. Application Design](#2-application-design)  
[3. Build with Make](#3-build-with-make)  
[4. Application Programming Interface](#4-application-programming-interface)  
[5. Storage Service](#5-storage-service)  

[Note #1 - Bundle Protocol Version 6](doc/bpv6_notes.md)  
[Note #2 - Library Development Guidelines](doc/dev_notes.md)  
[Note #3 - Configuration Parameter Trades](doc/parm_notes.md)  

----------------------------------------------------------------------
## 1. Overview
----------------------------------------------------------------------

The Bundle Protocol library (bplib) implements a subset of the RFC5050 Bundle Protocol necessary for embedded space flight applications. The library uses the concept of a bundle channel to manage the process of encapsulating application data in bundles, and extracting application data out of bundles.  A channel specifies how the bundles are created (e.g. primary header block fields), and how bundles are processed (e.g. payloads extracted from payload block). Bplib contains no threads and relies entirely on the calling application for its execution context and implements a thread-safe blocking I/O model where requested operations will either block according to the provided timeout, or return an error code immediately if the operation cannot be performed. 

Bplib assumes the availAPIlity of a persistent queued storage system for managing the rate buffering that must occur between data and bundle processing. This storage system is provided at run-time by the application, which can either use its own or can use one of the bplib included storage services. In addition to the storage service, bplib needs an operating system interface provided at compile-time. By default a POSIX compliant operating systems interface is built with the included makefile - see below for further instructions on changing the operating system interface.

----------------------------------------------------------------------
## 2. Application Design
----------------------------------------------------------------------

![Figure 1](doc/bp_api_architecture.png "BP Library API (Architecture)")

Bplib is written in "vanilla C" and is intended to be linked in as either a shared or static library into an application with an API for reading/writing application data and reading/writing bundles.  

Conceptually, the library is meant to exist inside a board support package for an operating system and be presented to the application as a service.  In such a design only the interface for reading/writing data would be provided to the application, and the interface for reading/writing bundles would be kept inside the board support package.  This use case would look a lot like a typical socket application where a bundle channel (socket) is opened, data is read/written, and then at some later time the channel is closed.  Underneath the operating system (via the board support packet) would take care of sending and receiving the bundles.

In order to support bplib being used directly by the application, both the data and the bundle interfaces are provided in the API. In these cases, the application is also responsible for sending and receiving the bundles.

An example application design that manages both the data and bundle interfaces could look as follows:
1. A __bundle reader__ thread that receives bundles from a convergence layer and calls bplib to _process_ them
2. A __data writer__ thread that _accepts_ application data from bplib
3. A __bundle writer__ thread that _loads_ bundles from bplib and sends bundles over a convergence layer
4. A __data reader__ thread that _stores_ application data to bplib

The stream of bundles received by the application is handled by the bundle reader and data writer threads. The __bundle reader__ uses the `bplib_process` function to pass bundles read from the convergence layer into the library.  If those bundles contain payload data bound for the application, that data is pulled out of the bundles and queued in storage until the __data writer__ thread calls the `bplib_accept` function to dequeue the data out of storage and write it to the application.  

Conversely, the stream of bundles sent by the application is handled by the data reader and bundler writer threads. The __data reader__ thread calls `bplib_store` to pass data from the application into the library to be bundled.  Those bundles are queued in storage until the __bundle writer__ threads calls the `bplib_load` function to dequeue them out of storage and write them to the convergence layer.

----------------------------------------------------------------------
## 3. Build with Make
----------------------------------------------------------------------

Go to repository root directory and execute the following commands:
* `make`
* `sudo make install`

This will build the following binaries:
* `build/libbp.so.<version>` - shared library
* `build/libbp.a` - static library
* `bindings/lua/build/bplib.so` - lua extension module

And perform the following installations:
* `/usr/local/lib`: bplib libraries
* `/usr/local/inc`: bplib includes
* `/usr/local/lib/lua/5.3`: lua extensions and helper scripts

Additional make commands are as follows:
* `make clean` will remove all generated files and directories
* `make testmem` will call valgrind for detecting memory leaks
* `make testcpu` will call valgrind/callgrind for detecting cpu bottlenecks
* `make testheap` will call valgrind/massif for detecting sources of memory bloat
* `make testcov` will generate a line coverage report (if built and run with gcov, which is enabled by default)

On CentOS you may need to create a file with the conf extension in /etc/ld.so.conf.d that contains the line '/usr/local/lib'.
* `sudo echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf`
* `sudo ldconfig` 

Tailoring the build is accomplished by providing a custom configuration makefile.  By default `posix.mk` is used.  For example:
* `make CONFIG=<my_config_makefile>`
* `sudo make CONFIG=<my_config_makefile> install`


----------------------------------------------------------------------
## 4. Application Programming Interface
----------------------------------------------------------------------

#### 4.1 Functions

| Function        | Purpose |
| --------------- | ------- | 
| [bplib_init](#initialize)                | Initialize the BP library - called once at program start |
| [bplib_open](#open-channel)              | Open a channel - provides handle to channel for future channel operations |
| [bplib_close](#close-channel)            | Close a channel |
| [bplib_flush](#flush-channel)            | Flush active bundles on a channel |
| [bplib_config](#config-channel)          | Change and retrieve channel settings |
| [bplib_latchstats](#latch-statistics)    | Read out bundle statistics for a channel |
| [bplib_store](#store-payload)            | Create a bundle from application data and queue in storage for transmission |
| [bplib_load](#load-bundle)               | Retrieve the next available bundle from storage to transmit |
| [bplib_process](#process-bundle)         | Process a bundle for data extraction, custody acceptance, and/or forwarding |
| [bplib_accept](#accept-payload)          | Retrieve the next available data payload from a received bundle |
| [bplib_ackbundle](#acknowledge-bundle)   | Release bundle memory pointer for reuse (needed after bplib_load) |
| [bplib_ackpayload](#acknowledge-payload) | Release payload memory pointer for reuse (needed after bplib_accept) |
| [bplib_routeinfo](#route-information)    | Parse bundle and return routing information |
| [bplib_eid2ipn](#eid-to-ipn)             | Utility function to translate an EID string into node and service numbers |
| [bplib_ipn2eid](#ipn-to-eid)             | Utility function to translate node and service numbers into an EID string |
| [bplib_attrinit](#attr-init)             | Utility to initialize a channel attribute structure with default values.  Useful if the calling application only wants to change a few attributes without setting them all. |

__Note__: _functions that operate on a channel are thread-safe with other functions that operate on channels, but they are not thread-safe with the open and close functions.  A channel can only be closed when no other operations are being performed on it._


----------------------------------------------------------------------
##### Initialize

`void bplib_init (void)`

Initializes the BP library.  This must be called before any other call to the library is made.  It calls the operating system layer initialization routine.

----------------------------------------------------------------------
##### Open Channel

`bp_desc_t bplib_open (bp_route_t route, bp_store_t store, bp_attr_t* attributes)`

Opens a bundle channel that uses the provided endpoint IDs, storage service, and attributes. 

This function returns a channel handle that is used for all future operations on the channel.  The open and close calls are mutex'ed against other open and close calls, but once a channel is created, operations on that channel are only mutex'ed against other operations on the same channel.  A channel persists until it is closed.

`route` - a set of endpoing IDs defining the source, destination, and report to endpoints

* __local node__: The {node} number of the ipn:{node}.{service} endpoint ID used for the source and custody endpoints of bundles generated on the channel.

* __local service__: The {service} number of the ipn:{node}.{service} endpoint ID used for the source and custody enpoints of bundles generated on the channel.

* __destination node__: The {node} number of the ipn:{node}.{service} endpoint ID used for the destination enpoint of bundles generated on the channel.

* __destination service__: The {service} number of the ipn:{node}.{service} endpoint ID used for the destination enpoint of bundles generated on the channel.

* __report to node__: The {node} number of the ipn:{node}.{service} endpoint ID used for the report to enpoint of bundles generated on the channel.

* __report to service__: The {service} number of the ipn:{node}.{service} endpoint ID used for the report to enpoint of bundles generated on the channel.

`store` - a set of callbacks that provide access to the desired storage service.  See [Storage Service](#storage-service) section for more details.

`attributes` - set of characteristics and settings for the channel that trade memory usage and performance

* __lifetime__: Bundle generation parameter - the number of seconds since its creation that the bundle is valid.  Once the lifetime of a bundle expires, the bundle can be deleted by the bundle agent.

* __request_custody__: Bundle generation parameter - if set then the bundle request custody transfer and includes a CTEB extension block.

* __admin_record__: Bundle generation parameter - if set then the bundle is set as an adminstrative record.  The library handles this setting automatically for Aggregate Custody Signals that it generates; but if the user wants to create their own adminstrative record, then this attribute provides that option.

* __integrity_check__: Bundle generation parameter - if set then the bundle includes a BIB extension block.

* __allow_fragmentation__: Bundle generation parameter - if set then any generated or forwarded bundles on the channel will be fragmented if the size of the bundle exceeds the __max_length__ attribute of the channel; if not set, then any bundle generated or forwarded that exceeds the __max_length__ will be dropped.

* __cipher_suite__: Bundle generation parameter - provides the CRC type used inside the BIB extension block.  If the __integrity_check__ attribute is not set, then this setting is ignored.  If the __integrity_check__ attribute is set and this attribute is set to BP_BIB_NONE, then a BIB is included but the cipher result length is zero (this provide unambigous indication that no integrity check is included). Currently supported cipher suites are: BP_BIB_CRC16_X25, and BP_BIB_CRC32_CASTAGNOLI.

* __timeout__: The number of seconds the library waits before re-loading an unacknowledged bundle.

* __max_length__: The maximum size in bytes that a bundle can be, both on receipt and on transmission.

* __cid_reuse__: The library's behavior when a bundle times-out - if set, bundles that are retransmitted use the original Custody ID of the bundle when it was originally sent; if not set, then a new Custody ID is used when the bundle is retransmitted.  Re-using the Custody ID bounds the size of the Aggregrate Custody Signal coming back (worse-case gaps).  Using a new Custody ID makes the average size of the Aggregate Custody Signal smaller.

* __dacs_rate__: The maximum number of seconds to wait before an Aggregate Custody Signal which has accumulated acknowledgments is sent.  Every time a call to `bplib_load` is made, the code checks to see if there is an Aggregate Custody Signal which exists in memory but has not been sent for at least __dacs_rate__ seconds.

* __protocol_version__: Which version of the bundle protocol to use; currently the library only supports version 6.

* __retransmit_order__: The order in which bundles that have timed-out are retransmitted. There are currently two retransmission orders supported: BP_RETX_OLDEST_BUNDLE, and BP_RETX_SMALLEST_CID.

* __active_table_size__:  The number of unacknowledged bundles to keep track of. The larger this number, the more bundles can be sent before a "wrap" occurs (see BP_OPT_WRAP_RESPONSE).  But every unacknowledged bundle consumes 8 bytes of CPU memory making this attribute the primary driver for a channel's memory usage.

* __max_fills_per_dacs__: The maximum number of fills in the Aggregate Custody Signal.  An Aggregate Custody Signal is sent when the maximum fills are reached or the __dacs_rate__ period has expired (see BP_OPT_DACS_RATE).

* __max_gaps_per_dacs__: The maximum number of Custody ID gaps a channel can keep track up when receiving bundles requesting custody transfer.  If this gap limit is reached, the Aggregate Custody Signal is sent and a new one immediately begins to accumulate acknowledgments.

* __storage_service_parm__: A pass through to the storage service `create` function.

`returns` - channel descriptor. 

----------------------------------------------------------------------
##### Close Channel

`void bplib_close (bp_desc_t channel)`

Closes the specified bundle channel and releases all run-time resources associated with it; this does not include the bundles stored in the storage service; nor does it include bundles that have been transmitted but not yet acknowledged (active bundles).  The close call is not mutex'ed against other channel operations - it is the caller's responsibility that the close call is made non-concurrently with any other library function call on that channel.

`channel` - which channel to close

----------------------------------------------------------------------
##### Flush Channel

`int bplib_flush (bp_desc_t channel)`

Flushes all active bundles on a channel; this treats each bundle that has been transmitted but not yet acknowledged as if it was immediately acknowledged.  This function is separate from the bplib_close function because it is possible that a storage service supports resuming where it left off after a channel is closed.  In such a case, closing the channel would occur without flushing the data since the next time the channel was opened, the data that had not yet been relinquished would resume being sent.

`channel` - which channel to flush

----------------------------------------------------------------------
##### Config Channel

`int bplib_config (bp_desc_t channel, int mode, int opt, void* val, int len)`

Configures or retrieves an attribute on a channel.

`channel` - which channel to configure or retrieve attribute

`mode` - whether to read or write the attribute

* BP_OPT_MODE_READ: the attribute is read and placed into the memory localtion pointed to by _val_

* BP_OPT_MODE_WRITE: the attribute is written with the value stored at the memory location pointed to by _val_

`opt` - the attribute to perform the operation on, as described in the table below.  The different attributes that can be changed or read are further described in the [Open Channel](#open-channel) section.

| Option                 | Units    | Default | Description |
| ---------------------- | -------- | ------- | ----------- |
| BP_OPT_LIFETIME        | int      | 0 | Amount of time in seconds added to creation time specifying duration of time bundle is considered valid, 0: infinite |
| BP_OPT_REQUEST_CUSTODY | int      | 1 | Sets whether transmitted bundles request custody transfer, 0: false, 1: true |
| BP_OPT_ADMIN_RECORD    | int      | 0 | Sets whether generated bundles are administrative records, 0: false, 1: true |
| BP_OPT_INTEGRITY_CHECK | int      | 1 | Sets whether transmitted bundles include a BIB extension block, 0: false, 1: true |
| BP_OPT_ALLOW_FRAGMENTATION | int  | 1 | Sets whether transmitted bundles are allowed to be fragmented, 0: false, 1: true |
| BP_OPT_CIPHER_SUITE    | int      | BP_BIB_CRC16_X25 | The type of Cyclic Redundancy Check used in the BIB extension block - BP_BIB_NONE, BP_BIB_CRC16_X25, BP_BIB_CRC32_CASTAGNOLI |
| BP_OPT_TIMEOUT         | int      | 10 | Amount of time in seconds to wait for positive acknowledgment of transmitted bundles before retransmitting, 0: infinite |
| BP_OPT_MAX_LENGTH      | int      | 4096 | Maximum length of the transmitetd bundles |
BP_WRAP_BLOCK, BP_WRAP_DROP |
| BP_OPT_CID_REUSE       | int      | 0 | Sets whether retransmitted bundles reuse their original custody ID, 0: false, 1: true |
| BP_OPT_DACS_RATE       | int      | 5 | Sets minimum rate of ACS generation |

__NOTE__: _transmitted_ bundles include both bundles generated on the channel from local data that is stored, as well as bundles that are received and forwarded by the channel.

`val` - the value set or returned

`len` - the length in bytes of the memory pointed to by _val_

`returns` - [return code](#4.2-return-codes).

----------------------------------------------------------------------
##### Latch Statistics

`int bplib_latchstats (bp_desc_t channel, bp_stats_t* stats)`

Retrieve channel statistics populated in the structure pointed to by _stats_.

`channel` - channel to retrieve statistics on

`stats` - pointer to the statistics structure to be populated

* __lost__: number of deleted bundles due to: storage failure, and memory copy failure

* __expired__: number of deleted bundles due to their lifetime expiring

* __acknowledged__: number of deleted bundes due to a custody signal acknowledgment

* __transmitted__: number of bundles returned by the `bplib_load` function

* __retransmitted__: number of bundles returned by the `bplib_load` function because the bundle timed-out and is being resent

* __received__: number of bundles passed to the `bplib_process` function; includes bundles that are mal-formed and return an error

* __generated__: number of bundles successfully generated via the `bplib_store` function

* __delivered__: number of bundle payloads delivered to the application via the `bplib_accept` function

* __bundles__: number of data bundles currently in storage

* __payloads__: number of payloads currently in storage

* __records__: number of aggregate custody signal bundles currently in storage

* __active__: number of bundles that have been loaded for which no acknowledgment has been received

----------------------------------------------------------------------
##### Store Payload

`int bplib_store (bp_desc_t channel, void* payload, int size, int timeout, uint16_t* flags)`

Initiates sending the data pointed to by _payload_ as a bundle. The data will be encapsulated in a bundle (or many bundles if the channel allows fragmentation and the payload exceeds the maximum bundle length) and queued in storage for later retrieval and transmission.

`channel` - channel to create bundle on

`payload` - pointer to data to be bundled

`size` - size of payload in bytes

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`flags` - flags that provide additional information on the result of the store operation (see [flags](#6-3-flag-definitions)).  The flags variable is not initialized inside the function, so any value it has prior to the function call will be retained.

`returns` - size of bundle created in bytes, or [return code](#4.2-return-codes) on error.

----------------------------------------------------------------------
##### Load Bundle

`int bplib_load (bp_desc_t channel, void** bundle,  int* size, int timeout, uint16_t* flags)`

Reads the next bundle from storage to be sent by the application over the convergence layer.  From the perspective of the library, once a bundle is loaded to the application, it is as good as sent.  Any failure of the application to send the bundle is treated no differently that a failure downstream in the bundle reaching its destination.  On the other hand, the memory containing the bundle returned by the library is kept valid until the `bplib_ackbundle` function is called, which must be called once for every returned bundle.  So while subsequent calls to `bplib_load` will continue to provide the next bundle the library determines should be sent, the application is free to hold onto the bundle buffer and keep trying to send it until it acknowledges the bundle to the library.

`channel` - channel to retrieve bundle from

`bundle` - pointer to a bundle buffer pointer; on success, the library will populate this pointer with the address of a buffer containing the bundle that is loaded.

`size` - pointer to a variable holding the size in bytes of the bundle buffer being returned, populated on success.

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`flags` - flags that provide additional information on the result of the load operation (see [flags](#6-3-flag-definitions)). The flags variable is not initialized inside the function, so any value it has prior to the function call will be retained.

`returns` - the bundle reference, the size of the bundle, and [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Process Bundle

`int bplib_process (bp_desc_t channel, void* bundle,  int size, int timeout, uint16_t* flags)`

Processes the provided bundle.

There are three types of bundles processed by this function:
(1) If the bundle is an aggregate custody signal, then any acknowledged bundles will be freed from storage.
(2) If the bundle is destined for the local node, then the payload data will be extracted and queued for retrieval by the application; and if custody is requested, then the current aggregate custody signal will be updated and queued for transmission if necessary.
(3) If the bundle is not destined for the local node, then the bundle will be queued for transmission as a forwarded bundle; and if custody is requested, then the current aggregate custody signal will be updated and queued for transmission if necessary.

`channel` - channel to process bundle on

`bundle` - pointer to a bundle

`size` - size of the bundle in bytes

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`flags` - flags that provide additional information on the result of the process operation (see [flags](#6-3-flag-definitions)). The flags variable is not initialized inside the function, so any value it has prior to the function call will be retained.

`returns` - [return code](#4.2-return-codes).

----------------------------------------------------------------------
##### Accept Payload

`int bplib_accept (bp_desc_t channel, void** payload, int* size, int timeout, uint16_t* flags)`

Returns the next available bundle payload (from bundles that have been received and processed via the `bplib_process` function) to the application. The memory containing the payload returned by the library is kept valid until the `bplib_ackpayload` function is called, which must be called once for every returned payload.  So while subsequent calls to `bplib_accept` will continue to provide the next payload the library determines should be accepted, the payload will not be deleted from the library's storage service until it is acknowledged by the application.

`channel` - channel to accept payload from

`payload` - pointer to a payload buffer pointer; on success, the library will populate this pointer with the address of a buffer containing the payload that is accepted.

`size` - pointer to a variable holding the size in bytes of the payload buffer being returned, populated on success.

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`flags` - flags that provide additional information on the result of the accept operation (see [flags](#6-3-flag-definitions)). The flags variable is not initialized inside the function, so any value it has prior to the function call will be retained.

`returns` - the payload reference, the size of the payload, and [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Acknowledge Bundle

`int bplib_ackbundle (bp_desc_t channel, void* bundle)`

Informs the library that the memory and storage used for the payload can be freed.  The memory will be immediately freed, the storage will be freed immediately only if the bundle is not requesting custody transfer (otherwise, if the bundle is requesting custody transfer, then the ACS acknowledgment frees the storage).  This must be called at some point after every bundle that is loaded.

`channel` - channel to acknowlwedge bundle

`bundle` - pointer to the bundle buffer to be acknowledged

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Acknowledge Payload

`int bplib_ackpayload (bp_desc_t channel, void* payload)`

Informs the library that the memory and storage used for the payload can be freed.  This must be called at some point after every payload that is accepted.

`channel` - channel to acknowlwedge payload

`payload` - pointer to the payload buffer to be acknowledged

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Route Information

`int bplib_routeinfo (void* bundle, int size, bp_route_t* route)`

Parses the provided bundle and supplies its endpoint ID node and service numbers.  Used to route a received bundle to the appropriate channel by looking up its destination endpoint ID prior to making other library calls that require a channel identifier.

`bundle` - pointer to a buffer of memory containing a properly formatted bundle

`size` - size of the bundle

`route` - pointer to a route structure that is populated by the function (see [Open Channel](#open-channel) for more details on the structure contents).

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### EID to IPN

`int bplib_eid2ipn (const char* eid, int len, bp_ipn_t* node, bp_ipn_t* service)`

Convert a enpoint ID string into the IPN node and service numbers

`eid` - string containing the endpoint ID

`len` - length of the __eid__ string

`node` - pointer to variable that will be populated with the node number

`service` - pointer to the variable that will be populated with the service number

----------------------------------------------------------------------
##### IPN to EID

`int bplib_ipn2eid (char* eid, int len, bp_ipn_t node, bp_ipn_t service)`

Convert an IPN node and service number to an enpoint ID string

`eid` - pointer to a buffer that will be populated with the endpoint ID string

`len` - length of the __eid__ buffer

`node` - node number used to populate the endpoint ID string

`service` - service number used to populate the endpoint ID string

----------------------------------------------------------------------
##### Initialize Attributes

`int bplib_attrinit (bp_attr_t* attr)`

Initialize an attribute structure with the library default values.  This is useful when creating a channel where only a few attributes need to be changed.

`attr` - pointer to attributes structure populated by the function (see [Open Channel](#open-channel) for more details on the attributes structure contents).

----------------------------------------------------------------------
#### 4.2 Return Codes

| Code                    | Value | Description |
| ----------------------- | ----- | ----------- |
| BP_SUCCESS              | 1     | Operation successfully performed |
| BP_TIMEOUT              | 0     | A timeout occurred when a blocking operation was performed |
| BP_ERROR                | -1    | Generic error occurred; further information provided in flags to determine root cause |
| BP_PARMERR              | -2    | Parameter passed into function was invalid |
| BP_UNSUPPORTED          | -3    | A valid bundle could not be processed by the library because the requested functionality is not yet implemented |
| BP_EXPIRED              | -4    | A bundle expired due to its lifetime and was deleted |
| BP_DROPPED              | -5    | A bundle was dropped because it could not be processed |
| BP_INVALIDHANDLE        | -6    | The handle passed into a storage service function was invalid |
| BP_WRONGVERSION         | -7    | The primary block bundle version number did not match the CCSDS recommended version |
| BP_BUNDLEPARSEERR       | -8    | An error was encountered when trying to read or write a bundle, usually associated with either an SDNV overflow or a buffer that is too small |
| BP_UNKNOWNREC           | -9    | The administrative record type was unrecognized by the library |
| BP_BUNDLETOOLARGE       | -10   | The size of the bundle exceeded the maximum size bundle that is able to be processed by the library |
| BP_WRONGCHANNEL         | -11   | The destination service number did not match the channel's source service number when trying to processing a bundle destined for the local node |
| BP_FAILEDINTEGRITYCHECK | -12   | A bundle processed by the library contained a Bundle Integrity Block, but the checksum contained in that block did not match the calculated checksum |
| BP_FAILEDSTORE          | -13   | The library encountered an error originating from the storage service |
| BP_FAILEDOS             | -14   | The library encountered an error originating from the operation system abstraction layer |
| BP_FAILEDMEM            | -15   | The library encountered an error allocating memory for a channel |
| BP_FAILEDRESPONSE       | -16   | The library was unable to report back to another node, e.g. a DACS bundle could not be created or sent due to there being too many sources to track |
| BP_INVALIDEID           | -17   | An EID string did not contain a valid IPN address |
| BP_INVALIDCIPHERSUITEID | -18   | A BIB block as an unrecognized Cipher Suite ID  |
| BP_DUPLICATECID         | -19   | A custody ID was already present in the system either when loading a new bundle or acknowledging a received bundle  |
| BP_CUSTODYTREEFULL      | -20   | The receiption and custody transfer of a bundle exhausted the memory allocated to keep track of custody and caused a DACS bundle to be immediately issued |
| BP_ACTIVETABLEFULL      | -21   | No bundle can be loaded because the active table cannot accept any more bundles |
| BP_CIDNOTFOUND          | -22   | The custody ID provided in an acknowledgement did not match any active bundle |
| BP_PENDINGACKNOWLEDGMENT| -23   | An aggregate custody signal was unable to be processed due to a library failure |
| BP_PENDINGFORWARD       | -24   | A forwarded bundle requesting custody transfer failed to be acknowledged due to a library failure |
| BP_PENDINGACCEPTANCE    | -25   | A bundle destined for the local node requesting custody transfer failed to be acknowledged due to a library failure |

----------------------------------------------------------------------
#### 4.3 Flag Definitions

| Flag                     | Value | Description |
| ------------------------ | ----- | ----------- |
| BP_FLAG_NONCOMPLIANT     | 0x0001 | Valid bundle but the library was not able to comply with the standard |
| BP_FLAG_INCOMPLETE       | 0x0002 | At least one block in bundle was not recognized |
| BP_FLAG_UNRELIABLETIME   | 0x0004 | The time returned by the O.S. preceded the January 2000 epoch, or went backwards |
| BP_FLAG_FILLOVERFLOW     | 0x0008 | A gap in the CIDs exceeds the max fill value allowed in an ACS bundle |
| BP_FLAG_TOOMANYFILLS     | 0x0010 | All the fills in the ACS are used |
| BP_FLAG_CIDWENTBACKWARDS | 0x0020 | The custody ID went backwards |
| BP_FLAG_ROUTENEEDED      | 0x0040 | The bundle returned needs to be routed before transmission |
| BP_FLAG_STOREFAILURE     | 0x0080 | Storage service failed to deliver data |
| BP_FLAG_UNKNOWNCID       | 0x0100 | An ACS bundle acknowledged a CID for which no bundle was found |
| BP_FLAG_SDNVOVERFLOW     | 0x0200 | The local variable used to read/write and the value was of insufficient width |
| BP_FLAG_SDNVINCOMPLETE   | 0x0400 | There was insufficient room in block to read/write value |
| BP_FLAG_ACTIVETABLEWRAP  | 0x0800 | The active table wrapped; see BP_OPT_WRAP_RESPONSE |
| BP_FLAG_DUPLICATES       | 0x1000 | The custody ID was already acknowledged |
| BP_FLAG_RBTREEFULL       | 0x2000 | An aggregate custody signal was generated due the number of custody ID gaps exceeded the maximum allowed |

----------------------------------------------------------------------
## 5. Storage Service
----------------------------------------------------------------------

The application is responsible for providing the storage service to the library at run-time through call-backs passed to the `bplib_open` function.  

----------------------------------------------------------------------
##### Create Storage Service

`int create (void* parm)`

Creates a storage service.

`parm` - service specific parameters pass through library to this function.  See the storage_service_parm of the attributes structure passed to the `bplib_open` function.

`returns` - handle for storage service used in subsequence calls.

----------------------------------------------------------------------
##### Destroy Storage Service

`int destroy (int handle)`

Destroys a storage service.  This does not mean that the data stored in the service is freed - that is service specific.

`handle` - handle to the storage service

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Enqueue Storage Service

`int enqueue (int handle, void* data1, int data1_size, void* data2, int data2_size, int timeout)`

Stores the pointed to data into the storage service.

`handle` - handle to the storage service

`data1` - pointer to first block of memory to store.  This must be concatenated with __data2__ by the function into one continuous block of data.

`data1_size` - size of first block of memory to store.

`data2` - pointer to second block of memory to store.  This must be concatenated with __data1__ by the function into one continuous block of data.

`data2_size` - size of the second block of memory to store.

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Dequeue Storage Service

`int dequeue (int handle, void** data, int* size, bp_sid_t* sid, int timeout)`

Retrieves the oldest data block stored in the storage service that has not yet been dequeued, and returns a _Storage ID_ that can be used to retrieve the data block in the future.

`handle` - handle to the storage service

`data` - the pointer that will be updated to point to the retrieved block of memory.  This function returns the data block via a pointer and performs no copy.  The data is still owned by the storage service and is only valid until the next dequeue or relinquish call.

`size` - size of data block being retrieved.

`sid` - pointer to a _Storage ID_ variable populated by the function.  The sid variable is used in future storage service functions to identify the retrieved data block.

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Retrieve Storage Service

`int retrieve (int handle, void** data, int* size, bp_sid_t sid, int timeout)`

Retrieves the data block stored in the storage service identified by the _Storage ID_ sid parameter.

`handle` - handle to the storage service

`data` - the pointer that will be updated to point to the retrieved block of memory.  This function returns the data block via a pointer and performs no copy.  The data is still owned by the storage service and is only valid until the next dequeue or relinquish call.

`size` - size of data block being retrieved.

`sid` - the _Storage ID_ that identifies which data block to retrieve from the storage service

`timeout` - 0: check, -1: pend, 1 and above: timeout in milliseconds

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Release Storage Service

`int release (int handle, bp_sid_t sid)`

Releases any in-memory resources associated with the dequeueing or retrieval of a bundle.

`handle` - handle to the storage service

`sid` - the _Storage ID_ that identifies the data block for which memory resources are released.

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Relinquish Storage Service

`int relinquish (int handle, bp_sid_t sid)`

Deletes the stored data block identified by the _Storage ID_ sid parameter.

`handle` - handle to the storage service

`sid` - the _Storage ID_ that identifies which data block to delete from storage

`returns` - [return code](#4.2-return-codes)

----------------------------------------------------------------------
##### Get Count Storage Service

`int getcount (int handle)`

Returns the number of data blocks currently stored in the storage service.

`handle` - handle to the storage service

`returns` - number of data blocks

----------------------------------------------------------------------
The storage service call-backs must have the following characteristics:
* `enqueue`, `dequeue`, `retrieve`, and `relinquish` are expected to be thread safe against each other.
* `create` and `destroy` do not need to be thread safe against each other or any other function call - the application is responsible for calling them when it can complete atomically with respect to any other storage service call
* The memory returned by the dequeue and retrieve function is valid until the release function call.  Every dequeue and retrieve issued by the library will be followed by a release.
* The _Storage ID (SID)_ returned by the storage service cannot be zero since that is marked as a _VACANT_ SID

 
