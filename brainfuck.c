#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For potential future use with getopt, though not strictly needed for current manual parsing
#include <getopt.h> // For standard argument parsing (using manual parsing here)

// Define max code size and pointer stack depth (adjust if needed)
#define MAX_CODE_SIZE 1000000
#define MAX_POINTER_DEPTH 100 // Max nesting depth for '*' blocks
#define MAX_BRACKET_STAR_DEPTH 500 // Max nesting depth for [] and **

// --- Debug Flag for build_maps ---
// Set to 1 to enable detailed build_maps debugging, 0 to disable
#define DEBUG_BUILD_MAPS 0 // DISABLED NOW

// --- Data Structures ---

// Structure for a memory cell using a doubly linked list
typedef struct Node {
    unsigned char data; // Use unsigned char for 8-bit wrapping behavior
    struct Node* next; // Pointer to the next cell
    struct Node* prev; // Pointer to the previous cell
} Node;

// Structure for the BrainFuck pointer, pointing to the current memory cell
typedef struct Pointer {
    Node* current; // Pointer to the current active node/cell
} Pointer;

// Structure to hold jump maps for loop optimization and '*' pairing
typedef struct Maps {
    size_t bracket_map[MAX_CODE_SIZE]; // Maps '[' to ']' and vice versa
    size_t star_map[MAX_CODE_SIZE];    // Maps starting '*' to ending '*' and vice versa
    size_t code_len;                  // Length of the code
} Maps;

// Structure to hold the execution state, passed around to helper functions
typedef struct ExecutionState {
    Pointer* main_pointer;              // The main pointer owning the memory tape
    Pointer* active_pointer;            // The currently active pointer for operations
    Pointer* pointer_stack[MAX_POINTER_DEPTH]; // Stack for suspended pointers (*)
    int pointer_stack_top;              // Top index for pointer_stack (-1 means empty)
    size_t ip;                          // Instruction pointer for the main code string
    const char* code;                   // The source code string
    Maps* maps;                         // Pointer to pre-calculated jump/pairing maps
    int error_flag;                     // Flag to indicate runtime errors
    int debug_mode;                     // Flag to indicate if debug mode is enabled
    long relative_pos;                  // Tracks active pointer relative position from start
    int in_comment_line;                // Flag: 1 if currently inside a # comment line, 0 otherwise
} ExecutionState;


// --- Memory Cell (Node) Functions ---

// Creates a new memory node (cell) initialized to zero
Node* create_node() {
    Node* node = (Node*)malloc(sizeof(Node));
    if (node == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for node.\n");
        // In a real application, might want more robust error handling than exit
        exit(EXIT_FAILURE);
    }
    node->data = 0;
    node->next = NULL;
    node->prev = NULL;
    return node;
}

// Finds the head of the linked list given any node in it
Node* find_head(Node* current_node) {
    if (current_node == NULL) return NULL;
    while (current_node->prev != NULL) {
        current_node = current_node->prev;
    }
    return current_node;
}

// Frees the entire linked list starting from the head.
// Should only be called once at the end for the main memory tape.
void cleanup_memory(Node* any_node) {
    Node* current = find_head(any_node); // Find the actual start of the list
    while (current != NULL) {
        Node* temp = current;
        current = current->next;
        free(temp);
    }
}

// --- Pointer Functions ---

// Initializes the pointer structure fields. Memory node must be assigned separately.
void pointer_init(Pointer* pointer) {
    pointer->current = NULL;
}

// Creates just the Pointer struct itself (allocates memory for the struct).
// The 'current' node needs to be assigned later.
Pointer* create_pointer_struct() {
    Pointer* pointer = (Pointer*)malloc(sizeof(Pointer));
    if (pointer == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for pointer struct.\n");
        exit(EXIT_FAILURE);
    }
    pointer_init(pointer); // Initialize fields
    return pointer;
}

// Destroys the main pointer structure AND cleans up the associated memory list.
// Should only be called on the *main* pointer at the very end of execution.
void destroy_main_pointer_and_memory(Pointer* pointer) {
    if (pointer == NULL) return;
    cleanup_memory(pointer->current); // Clean up the shared linked list memory
    free(pointer);                   // Free the main pointer struct itself
}


// --- BrainFuck Command Implementations (operate on the active pointer) ---

// '+' command: Increment the data in the current cell
void bf_add(Pointer* pointer) {
    if (pointer == NULL || pointer->current == NULL) return;
    pointer->current->data++;
    // Optional: Add modulo 256 arithmetic if strict byte behavior is desired
    // pointer->current->data &= 0xFF; // Or use % 256 carefully with negatives
}

