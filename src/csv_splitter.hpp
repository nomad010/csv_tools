#include <alloca.h>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <type_traits>
#include <vector>

/** 
 * ~16K byte buffer sizes, this can have an impact on performance.
 */
static const size_t BUFFER_SIZE = 16*1024; 

/**
 * We would like to prefer stack memory if possible. 
 * This method says whether we need to use the heap, because our buffer size is too large.
 * If 90% of the stack size is smaller than the two buffers, use the heap.
 */
bool should_use_heap(size_t number_of_chars)
{
    static const size_t required_stack = sizeof(uint8_t)*number_of_chars;
    struct rlimit rlimit_info;
    int result = getrlimit(RLIMIT_STACK, &rlimit_info);
    if (result == 0)
    {
        size_t available_stack = (9*rlimit_info.rlim_cur)/10;
        return available_stack < required_stack;
    }
    else
    {
        /// Failed to get info, use the heap
        return true;
    }
}

/**
 * Use malloc if we have to, otherwise use alloca for the stack.
 * This has to be a macro for alloca
 */
#define ALLOCATE_BUFFER(use_heap, buffer, sz) \
if(use_heap)\
    buffer = static_cast<uint8_t*>(malloc(sz));\
else\
    buffer = static_cast<uint8_t*>(alloca(sz));\
    
    
/**
 * Create column info for a fresh column.
 * This has to be a macro for alloca
 * TODO: More error handling.
 */
#define CREATE_COLUMN_INFO(info, fname, id) \
static char id_buffer[24];\
sprintf(id_buffer, "%03zu", id);\
info.output_fd = open((fname + std::string(id_buffer) + ".csv").c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);\
if(info.output_fd == -1)\
{\
    perror("Error opening file for writing");\
    exit(1);\
}\
info.buffer_position = 0;\
info.buffer_size = BUFFER_SIZE;\
info.on_heap = should_use_heap(BUFFER_SIZE);\
ALLOCATE_BUFFER(info.on_heap, info.buffer, BUFFER_SIZE);

/**
 * Checks for the existence of a column.
 * This has to be a macro for alloca.
 */
#define CHECK_AND_CREATE_COLUMN \
if(__builtin_expect(current_column == column_infos.size(), 0))\
{\
    column_infos.resize(column_infos.size() + 1);\
    ColumnInfo& info = column_infos.back();\
    CREATE_COLUMN_INFO(info, name, column_infos.size());\
    add_chars_to_column(info, '\n', current_row);\
}


class ColumnInfo
{
public:
    int output_fd;
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_position;
    bool on_heap;
};

void flush_buffer(ColumnInfo& column)
{
    ssize_t remaining_count = column.buffer_position;
    auto write_result = write(column.output_fd, column.buffer, remaining_count);
    assert(write_result == remaining_count);
    column.buffer_position = 0; /// Have to make sure we reset the position on the buffer.
}

void add_buffer_to_column(ColumnInfo& column, uint8_t* from_buffer, size_t copy_size)
{
    size_t remaining_buffer = column.buffer_size - column.buffer_position;
    while(copy_size >= remaining_buffer)
    {
        /// Write as much of the from_buffer as possible to fill up the output buffer.
        memcpy(column.buffer + column.buffer_position, from_buffer, remaining_buffer);
        column.buffer_position += remaining_buffer;
        
        /// Write out to the fd
        flush_buffer(column);
        
        /// Alter input by how much we were able to write
        copy_size -= remaining_buffer;
        from_buffer += remaining_buffer;
        remaining_buffer = column.buffer_size;
    }
    
    if(copy_size != 0)
    {
        /// Write remainder of buffer to much of the from_buffer as possible to fill up the output buffer.
        memcpy(column.buffer + column.buffer_position, from_buffer, copy_size);
        column.buffer_position += copy_size;
    }
}

void add_chars_to_column(ColumnInfo& column, uint8_t chr, size_t copy_size)
{
    size_t remaining_buffer = column.buffer_size - column.buffer_position;
    
    if(column.buffer_position + copy_size <= column.buffer_size)
    {
        /// If the copy fits then we are fine and we just memset
        memset(column.buffer + column.buffer_position, chr, copy_size);
        column.buffer_position += copy_size;
    }
    else
    {
        /// If the copy size overflows it we output the buffer first
        if(column.buffer_position != 0)
            flush_buffer(column);
        
        if(copy_size >= column.buffer_size)
        {
            /// If the copy size is bigger than the buffer size we only need to memset once
            /// Fill the buffer once and write it multiple times
            memset(column.buffer, chr, column.buffer_size);
            
            while(copy_size >= column.buffer_size)
            {
                flush_buffer(column);
                copy_size -= column.buffer_size;
            }
            
            column.buffer_position = copy_size;
        }
        else
        {
            /// Just fill the buffer
            memset(column.buffer, chr, copy_size);
            column.buffer_position = copy_size;
        }
    }
}

