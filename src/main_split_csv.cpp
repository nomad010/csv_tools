#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include "csv_splitter.hpp"

void print_help()
{
    static auto help = R"help(split_csv - A tool for splitting csv into column files.

Syntax:
    ./split_csv [OPTIONS] <input_filename>

Decomposes a CSV consisting of several columns into a several files each 
containing a single column. The files themselves are in CSV format. Refer to
RFC 4180 for details on the format this program expects. Other formats may
result in unusual or incorrect behaviour. This program may be useful for
performing analysis on individual columns of a CSV file. Non-rectangular CSVs
are handled by outputting blank lines to the missing rows. The column files 
have the XXX.csv suffix.

Options:
    --help               Prints this message and exit before processing.
    --prefix=<name>      A prefix for the name of all the output files. The 
                         number of the column and '.csv' will be appended to 
                         give the complete filename. By default this is empty.
                         The program will fail if the prefix points to a 
                         non-existent directory.
Arguments:
    <input_filename>     The name of the input filename. Input can be read from
                         stdin by specifying -.
    <output_prefix>      
                         
Example usage:
  # Read a CSV file from stdin and save the output to the current directory 
    with the col prefix.
  ./split_csv --prefix=col -

  # Read a CSV file from stdin and save the output to the output_directory with
    the col prefix.
  ./split_csv --prefix=output_directory/col -
)help";
    printf("%s", help);
}

int main(int argc, char** argv)
{
    if(argc == 1)
    {
        /// Not enough arguments
        print_help();
        return 1;
    }
    else
    {
        /// Capture input source
        std::string input_filename(argv[argc - 1]);
        
        int input_fd;
        if(input_filename == "-")
        {
            input_fd = STDIN_FILENO;
        }
        else
        {
            input_fd = open(input_filename.c_str(), O_RDONLY);
            if(input_fd == -1)
            {
                perror("Error opening input file");
                exit(1);
            }
            /// Tell the OS, we need to read sequentially on the file, if there is an error, well we tried our best.
            posix_fadvise(input_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        }
        
        std::string prefix;
        /// Process other arguments
        for(int i = 1; i < argc - 1; i++)
        {
            std::string arg = argv[i];
            if(arg == "--help")
            {
                print_help();
                return 0;
            }
            else if(arg.substr(0, 9) == "--prefix=")
            {
                prefix = arg.substr(9);
            }
        }
        
        split_csv(input_fd, prefix);
    }
}