// '-' command: Decrement the data in the current cell
void bf_sub(Pointer* pointer) {
    if (pointer == NULL || pointer->current == NULL) return;
    pointer->current->data--;
    // Optional: Add modulo 256 arithmetic
    // pointer->current->data &= 0xFF;
}

// '>' command: Move the pointer to the right cell, creating one if necessary
// Now updates the relative position in the ExecutionState.
void bf_move_right(ExecutionState* state) {
    if (state == NULL || state->active_pointer == NULL || state->active_pointer->current == NULL) return;
    Pointer* pointer = state->active_pointer;
    if (pointer->current->next == NULL) {
        // Create a new node if moving past the current end
        pointer->current->next = create_node();
        if (!pointer->current->next) return; // Allocation failed
        pointer->current->next->prev = pointer->current;
    }
    pointer->current = pointer->current->next;
    state->relative_pos++; // Increment relative position
}

// '<' command: Move the pointer to the left cell, creating one if necessary
// Now updates the relative position in the ExecutionState.
void bf_move_left(ExecutionState* state) {
    if (state == NULL || state->active_pointer == NULL || state->active_pointer->current == NULL) return;
    Pointer* pointer = state->active_pointer;
    if (pointer->current->prev == NULL) {
        // Create a new node if moving past the current beginning
        pointer->current->prev = create_node();
         if (!pointer->current->prev) return; // Allocation failed
        pointer->current->prev->next = pointer->current;
    }
    pointer->current = pointer->current->prev;
    state->relative_pos--; // Decrement relative position
}

// '.' command: Print the ASCII value of the data in the current cell
void bf_print(Pointer* pointer) {
    if (pointer == NULL || pointer->current == NULL) return;
    // Standard C behavior for putchar depends on the int value.
    // Often, only the lower 8 bits are used for ASCII.
    putchar(pointer->current->data);
}

// ',' command: Read one character of input into the current cell
void bf_input(Pointer* pointer) {
    if (pointer == NULL || pointer->current == NULL) return;
    int ch = getchar();
    if (ch == EOF) {
        pointer->current->data = 0; // Store 0 on EOF (common convention)
    } else {
        // Store the byte value.
        pointer->current->data = (unsigned char)ch;
    }
}


// --- Debugging Helper ---

// Prints debug information for the current step to file and selectively to terminal
void print_debug_step(FILE* log_file, const ExecutionState* state, char current_cmd) {
    if (!state || !state->debug_mode) return; // Only run if debug mode is on

    // Prepare basic info string
    char info[256];
    snprintf(info, sizeof(info),
             "IP: %-5zu | Cmd: '%c' | Pos: %-5ld | Val: %-3d (0x%02X) | Ptr: %p | Stk: %d",
             state->ip,
             current_cmd ? current_cmd : ' ', // Show space if cmd is null/invalid
             state->relative_pos,
             state->active_pointer && state->active_pointer->current ? state->active_pointer->current->data : -1, // Show -1 if pointer/node invalid
             state->active_pointer && state->active_pointer->current ? state->active_pointer->current->data : 0xFF,
             (void*)(state->active_pointer ? state->active_pointer->current : NULL), // Pointer address
             state->pointer_stack_top + 1 // Stack depth (0 if empty)
            );

    // Always write full info to log file
    if (log_file) {
        fprintf(log_file, "%s\n", info);
        // Flush occasionally to ensure logs are written even if it crashes
        // Warning: Frequent flushing can impact performance.
        // Consider flushing only on critical steps or using setvbuf.
        if (state->ip % 100 == 0) { // Flush every 100 steps
             fflush(log_file);
        }
    }

    // Selectively print to terminal for critical/interesting commands
    int print_to_terminal = 0;
    switch (current_cmd) {
        case '[':
        case ']':
        case '*':
        case '!':
        case '.':
        case ',':
            print_to_terminal = 1;
            break;
        // Add other conditions if needed, e.g., print every N steps:
        // default: if (state->ip % 1000 == 0) print_to_terminal = 1; break;
    }

    if (print_to_terminal) {
        printf("DEBUG: %s\n", info);
    }
}

// --- Pre-processing (Jump Maps for [] and **) ---

