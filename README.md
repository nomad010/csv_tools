# csv_tools
A collection of tools for manipulating CSV files

split_csv - A tool for splitting csv into column files.

General information
===================

Decomposes a CSV consisting of several columns into a several files each containing a single column. The output files themselves are in CSV format. Refer to RFC 4180 for details on the format this program expects. Other formats may result in unusual or incorrect behaviour. This program may be useful for performing analysis on individual columns of a CSV file. Non-rectangular CSVs are handled by outputting blank lines to the missing rows. The column files have the XXX.csv suffix.

The code has been somewhat written for performance using the restartable IO buffer method, although no benchmarking has been performed and definite improvements could be made. The hope is that it uses both the CPU and IO efficiently enough to max out the current generation of SSDs. However performance is dependent on many factors such as the average item length, what types are used, the speed of your disk, whether the file is in OS cache, etc. Most of the work is done by rawmemchr calls which are usually implemented to take advantage of CPU features.

The performance of the program can be decomposed as follows:

T<sub>total</sub> = T<sub>input</sub> + T<sub>output</sub> + T<sub>CPU</sub> where T is time.

Which is:

T<sub>total</sub> = S<sub>input</sub>/V<sub>input</sub> + S<sub>output</sub>/V<sub>output</sub> + T<sub>CPU</sub> where S is size and V is speed.

* For rectangular CSVs we write roughly the same amount as read: T<sub>total</sub> = S/V<sub>input</sub> + S/V<sub>output</sub> + T<sub>CPU</sub>

These figures can be collected by running the command under GNU time(usually /usr/bin/time) with the -v option.
I believe T<sub>wall</sub> corresponds to T<sub>total</sub>, and T<sub>user</sub> + T<sub>system</sub>	corresponds to T<sub>CPU</sub>.

Issues
======
 * More and better tests. Currently only incidental tests have been performed, however.
 * Unicode variants should be handled. Although the code should deal with UTF-8, however RFC 4180 makes no mention of Unicode and whether a 'FULLWIDTH COMMA(U+FF0C)' should be treated as a regular comma.
 * Some of the code paths assert rather than throw a bonafide error.

Enhancements
============
 * Make code more robust to arbitrary CSV formats.
 * More tests.
 * Verbosity mode.
