NVPhTM: An Efficient Phase-Based Transactional System for Non-Volatile Memory
===============

This is the source code for NVPhTM, published in Euro-Par 2020.

You will need a processor with TSX (RTM) support to run the system. NVPhTM
uses a phase-based TM, particularly PhTM\*. The software module is based on
NOrec and the durable HTM on NV-HTM.


Quick Start
-----------

Build the memory allocator binaries:

`cd allocators`
`./gen-allocators.sh`
`cd ..`

Then you need to build the TM libraries and compile the STAMP applications.
There is a script to automate the whole process. Just issue:

`./scripts/compile -b 'seq_nvm nvphtm_pstm nvm_rtm pstm' -s 'genome intruder kmeans labyrinth ssca2 vacation yada' -P HTM_STATUS_PROFILING -P TIME_MODE_PROFILING`

The options after the flag `-b` are the TM systems:

* seq\_nvm - sequential persistent version without concurrency control
* nvphtm\_pstm - this is NVPhTM
* nvm\_rtm - persistent HTM (based on NV-HTM)
* pstm - persistent STM (based on NOrec)

The flag `-s` specifies which STAMP applications should be compiled. Flag `-P`
turns on some statistics collection.

There is another script to execute the applications. Just type:

`./scripts/execute -t 1 -n 5 -M ibmtcmalloc -b 'seq_nvm' -s 'genome intruder kmeans labyrinth ssca2 vacation yada'`

It will execute the applications specified after `-s` using the system after
`-b`, in this case the sequential version. Flag `-M` specifies the memory
allocator and `-n` how many times each experiment should be repeated (in this
example, 5). Finally, the flag `-t` specifies how many threads should be used
(1 here, because it is the sequential version).

After the script is over, you will notice a new directory named
`results-[day-time]`. Inside this directory there will be a summary of the executions
and in the subfolder `logs` you will find the output for each experiment executed.


To plot the graphs, take a look at 'plot-table.py' and change the variables
pointed out in the file to reflect your settings. 