// Builds maps for both '[]' and '**' pairs.
// Returns 0 on success, -1 on mismatch or other errors.
int build_maps(const char* code, size_t code_len, Maps* maps) {
#if DEBUG_BUILD_MAPS
    fprintf(stderr, "[DEBUG build_maps] Entering function. code_len = %zu\n", code_len);
    // Optionally print the first few chars of code: 
    // fprintf(stderr, "[DEBUG build_maps] Code starts with: %.*s\n", (int)(code_len > 10 ? 10 : code_len), code);
#endif

    if (code_len >= MAX_CODE_SIZE) {
         fprintf(stderr, "Error: Code length (%zu) exceeds maximum allowed size (%d).\n", code_len, MAX_CODE_SIZE);
         return -1;
    }
    maps->code_len = code_len;
    // Initialize maps to 0 (or another indicator of no mapping)
    memset(maps->bracket_map, 0, sizeof(maps->bracket_map));
    memset(maps->star_map, 0, sizeof(maps->star_map));

    // Using two stacks with fixed maximum depth.
    size_t* bracket_stack = (size_t*)calloc(MAX_BRACKET_STAR_DEPTH, sizeof(size_t));
    size_t* star_stack = (size_t*)calloc(MAX_BRACKET_STAR_DEPTH, sizeof(size_t));
    if (bracket_stack == NULL || star_stack == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for map building stacks.\n");
        free(bracket_stack); 
        free(star_stack);
        return -1;
    }
    int bracket_stack_ptr = -1; 
    int star_stack_ptr = -1;

#if DEBUG_BUILD_MAPS
    fprintf(stderr, "[DEBUG build_maps] Initialized stacks. bracket_ptr=%d, star_ptr=%d\n", bracket_stack_ptr, star_stack_ptr);
#endif

    for (size_t i = 0; i < code_len; ++i) {
#if DEBUG_BUILD_MAPS > 1 // More verbose logging inside loop (set DEBUG_BUILD_MAPS to 2)
        fprintf(stderr, "[DEBUG build_maps] Loop i=%zu, char='%c' (%d), bracket_ptr=%d, star_ptr=%d\n", i, code[i] >= 32 ? code[i] : '?', code[i], bracket_stack_ptr, star_stack_ptr);
#endif
        switch(code[i]) {
            case '[':
                // Check against fixed stack depth
                if (bracket_stack_ptr + 1 >= MAX_BRACKET_STAR_DEPTH) { 
                     fprintf(stderr, "Error: Bracket stack overflow during parsing (max depth %d reached).\n", MAX_BRACKET_STAR_DEPTH); goto map_error;
                }
                bracket_stack[++bracket_stack_ptr] = i; 
#if DEBUG_BUILD_MAPS
                fprintf(stderr, "[DEBUG build_maps] Pushed '[' at index %zu. New bracket_ptr=%d\n", i, bracket_stack_ptr);
#endif
                break;
            case ']':
                if (bracket_stack_ptr < 0) {
                    fprintf(stderr, "Error: Unmatched closing bracket ']' at position %zu.\n", i); goto map_error;
                }
                // Read value first, then explicitly decrement pointer
                size_t open_bracket_index = bracket_stack[bracket_stack_ptr];
                bracket_stack_ptr--; 
                maps->bracket_map[open_bracket_index] = i;
                maps->bracket_map[i] = open_bracket_index;
#if DEBUG_BUILD_MAPS
                fprintf(stderr, "[DEBUG build_maps] Matched ']' at index %zu with '[' at %zu. New bracket_ptr=%d\n", i, open_bracket_index, bracket_stack_ptr);
#endif
                break;
            case '*':
                if (star_stack_ptr < 0) {
                    // Check against fixed stack depth
                     if (star_stack_ptr + 1 >= MAX_BRACKET_STAR_DEPTH) {
                        fprintf(stderr, "Error: Star stack overflow during parsing (max depth %d reached).\n", MAX_BRACKET_STAR_DEPTH); goto map_error;
                    }
                    star_stack[++star_stack_ptr] = i; 
#if DEBUG_BUILD_MAPS
                    fprintf(stderr, "[DEBUG build_maps] Pushed starting '*' at index %zu. New star_ptr=%d\n", i, star_stack_ptr);
#endif
                } else {
                    // Read value first, then explicitly decrement pointer
                    size_t start_star_index = star_stack[star_stack_ptr]; 
                    star_stack_ptr--;
                    maps->star_map[start_star_index] = i;       
                    maps->star_map[i] = start_star_index;       
#if DEBUG_BUILD_MAPS
                    fprintf(stderr, "[DEBUG build_maps] Matched ending '*' at index %zu with start '*' at %zu. New star_ptr=%d\n", i, start_star_index, star_stack_ptr);
#endif
                }
                break;
        }
    }

#if DEBUG_BUILD_MAPS
    fprintf(stderr, "[DEBUG build_maps] Finished loop. Final bracket_ptr=%d, star_ptr=%d\n", bracket_stack_ptr, star_stack_ptr);
#endif

    // After checking all code, stacks should be empty if brackets/stars are matched
    if (bracket_stack_ptr != -1) {
#if DEBUG_BUILD_MAPS
        // Log the value that will be printed in the error message
        fprintf(stderr, "[DEBUG build_maps] Unmatched '['. bracket_stack_ptr=%d. Value at stack top: %zu\n", bracket_stack_ptr, bracket_stack[bracket_stack_ptr]); 
#endif
        fprintf(stderr, "Error: Unmatched opening bracket '[' at position %zu.\n", bracket_stack[bracket_stack_ptr]); goto map_error;
    }
    if (star_stack_ptr != -1) {
#if DEBUG_BUILD_MAPS
        // Log the value that will be printed in the error message
        fprintf(stderr, "[DEBUG build_maps] Unmatched starting '*'. star_stack_ptr=%d. Value at stack top: %zu\n", star_stack_ptr, star_stack[star_stack_ptr]);
#endif
        fprintf(stderr, "Error: Unmatched starting '*' at position %zu.\n", star_stack[star_stack_ptr]); goto map_error;
    }

    // Cleanup stacks and return success
#if DEBUG_BUILD_MAPS
    fprintf(stderr, "[DEBUG build_maps] Stacks were matched. Returning success.\n");
#endif
    free(bracket_stack);
    free(star_stack);
    return 0;

map_error:
#if DEBUG_BUILD_MAPS
    fprintf(stderr, "[DEBUG build_maps] Error occurred. Cleaning up stacks and returning failure.\n");
#endif
    free(bracket_stack);
    free(star_stack);
    return -1; // Failure
}