/// Allows us to write the code like a state machine and can stopped and resumed on buffer boundaries
enum CSVState
{
    OnRowInitial, /// This means that we are on a newline
    OnColumnInitial, /// This means that we are on a comma
    InSimpleColumn, /// This means that we write to the same column until we hit a comma
    InQuotedStringColumn, /// This means that we are in a quoted string and we ended on a non-quote
    InQuotedStringColumnOnQuote /// This means we are in a quoted string and we ended on a quote
};
    
/**
 * This is the main loop of the function.
 * Read a chunk from the input file.
 *   Copy each item to the respective output buffer
 *     If the output buffer fills up
 *       Write to the respective output file
 *       Copy the remainder of the of the output column
 */
void split_csv(int input_fd, std::string name)
{
    bool input_buffer_on_heap = should_use_heap(BUFFER_SIZE + 10);
    /// Allocate more space for the input buffer for sentinel characters
    uint8_t* input_buffer; 
    ALLOCATE_BUFFER(input_buffer_on_heap, input_buffer, BUFFER_SIZE + 10);
    input_buffer[BUFFER_SIZE] = '\n';
    input_buffer[BUFFER_SIZE + 1] = ',';
    input_buffer[BUFFER_SIZE + 2] = '"';
    uint8_t* NEWLINE_SENTINEL = input_buffer + BUFFER_SIZE;
    uint8_t* COMMA_SENTINEL = input_buffer + BUFFER_SIZE + 1;
    uint8_t* DQUOTE_SENTINEL = input_buffer + BUFFER_SIZE + 2;
    uint8_t* next_newline;
    std::vector<ColumnInfo> column_infos;
    size_t current_row = 0;
    size_t current_column = 0;
    CSVState current_state = OnColumnInitial;
    
    while(true)
    {
        read_chunk:;
        auto bytes_total = read(input_fd, input_buffer, BUFFER_SIZE); /// Bytes total represent's the input chunk size
        if(__builtin_expect(bytes_total == -1, 0))
        {
            /// Error with read
            perror("Error reading file");
            exit(1);
        }
        else if(__builtin_expect(bytes_total == 0, 0))
        {
            /// No more data - make sure every column has been output
            
            /// Flush buffers and free them up
            for(auto& c : column_infos)
            {
                flush_buffer(c);
                if(c.on_heap)
                    free(c.buffer);
            }
            
            /// Flush input buffer
            if(input_buffer_on_heap)
                free(input_buffer);
            
            break;
        }
        else
        {
            /// Successfully read a chunk - expect a full size chunk, if we don't get one we are at the end and we add sentinels
            if(__builtin_expect(bytes_total != BUFFER_SIZE, 0))
            {
                input_buffer[bytes_total] = '\n';
                input_buffer[bytes_total + 1] = ',';
                input_buffer[bytes_total + 2] = '"';
                NEWLINE_SENTINEL = input_buffer + bytes_total;
                COMMA_SENTINEL = input_buffer + bytes_total + 1;
                DQUOTE_SENTINEL = input_buffer + bytes_total + 2;
            }
            
            uint8_t* previous_ptr = input_buffer; /// Represents the one past last position where we last wrote or the beginning of a chunk.
            /// Compute the next newline position and transform it into a comma if it's not the sentinel
            if(current_state != OnRowInitial)
            {
                /// This is gated because if current_state == OnRowInitial then both searches for the
                /// newline will go through, this is bad because we overwrite the newline character so we 
                /// won't find the correct one.
                next_newline = static_cast<uint8_t*>(rawmemchr(previous_ptr, '\n'));
                if(next_newline != NEWLINE_SENTINEL)
                    *next_newline = ',';
            }
            
            while(true)
            {
                state_begin:;
                if(__builtin_expect(previous_ptr == NEWLINE_SENTINEL, 0))
                    goto read_chunk; /// We are at the end of a chunk
                
                switch(current_state)
                {
                    case OnRowInitial:
                        /// We need to update the unread columns with newlines
                        while(current_column != column_infos.size())
                            add_chars_to_column(column_infos[current_column++], '\n', 1);
                        current_column = 0;
                        current_row++;
                        /// Compute the next newline position and transform it into a comma if it's not the sentinel
                        next_newline = static_cast<uint8_t*>(rawmemchr(previous_ptr, '\n'));
            
                        if(next_newline != NEWLINE_SENTINEL)
                            *next_newline = ',';
                        /// Note this falls through to OnColumnInitial
                    case OnColumnInitial:
                        if(__builtin_expect(*previous_ptr == '"', 0))
                        {
                            /// The beginning of a escaped string
                            /// Create the column and add an opening "
                            CHECK_AND_CREATE_COLUMN;
                            add_chars_to_column(column_infos[current_column], '"', 1);
                            
                            previous_ptr++;
                            current_state = InQuotedStringColumn;
                            goto state_begin;
                        }
                        else if(__builtin_expect(*previous_ptr == ',', 0))
                        {
                            /// Empty column - or maybe the end of the line
                            CHECK_AND_CREATE_COLUMN;
                            if(__builtin_expect(previous_ptr == next_newline, 0))
                            {
                                /// This is an end of line
                                
                                /// Update position
                                ++previous_ptr;
                                
                                // Go to OnRowInitial
                                current_state = OnRowInitial;
                                goto state_begin;
                            }
                            else
                            {
                                /// This is just an empty column
                                
                                /// Add a newline and update position
                                add_chars_to_column(column_infos[current_column++], '\n', 1);
                                ++previous_ptr;
                                
                                /// Go to OnColumnInitial
                                current_state = OnColumnInitial;
                                goto state_begin;
                            }
                        }
                        else if(__builtin_expect(*previous_ptr == '\n', 0))
                        {
                            /// Empty column at end of line
                            /// Write single newlines to remaining columns
                            /// Newlines should be comma-ified
                            assert(false);
                        }
                        else
                        {
                            /// A normal unescaped string column
                            CHECK_AND_CREATE_COLUMN;
                            current_state = InSimpleColumn;
                            goto state_begin;
                        }
                        break;
                    case InSimpleColumn:
                    {
                        /// Non-empty column non advanced string column
                        uint8_t* next_column = static_cast<uint8_t*>(rawmemchr(previous_ptr, ','));
                        int p = next_column - input_buffer;
                        int n = next_newline - input_buffer;
                        if(__builtin_expect(next_column == COMMA_SENTINEL, 0))
                        {
                            /// End of the chunk read 
                            
                            /// Write data to column
                            size_t copy_size = std::distance(previous_ptr, input_buffer + BUFFER_SIZE);
                            add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                            
                            /// Continue reading the simple column on chunk_read
                            current_state = InSimpleColumn;
                            goto read_chunk;
                        }
                        else if(__builtin_expect(next_column == next_newline, 0))
                        {
                            /// End of a row
                            
                            /// Output till next_ptr
                            size_t copy_size = std::distance(previous_ptr, next_column);
                            add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                            add_chars_to_column(column_infos[current_column++], '\n', 1);
                            
                            /// Set state to row initial 
                            previous_ptr = next_column + 1;
                            current_state = OnRowInitial;
                            goto state_begin;
                        }
                        else
                        {
                            /// End of a column
                            
                            /// Output till next_ptr
                            size_t copy_size = std::distance(previous_ptr, next_column);
                            add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                            add_chars_to_column(column_infos[current_column++], '\n', 1);
                            
                            /// Set state to column initial 
                            previous_ptr = next_column + 1;
                            current_state = OnColumnInitial;
                            goto state_begin;
                        }
                        break;
                    }
                    case InQuotedStringColumnOnQuote:
                        assert(false);
                        if(*previous_ptr == '"')
                        {
                            /// Two quotes in a row - we actually just add a '"' to the column and proceed to InQuotedStringColumn
                            current_state = InQuotedStringColumn;
                            
                            add_chars_to_column(column_infos[current_column], '"', 1);
                            previous_ptr++;
                            
                            goto state_begin;
                            
                        }
                        else if(*previous_ptr == ',')
                        {
                            /// A finishing quote was found at the end of the prior chunk - end the column and proceed to OnColumnInitial
                            current_state = OnColumnInitial;
                            
                            add_chars_to_column(column_infos[current_column++], '\n', 1);
                            previous_ptr++;
                            
                            goto state_begin;
                        }
                        else
                        {
                            /// No idea
                            assert(false);
                        }
                        break;
                    case InQuotedStringColumn:
                    {
                        /// Advanced string column
                        /// NOTE: Handle newline in advanced string by checking if the end of the read is after the newline
                        /// Represents the last read start, the last written position could be before this.
                        uint8_t* last_read = previous_ptr;
                        
                        while(true)
                        {
                            /// The next ptr is where our read chunk ends - typically a hopefully ending double quote
                            uint8_t* next_ptr = static_cast<uint8_t*>(rawmemchr(last_read, '"'));
                            
                            if(next_ptr > next_newline)
                            {
                                /// Check if we scanned past the newline sentinel - otherwise we revert it
                                /// Revert the newline sentinel back to a newline even if it is the NEWLINE_SENTINEL
                                *next_newline = '\n';
                                /// Find the next newline position
                                if(next_ptr < input_buffer + BUFFER_SIZE)
                                {
                                    /// Next ptr is still somewhere in the buffer - recompute the next newline position
                                    /// If it wasn't then the chunk reload would hit the NEWLINE sentinel
                                    next_newline = static_cast<uint8_t*>(rawmemchr(next_ptr, '\n'));
                                    if(next_newline != NEWLINE_SENTINEL)
                                        *next_newline = ',';
                                }
                            }
                    
                            if(__builtin_expect(next_ptr == DQUOTE_SENTINEL, 0))
                            {
                                /// We hit the end of the input block before we hit the end of the string
                                /// Alter the state to reflect the ending state and save to the output buffer
                                
                                /// Write data to column
                                size_t copy_size = std::distance(previous_ptr, input_buffer + BUFFER_SIZE);
                                add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                                
                                /// Trigger a chunk reload and continue from being in a quoted string
                                current_state = InQuotedStringColumn;
                                goto read_chunk;
                            }
                            else if(__builtin_expect(next_ptr == input_buffer + BUFFER_SIZE - 1, 0))
                            {
                                /// The double quote occurs on the buffer boundary
                                /// That means we restart the search on with the double quoted string in mind
                                
                                /// Write data to column
                                size_t copy_size = std::distance(previous_ptr, input_buffer + BUFFER_SIZE);
                                add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                                
                                /// Trigger a chunk reload and continue from the quoted string with prior quote char seen
                                current_state = InQuotedStringColumnOnQuote;
                                goto read_chunk;
                            }
                            else
                            {
                                /// This is a bonafide double quote, if a double quote follows it it is not an end
                                if(*(next_ptr + 1) == '"')
                                {
                                    /// An escape sequence - we are still in a quoted string
                                    last_read = next_ptr + 2;
                                    
                                    /// Trigger another iteration of the quote loop
                                    continue;
                                }
                                else if(*(next_ptr + 1) == ',')
                                {
                                    /// An end of column - maybe a newline.
                                    
                                    /// Output till next_ptr                                    
                                    size_t copy_size = std::distance(previous_ptr, next_ptr + 1);
                                    add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                                    /// Update the column with a new line
                                    add_chars_to_column(column_infos[current_column++], '\n', 1);

                                    previous_ptr = next_ptr + 2;
                                    
                                    if(next_ptr + 1 == next_newline)
                                    {
                                        /// This is an end of line 
                                        next_newline = static_cast<uint8_t*>(rawmemchr(next_newline + 1, '\n'));
                                        if(next_newline != NEWLINE_SENTINEL)
                                            *next_newline = ',';
                                        
                                        /// Go to the OnRowInitial state
                                        current_state = OnRowInitial;
                                        goto state_begin;
                                    }
                                    else
                                    {
                                        /// Go to the OnColumnInitial state
                                        current_state = OnColumnInitial;
                                        goto state_begin;
                                    }
                                }
                                else
                                {
                                    /// No idea what that this is, but we treat as a non-quoted continuation of the string
                                    /// This protects against trailing \r's
                                    
                                    /// Output till next_ptr                                    
                                    size_t copy_size = std::distance(previous_ptr, next_ptr + 1);
                                    add_buffer_to_column(column_infos[current_column], previous_ptr, copy_size);
                                    previous_ptr = next_ptr + 1;
                                    
                                    /// Drop down to InSimpleColumn state
                                    current_state = InSimpleColumn;
                                    goto state_begin;
                                }
                            }
                        }
                    }
                        break;
                }
            }
            
            /// This is not really reachable
        }
    }
}