// --- Command Execution Logic Helpers ---

// Handles the logic for the '*' command (entering/exiting context)
// Takes the current execution state and modifies it directly.
void handle_star_command(ExecutionState* state) {
    // Basic validation
    if (!state || !state->maps || !state->active_pointer) {
        fprintf(stderr, "Internal Error: Invalid state passed to handle_star_command.\n");
        state->error_flag = 1; return;
    }

    size_t current_ip = state->ip;
    // Check if a valid pairing exists in the map. build_maps should ensure this if it succeeded.
    size_t match_ip = state->maps->star_map[current_ip];
    if (match_ip == 0 && state->code[current_ip] == '*') {
        // This indicates an internal inconsistency or map building failure not caught earlier.
        fprintf(stderr, "Internal Error: Invalid star map entry for '*' at ip %zu.\n", current_ip);
        state->error_flag = 1; return;
    }

    if (match_ip > current_ip) {
        // --- Entering '*' block ---
        if (state->pointer_stack_top + 1 >= MAX_POINTER_DEPTH) {
            fprintf(stderr, "Error: Pointer stack overflow at ip %zu (max depth %d reached).\n", current_ip, MAX_POINTER_DEPTH);
            state->error_flag = 1; return;
        }
        // Create a new temporary pointer struct (doesn't allocate new Node memory)
        Pointer* temp_pointer = create_pointer_struct();
        if (temp_pointer == NULL) { state->error_flag = 1; return; /* Error already printed */ }

        // Point temp pointer to the same memory node as the currently active one
        // Ensure active pointer and its node are valid before dereferencing
        if (!state->active_pointer->current) {
             fprintf(stderr, "Internal Error: Active pointer's node is NULL when entering '*' at ip %zu.\n", current_ip);
             free(temp_pointer); // Clean up allocated struct
             state->error_flag = 1; return;
        }
        temp_pointer->current = state->active_pointer->current;

        // Push the *current* active pointer onto the stack
        state->pointer_stack[++(state->pointer_stack_top)] = state->active_pointer;

        // Make the temporary pointer the active one
        state->active_pointer = temp_pointer;

    } else {
        // --- Exiting '*' block ---
        if (state->pointer_stack_top < 0) {
            // This should be caught by build_maps, but double-check for runtime consistency
            fprintf(stderr, "Error: Pointer stack underflow at ip %zu (mismatched '*').\n", current_ip);
            state->error_flag = 1; return;
        }
        // Store pointer to be destroyed (the current temporary one)
        Pointer* pointer_to_destroy = state->active_pointer;

        // Pop the previous pointer from the stack to make it active
        state->active_pointer = state->pointer_stack[state->pointer_stack_top--];

        // Free the temporary Pointer struct *only*.
        // Important: Check it's not the main pointer before freeing, although logic dictates it shouldn't be.
        if (pointer_to_destroy && pointer_to_destroy != state->main_pointer) {
             free(pointer_to_destroy);
        } else if (pointer_to_destroy == state->main_pointer) {
             // This indicates a serious logic error if the main pointer was on the stack
             fprintf(stderr, "Critical Warning: Attempted to free main pointer during '*' exit at ip %zu.\n", current_ip);
             // Avoid freeing main pointer here, it's handled at the end.
        }
    }
}

// Handles the logic for the '[' command at the current IP
void handle_open_bracket(ExecutionState* state) {
     // Basic validation
     if (!state || !state->maps || !state->active_pointer || !state->active_pointer->current) {
         fprintf(stderr, "Internal Error: Invalid state passed to handle_open_bracket.\n");
         state->error_flag = 1; return;
     }
     // Check the data cell value
     if (state->active_pointer->current->data == 0) {
         // Jump IP past matching ']' using the main code's bracket map
         size_t target_ip = state->maps->bracket_map[state->ip];
         // Safety check the map result before jumping
         if (target_ip == 0 && state->code[state->ip] == '[') {
             // Map likely invalid or points to beginning, indicates error
             fprintf(stderr, "Error: Invalid bracket jump map for '[' at ip %zu.\n", state->ip);
             state->error_flag = 1;
             state->ip = state->maps->code_len; // Force stop
             return;
         }
         state->ip = target_ip; // Perform the jump (IP will be incremented after switch)
     }
     // If data is non-zero, IP simply increments naturally in the main loop
}

// Handles the logic for the ']' command at the current IP
void handle_close_bracket(ExecutionState* state) {
     // Basic validation
     if (!state || !state->maps || !state->active_pointer || !state->active_pointer->current) {
          fprintf(stderr, "Internal Error: Invalid state passed to handle_close_bracket.\n");
          state->error_flag = 1; return;
     }
     // Check the data cell value
     if (state->active_pointer->current->data != 0) {
         // Jump IP back to instruction after matching '[' using the main code's bracket map
         size_t target_ip = state->maps->bracket_map[state->ip];
          // Safety check the map result before jumping
         if (target_ip == 0 && state->code[state->ip] == ']') {
             // Map likely invalid or points to beginning, indicates error
             fprintf(stderr, "Error: Invalid bracket jump map for ']' at ip %zu.\n", state->ip);
             state->error_flag = 1;
             state->ip = state->maps->code_len; // Force stop
             return;
         }
         // The map gives the index of '[', we want to land *after* it for the next iteration
         state->ip = target_ip; // Perform the jump (IP will be incremented after switch)
     }
     // If data is zero, IP simply increments naturally in the main loop
}

// Handles the logic for the '!' command (Single-Command Interpreter)
void handle_interpret_command(ExecutionState* state) {
    // Basic validation
    if (!state || !state->active_pointer || !state->active_pointer->current) {
        fprintf(stderr, "Internal Error: Invalid state passed to handle_interpret_command.\n");
        state->error_flag = 1; return;
    }

    // Get the command code (ASCII value) from the current cell
    int command_code = state->active_pointer->current->data;

    // Execute the command corresponding to the ASCII code
    // Note: For '[', ']', '*' this executes their logic *as if* they were
    // present at the current '!' location in the main code stream.
    // !!! TEMPORARY FIX: Disable flow control execution via '!' to prevent loops !!!
    switch (command_code) {
        case '+': bf_add(state->active_pointer); break;
        case '-': bf_sub(state->active_pointer); break;
        case '>': bf_move_right(state); break;
        case '<': bf_move_left(state); break;
        case '.': bf_print(state->active_pointer); break;
        case ',': bf_input(state->active_pointer); break;
        // case '[':
        //     // Execute '[' logic using main IP and main bracket map relative to '!' pos
        //     handle_open_bracket(state);
        //     break;
        // case ']':
        //     // Execute ']' logic using main IP and main bracket map relative to '!' pos
        //     handle_close_bracket(state);
        //     break;
        // case '*':
        //     // Execute '*' logic using main IP and main star map relative to '!' pos
        //     handle_star_command(state);
        //     break;
        // Add cases for other potential future commands if needed
        default:
            // Value in cell is not a recognized command, or it's disabled flow control.
            // Ignore.
            // Optionally, add a warning for disabled commands:
            if (command_code == '[' || command_code == ']' || command_code == '*') {
                 fprintf(stderr, "Warning: Execution of flow control command %c (%d) via '!' is currently disabled.\n", command_code, command_code);
            }
            break;
    }
    // Main IP always advances past '!' in the main loop after this function returns
}


// --- BrainFuck++ Execution Engine ---
void execute_code(const char* code, size_t actual_code_len, int debug_mode) {
    // Allocate Maps on the heap to avoid stack overflow
    Maps* maps = (Maps*)malloc(sizeof(Maps));
    if (maps == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for Maps structure.\n");
        return;
    }

    // Open debug log file if debug mode is enabled
    FILE* log_file = NULL;
    if (debug_mode) {
        log_file = fopen("debug_log.txt", "w");
        if (log_file == NULL) {
            fprintf(stderr, "Warning: Could not open debug_log.txt for writing.\n");
            // Continue execution without logging to file
        } else {
             fprintf(log_file, "--- BrainFuck++ Debug Log ---\\n");
             fprintf(log_file, "Code Length: %zu\\n", actual_code_len);
             fprintf(log_file, "---------------------------\\n");
             // Use line buffering for potentially better performance than no buffering
             // but still ensures lines are written relatively quickly.
             setvbuf(log_file, NULL, _IOLBF, 0);
        }
    }

#if DEBUG_BUILD_MAPS // This block is inactive now
    fprintf(stderr, "[DEBUG execute_code] Before strlen. Code pointer: %p. First 20 chars: [%.*s]\n", 
            (void*)code, (int)strnlen(code, 20), code); // Print start of string safely
#endif

    // Build the jump/pairing maps first using the passed actual_code_len.
    if (build_maps(code, actual_code_len, maps) != 0) {
        fprintf(stderr, "Error during pre-processing: Halting execution.\n");
        // Cleanup maps if allocated before returning on error
        free(maps);
        if (log_file) fclose(log_file); // Close log file on error too
        return;
    }

    // Initialize main memory tape (start with one cell) and pointers
    Pointer* main_pointer = create_pointer_struct();
    if (!main_pointer) return; // Allocation failed
    main_pointer->current = create_node();
    if (!main_pointer->current) { free(main_pointer); return; } // Allocation failed

    // Initialize execution state
    ExecutionState state;
    state.main_pointer = main_pointer;
    state.active_pointer = main_pointer; // Start with main pointer active
    state.pointer_stack_top = -1;        // Pointer stack is initially empty
    state.ip = 0;                        // Start at the beginning of the code
    state.code = code;
    state.maps = maps; // Maps struct now contains the correct code_len
    state.maps->code_len = actual_code_len; // Ensure maps struct has the correct length too
    state.error_flag = 0;                // Initialize error flag
    state.debug_mode = debug_mode;       // Set debug mode in state
    state.relative_pos = 0;              // Initialize relative position
    state.in_comment_line = 0;           // Initialize comment flag

    // Main execution loop - uses maps->code_len which is now correct
    while (state.ip < state.maps->code_len && !state.error_flag) {
        // Get current command
        char current_cmd = state.code[state.ip];
        int skip_char = 0; // Flag: 1 if we should skip executing this char

        // 1. Handle comment state
        if (state.in_comment_line) {
            if (current_cmd == '\n') {
                state.in_comment_line = 0; // End of comment line
            }
            skip_char = 1; // Skip character inside comment
        } else if (current_cmd == '#') {
            state.in_comment_line = 1; // Start of comment line
            skip_char = 1; // Skip the '#' itself
        }

        // 2. If not skipped by comment logic, check if it's a valid command
        const char* valid_commands = "+-><.,[]*!";
        if (!skip_char && strchr(valid_commands, current_cmd) == NULL) {
            // It's not a comment, not '#', but also not a valid command
            // -> Treat as whitespace or other ignored char
            skip_char = 1;
        }

        // 3. Execute or Skip based on the flag
        if (skip_char) {
            state.ip++; // Skip the character
            // continue; // Continue is implicit here
        } else {
            // --- It IS a valid command, proceed with debugging and execution ---
            print_debug_step(log_file, &state, current_cmd);

            // Check for critical state validity before executing command
            if (!state.active_pointer || !state.active_pointer->current) {
                fprintf(stderr, "Critical Error: Active pointer or node became NULL before executing ip %zu.\n", state.ip);
                state.error_flag = 1; // Set error flag to stop loop
                break; // Exit while loop immediately
            }

            // Execute command based on the character in the code string
            switch (current_cmd) {
                // Standard commands call basic functions
                case '+': bf_add(state.active_pointer); break;
                case '-': bf_sub(state.active_pointer); break;
                case '>': bf_move_right(&state); break;
                case '<': bf_move_left(&state); break;
                case '.': bf_print(state.active_pointer); break;
                case ',': bf_input(state.active_pointer); break;
                // Control flow and extended commands call helper functions
                case '[': handle_open_bracket(&state); break;
                case ']': handle_close_bracket(&state); break;
                case '*': handle_star_command(&state); break;
                case '!': handle_interpret_command(&state); break;
                // Default should not be reachable due to the strchr check above,
                // but included for safety/completeness.
                default:
                     fprintf(stderr, "Internal Warning: Reached default case in switch for cmd '%c' at ip %zu. Should have been skipped.\n", current_cmd, state.ip);
                     break;
            }

            // Move to the next instruction in the main code, AFTER execution,
            // unless an error occurred. This handles jumps correctly, as handle_*_bracket
            // modifies state.ip directly before this increment.
            if (!state.error_flag) {
                state.ip++;
            }
            // If an error occurred, the loop condition will handle termination.
        }
    } // End while loop

    // --- Final Cleanup ---
    // Flush and close the debug log file if it was opened
    if (log_file) {
        fprintf(log_file, "--- End of Execution ---\\n");
        fflush(log_file); // Ensure everything is written
        fclose(log_file);
    }

    // Check for runtime errors indicated by the flag
    if (state.error_flag) {
         fprintf(stderr, "Execution halted due to runtime error.\n");
    }

    // Check pointer stack consistency
    if (state.pointer_stack_top != -1) {
         fprintf(stderr, "Warning: Execution finished, but pointer stack was not empty (level %d). Mismatched '*' likely.\n", state.pointer_stack_top + 1);
         // Attempt to free any remaining temporary pointer structs on the stack
         while (state.pointer_stack_top >= 0) {
             Pointer* ptr_on_stack = state.pointer_stack[state.pointer_stack_top--];
             if (ptr_on_stack != state.main_pointer) {
                 free(ptr_on_stack);
             }
         }
    }
    // Check if active pointer is a dangling temporary pointer
     if (state.active_pointer != state.main_pointer) {
         fprintf(stderr, "Warning: Execution finished, but active pointer was not the main pointer. Freeing temporary struct.\n");
         // Only free the struct, memory nodes are handled by main_pointer cleanup
         free(state.active_pointer);
         state.active_pointer = NULL; // Avoid double free if main_pointer cleanup runs after this
     }

    // Destroy the main pointer structure AND the underlying memory tape (Nodes)
    // Ensure main_pointer itself is valid before calling destroy
    if (state.main_pointer) {
        destroy_main_pointer_and_memory(state.main_pointer);
    }

    // Free the allocated Maps structure
    free(maps);
}


// --- Main Function and Argument Parsing ---
void print_help(const char* prog_name) {
    // Updated help message including '!' and '-d'
    printf("Usage: %s [options] [file]\n", prog_name);
    printf("Options:\n");
    printf("  -e \"<code>\"  Execute BrainFuck++ code directly from the command line.\n");
    printf("  -d, --debug  Enable debug mode (logs steps to debug_log.txt).\n");
    printf("  -h, --help   Display this help message.\n");
    printf("  [file]       Execute BrainFuck++ code from the specified file.\n");
    printf("\nBrainFuck++ Commands:\n");
    printf("  >  Move active pointer right\n");
    printf("  <  Move active pointer left\n");
    printf("  +  Increment cell value at active pointer\n");
    printf("  -  Decrement cell value at active pointer\n");
    printf("  .  Output cell value (ASCII) at active pointer\n");
    printf("  ,  Input character into cell at active pointer (0 on EOF)\n");
    printf("  [  Start loop (based on active pointer's cell value)\n");
    printf("  ]  End loop (based on active pointer's cell value)\n");
    printf("  * Start/End temporary pointer context (must be paired)\n");
    printf("  !  Interpret cell value (ASCII) as command and execute it once\n");
}

#ifndef TESTING
// Main function: handles argument parsing and file reading
int main(int argc, char* argv[]) {
    char* code_to_execute = NULL;
    char* filename = NULL;
    int run_from_string = 0;
    int debug_mode = 0; // Flag for debug mode

    // --- Argument Parsing Logic (Manual) ---
    // This is a simplified manual parser. getopt_long would be more robust.
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            i++; // Move to next argument
        } else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 < argc) {
                code_to_execute = argv[i + 1];
                run_from_string = 1;
                i += 2; // Consumed -e and the code string
            } else {
                fprintf(stderr, "Error: -e option requires a code string argument.\n");
                print_help(argv[0]);
                return 1;
            }
        } else {
            // Assume it's a filename, but only if we haven't already got one
            if (filename == NULL && !run_from_string) {
                filename = argv[i];
                i++;
            } else if (run_from_string) {
                 fprintf(stderr, "Error: Cannot specify both -e and a filename.\n");
                 print_help(argv[0]);
                 return 1;
            } else {
                // Multiple filenames or unrecognized argument
                fprintf(stderr, "Error: Unexpected argument '%s' or multiple filenames.\n", argv[i]);
                print_help(argv[0]);
                return 1;
            }
        }
    }

    // Check if a code source (file or -e) was provided
    if (filename == NULL && code_to_execute == NULL) {
         fprintf(stderr, "Error: No input file or code string provided.\n");
         print_help(argv[0]);
         return 1;
    }


    // --- Load Code Logic ---
    char* code_buffer = NULL; // Buffer for code read from file
    size_t actual_code_len = 0; // Variable to store the actual length read

    if (!run_from_string && filename != NULL) {
#if DEBUG_BUILD_MAPS // This block will be inactive now
        fprintf(stderr, "[DEBUG main] Processing file: %s\n", filename);
#endif
        FILE* file = fopen(filename, "rb");
        if (file == NULL) {
            fprintf(stderr, "Error: Cannot open file: %s\n", filename);
            return 1;
        }

        // Determine file size for buffer allocation (might be inaccurate)
        fseek(file, 0, SEEK_END);
        long estimated_size = ftell(file); // Renamed from file_size
#if DEBUG_BUILD_MAPS // This block will be inactive now
        fprintf(stderr, "[DEBUG main] ftell estimated_size = %ld\n", estimated_size);
#endif
        if (estimated_size < 0) { // Error checking ftell result
             fprintf(stderr, "Error getting file size for: %s\n", filename);
             fclose(file);
             return 1;
        }
        if (estimated_size >= MAX_CODE_SIZE) {
             fprintf(stderr, "Error: File size estimate (%ld) exceeds maximum allowed code size (%d).\n", estimated_size, MAX_CODE_SIZE);
             fclose(file);
             return 1;
        }
        fseek(file, 0, SEEK_SET); // Rewind to beginning

        // Allocate memory based on estimate + null terminator
        code_buffer = (char*)malloc(estimated_size + 1); 
        if (code_buffer == NULL) {
            fprintf(stderr, "Error: Memory allocation failed for reading file content.\n");
            fclose(file);
            return 1;
        }

        // Read file content
        // Use estimated_size for the read size limit
        size_t bytes_read = fread(code_buffer, 1, estimated_size, file);
#if DEBUG_BUILD_MAPS // This block will be inactive now
        fprintf(stderr, "[DEBUG main] fread read bytes_read = %zu\n", bytes_read);
#endif
        // Check for read errors (ferror) or incomplete reads if expected == estimated_size
        if (ferror(file)) {
             fprintf(stderr, "Error reading file: %s.\n", filename);
             free(code_buffer);
             fclose(file);
             return 1;
        }
        // Note: feof(file) might be true here if read stopped exactly at EOF.
        
        // Use bytes_read as the definitive length
        actual_code_len = bytes_read;

        // Null-terminate the string AT THE ACTUAL END OF READ DATA
        code_buffer[actual_code_len] = '\0'; 
#if DEBUG_BUILD_MAPS // This block will be inactive now
        fprintf(stderr, "[DEBUG main] Null terminator placed at index %zu based on bytes_read. Value: %d\n", 
                actual_code_len, (int)code_buffer[actual_code_len]);
        if (actual_code_len > 0) {
             fprintf(stderr, "[DEBUG main] Char before null term (index %zu): '%c' (%d)\n", 
                     actual_code_len - 1, 
                     code_buffer[actual_code_len - 1] >= 32 ? code_buffer[actual_code_len - 1] : '?', 
                     (int)code_buffer[actual_code_len - 1]);
         }
#endif

        fclose(file);
        code_to_execute = code_buffer; // Point to the buffer read from file
    } else if (run_from_string) {
        // For code from string, length is determined by strlen
        code_to_execute = argv[2]; 
        actual_code_len = strlen(code_to_execute);
    } else {
         // Should not happen if argument parsing is correct
         fprintf(stderr, "Internal Error: No code source specified.\n");
         return 1;
    }

    // --- Execute the Code ---
    if (code_to_execute) {
        // Pass the actual code length determined above
        execute_code(code_to_execute, actual_code_len, debug_mode); 
    }

    // --- Cleanup ---
    // Free the code buffer only if it was allocated for reading a file
    if (code_buffer != NULL) {
        free(code_buffer);
    }

    return 0; // Indicate successful execution
}
#endif // TESTING