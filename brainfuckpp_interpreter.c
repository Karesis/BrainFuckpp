#include <stdio.h>
#include <stdlib.h> // For malloc, free, exit, realloc
#include <string.h> // For strlen, strchr, memmove
#include <ctype.h>  // For isspace
#include <stdint.h> // For SIZE_MAX

// --- Constants ---
// Maximum nesting depth for temporary pointers ({})
#define MAX_TEMP_DEPTH 256
// Maximum nesting depth for brackets ([])
#define MAX_BRACKET_DEPTH 1024
// Maximum code size
#define MAX_CODE_SIZE 65536
// Initial size of the tape
#define INITIAL_TAPE_SIZE 30000
// Comment character
#define COMMENT_CHAR '#'
#define MAX_NESTING_DEPTH 1024 // Unified max depth for [] and {}

// Enum for bracket/brace types
typedef enum {
    TYPE_BRACKET, // []
    TYPE_BRACE    // {}
    // Add future types here
} PairType;

// --- Struct Definitions (Define before typedef Interpreter) ---

// Structure for the memory tape
struct Tape {
    unsigned char* cells;
    size_t size;
    size_t zero_offset;
};

// Structure to hold pointer state
struct Pointer {
    int index;
};

// Structure for undo log entries
struct UndoEntry {
    int logical_index;
    unsigned char original_value;
    size_t stack_level;
};

// --- Interpreter Typedef (Define AFTER dependent structs) ---

// Interpreter state structure
typedef struct Interpreter {
    char* code;
    size_t code_length;
    int* bracket_map;
    int* brace_map;

    struct Tape tape; // Use struct keyword as Tape is not globally typedef'd

    struct Pointer pointer_stack[MAX_TEMP_DEPTH]; // Use struct keyword
    size_t pointer_stack_top;

    struct UndoEntry* undo_log;
    size_t undo_log_count;
    size_t undo_log_capacity;

    FILE* input;
    FILE* output;
} Interpreter;

// --- Unified Stack Entry for build_maps ---
typedef struct {
    size_t position; // Position (index) in the code
    PairType type;   // Type of the opening symbol ([ or {)
} MapStackEntry;

// --- Function Forward Declarations (Define AFTER typedef Interpreter) ---

void log_undo_entry(Interpreter* interp, int logical_index);
void restore_undo_entries(Interpreter* interp);
// Tape functions still need struct Tape*
int ensure_tape_capacity(struct Tape* tape, int index);
void free_tape(struct Tape* tape);
int init_tape(struct Tape* tape, size_t initial_size);
// Other functions use Interpreter*
char* filter_code(const char* input);
int build_maps(Interpreter* interp);
Interpreter* create_interpreter(const char* code, FILE* input, FILE* output);
void free_interpreter(Interpreter* interp);
int run(Interpreter* interp);
int is_command_char(char c);


// --- Function Implementations ---

// Helper function to check if a character is a valid BrainFuck++ command
int is_command_char(char c) {
    return (strchr("+-<>.,[]{}", c) != NULL);
}

/**
 * @brief Initializes the memory tape.
 * @param tape Pointer to the Tape structure.
 * @param initial_size The initial size to allocate.
 * @return 0 on success, -1 on failure.
 */
int init_tape(struct Tape* tape, size_t initial_size) {
    if (!tape) return -1;
    tape->cells = (unsigned char*)calloc(initial_size, sizeof(unsigned char));
    if (!tape->cells) {
        fprintf(stderr, "Error: Failed to allocate memory for tape.\n");
        tape->size = 0;
        tape->zero_offset = 0;
        return -1;
    }
    tape->size = initial_size;
    tape->zero_offset = initial_size / 2;
    return 0;
}

/**
 * @brief Frees the memory allocated for the tape.
 * @param tape Pointer to the Tape structure.
 */
void free_tape(struct Tape* tape) {
    if (tape && tape->cells) {
        free(tape->cells);
        tape->cells = NULL;
        tape->size = 0;
        tape->zero_offset = 0;
    }
}

/**
 * @brief Ensures the tape has enough capacity to access the given logical index.
 * Expands the tape if necessary using realloc. Handles both positive and negative index growth.
 * @param tape Pointer to the Tape structure.
 * @param index The logical index to ensure access to (can be negative relative to initial zero).
 * @return 0 on success, -1 on memory allocation failure.
 */
int ensure_tape_capacity(struct Tape* tape, int index) {
    size_t required_physical_index;
    size_t new_size;

    // Calculate the physical index required based on the current zero offset
    // Handle potential overflow/underflow if index is very large/small, though unlikely with int
    // For safety, let's check if adding the offset might wrap around size_t bounds.
    // We assume size_t is larger than int here.
    if (index >= 0) {
        // Check for positive overflow before adding
        if (tape->zero_offset > SIZE_MAX - (size_t)index) {
            fprintf(stderr, "Error: Required physical index calculation overflowed (positive).\n");
            return -1;
        }
        required_physical_index = tape->zero_offset + (size_t)index;
    } else {
        // Index is negative. Calculate absolute value as size_t.
        size_t abs_index = (size_t)(-index);
        // Check if zero_offset is large enough for subtraction
        if (tape->zero_offset < abs_index) {
             // This means we definitely need to shift left (expand left)
             required_physical_index = 0; // Placeholder, will be handled by expansion
        } else {
            required_physical_index = tape->zero_offset - abs_index;
        }
    }


    // Check if the required physical index is within the current bounds
    // Note: required_physical_index is size_t, so it cannot be < 0.
    if (required_physical_index >= tape->size) {
        // Needs expansion
        size_t old_size = tape->size;
        new_size = old_size == 0 ? INITIAL_TAPE_SIZE : old_size;

        // Determine how much shift (left expansion) is needed
        size_t shift_amount = 0;
        if (index < 0 && tape->zero_offset < (size_t)(-index)) {
             shift_amount = (size_t)(-index) - tape->zero_offset + (new_size / 2); // Heuristic shift
        }

        // Calculate new size, ensuring it covers the required index and potential shift
        while (required_physical_index >= new_size - shift_amount || new_size < old_size + shift_amount) {
             if (new_size > SIZE_MAX / 2) { // Prevent overflow when doubling
                 fprintf(stderr, "Error: Cannot expand tape further, maximum size reached.\n");
                 return -1;
             }
            new_size *= 2;
             // Recalculate shift based on new potential size
             if (index < 0 && tape->zero_offset < (size_t)(-index)) {
                 shift_amount = (size_t)(-index) - tape->zero_offset + (new_size / 2); // Re-evaluate shift
             }
        }


        // Reallocate tape
        unsigned char* new_tape = (unsigned char*)realloc(tape->cells, new_size * sizeof(unsigned char));
        if (!new_tape) {
            fprintf(stderr, "Error: Failed to reallocate memory for tape expansion.\n");
            return -1;
        }
        // Update tape->cells *before* using it in memmove/memset
        tape->cells = new_tape;

        // If we grew to the left (shifted)
        if (shift_amount > 0 && shift_amount < new_size && old_size < new_size ) {
            size_t move_size = old_size * sizeof(unsigned char);
            size_t destination_offset = shift_amount;
            // Ensure move doesn't exceed bounds
            if (destination_offset + move_size > new_size) {
                fprintf(stderr, "Error: Internal error during tape expansion memmove calculation.\n");
                // Since realloc succeeded, maybe try to recover?
                // But it indicates a logic flaw, better to return error.
                return -1;
            }
            // Source address is now tape->cells (new_tape), Target is tape->cells + destination_offset
            // We need to move the *old* content which was at the beginning of the *old* block.
            // Since realloc might move the block, we can't reliably use tape->cells as the source *before* the move.
            // Let's rethink: We need to move the logical content.
            // The content was logically from -zero_offset to old_size - zero_offset - 1.
            // After shifting zero_offset by shift_amount, the content needs to be physically
            // from (new_zero_offset - old_zero_offset) to (new_zero_offset - old_zero_offset + old_size -1).
            // This is getting complicated. Let's stick to the simpler physical move,
            // assuming memmove handles overlap correctly after realloc.

            // Content to be moved is at the *beginning* of the realloc'd block before memmove.
            memmove(tape->cells + destination_offset, tape->cells, move_size);
            // Zero out the new left part [0, destination_offset - 1]
            memset(tape->cells, 0, destination_offset * sizeof(unsigned char));
            tape->zero_offset += shift_amount; // Update zero offset
            // Zero out the potentially new right part as well
            size_t right_zero_start = destination_offset + move_size;
            if (right_zero_start < new_size) {
                memset(tape->cells + right_zero_start, 0, (new_size - right_zero_start) * sizeof(unsigned char));
            }
        } else if (new_size > old_size) {
            // Only grew to the right (or initial allocation)
             memset(tape->cells + old_size, 0, (new_size - old_size) * sizeof(unsigned char));
        }

        tape->size = new_size;

         // Recalculate required_physical_index after potential shifts and resize
         if (index >= 0) {
             required_physical_index = tape->zero_offset + (size_t)index;
         } else {
             size_t abs_index = (size_t)(-index);
             if (tape->zero_offset < abs_index) { // Should not happen after expansion
                 fprintf(stderr, "Error: Internal error after tape expansion (negative index).\n");
                 return -1;
             }
             required_physical_index = tape->zero_offset - abs_index;
         }

        // Final check after expansion
        if (required_physical_index >= tape->size) {
             fprintf(stderr, "Error: Tape expansion logic failed (final check).\n");
             return -1;
        }
    }
    return 0;
}

/**
 * @brief Filters the input BrainFuck code, removing comments and non-command characters.
 * Also shrinks the allocated memory for the code string to fit exactly.
 * @param input The raw source code string.
 * @return A newly allocated string containing only valid BrainFuck++ commands, or NULL on error.
 * The caller is responsible for freeing this memory.
 */
char* filter_code(const char* input) {
    size_t input_len = strlen(input);
    char* filtered = (char*)malloc(input_len + 1); // Allocate maximum possible size initially
    if (!filtered) {
        fprintf(stderr, "Memory allocation failed in filter_code\n");
        return NULL; // Return NULL on allocation failure
    }

    size_t j = 0; // Index for the filtered code
    int in_comment = 0;

    for (size_t i = 0; i < input_len; i++) {
        if (in_comment) {
            if (input[i] == '\n') {
                in_comment = 0; // End of line comment
            }
            continue; // Skip characters inside comments
        }

        if (input[i] == COMMENT_CHAR) {
            in_comment = 1;
            continue;
        }

        if (!isspace((unsigned char)input[i]) && is_command_char(input[i])) {
            filtered[j++] = input[i];
        }
    }
    filtered[j] = '\0'; // Null-terminate the filtered string

    // --- Optimization: Shrink the allocated memory ---
    char* final_code = realloc(filtered, j + 1);
    if (final_code == NULL) {
        // Realloc failed, but filtered still holds the data
        fprintf(stderr, "Warning: Could not reallocate filtered code buffer to smaller size.\n");
        // Keep the original larger buffer in this case
    } else {
        filtered = final_code; // Use the potentially smaller buffer
    }
    // --- End Optimization ---

    return filtered;
}

/**
 * @brief Creates and initializes the interpreter state.
 * @param code The raw BrainFuck++ source code string.
 * @param input The input stream (e.g., stdin).
 * @param output The output stream (e.g., stdout).
 * @return A pointer to the newly created Interpreter structure, or NULL on failure.
 */
Interpreter* create_interpreter(const char* code, FILE* input, FILE* output) {
    Interpreter* interp = (Interpreter*)malloc(sizeof(Interpreter));
    if (!interp) {
        fprintf(stderr, "Error: Failed to allocate memory for interpreter.\n");
        return NULL;
    }

    // Filter the code first
    interp->code = filter_code(code);
    if (!interp->code) {
        free(interp);
        return NULL;
    }
    interp->code_length = strlen(interp->code);

    if (interp->code_length > MAX_CODE_SIZE) {
        fprintf(stderr, "Error: Code exceeds maximum allowed size (%d).\n", MAX_CODE_SIZE);
        free(interp->code);
        free(interp);
        return NULL;
    }

    // Allocate memory for bracket and brace maps
    interp->bracket_map = (int*)malloc(sizeof(int) * interp->code_length);
    interp->brace_map = (int*)malloc(sizeof(int) * interp->code_length);
    if (!interp->bracket_map || !interp->brace_map) {
        fprintf(stderr, "Error: Failed to allocate memory for jump maps.\n");
        free(interp->code);
        free(interp);
        return NULL;
    }

    // Initialize maps to -1 (no corresponding bracket/brace)
    for (size_t i = 0; i < interp->code_length; i++) {
        interp->bracket_map[i] = -1;
        interp->brace_map[i] = -1;
    }

    // Initialize the memory tape
    if (init_tape(&interp->tape, INITIAL_TAPE_SIZE) != 0) {
        free(interp->bracket_map);
        free(interp->brace_map);
        free(interp->code);
        free(interp);
        return NULL;
    }

    // Initialize pointer stack (top = 0 represents the root pointer in run())
    interp->pointer_stack_top = 0; // Will be initialized in run(), set to 0 as base

    // Initialize Undo Log
    interp->undo_log_capacity = 16; // Initial capacity
    interp->undo_log_count = 0;
    interp->undo_log = (struct UndoEntry*)malloc(interp->undo_log_capacity * sizeof(struct UndoEntry));
    if (!interp->undo_log) {
         fprintf(stderr, "Error: Failed to allocate memory for undo log.\n");
         free_tape(&interp->tape);
         free(interp->bracket_map);
         free(interp->brace_map);
         free(interp->code);
         free(interp);
         return NULL;
    }

    // Set I/O streams
    interp->input = input;
    interp->output = output;

    // Build the jump maps
    if (build_maps(interp) != 0) {
        // Error message printed inside build_maps
        free_tape(&interp->tape);
        free(interp->undo_log);
        free(interp->bracket_map);
        free(interp->brace_map);
        free(interp->code);
        free(interp);
        return NULL;
    }

    return interp;
}

/**
 * @brief Frees all resources associated with the interpreter.
 * @param interp The interpreter instance to free.
 */
void free_interpreter(Interpreter* interp) {
    if (!interp) return;
    free(interp->code);
    free(interp->bracket_map);
    free(interp->brace_map);
    free(interp->undo_log); // Free the undo log
    free_tape(&interp->tape);
    // No need to free individual pointers in pointer_stack as it's now an array of structs
    free(interp);
}

/**
 * @brief Pre-calculates the matching bracket/brace positions, enforcing correct nesting.
 * @param interp The interpreter instance containing the code and maps.
 * @return 0 on success, -1 on mismatch or stack overflow.
 */
int build_maps(Interpreter* interp) {
    MapStackEntry map_stack[MAX_NESTING_DEPTH];
    int stack_top = -1; // Index of the top element in map_stack

    for (size_t i = 0; i < interp->code_length; i++) {
        char c = interp->code[i];
        size_t open_pos;

        switch (c) {
            case '[':
                if (++stack_top >= MAX_NESTING_DEPTH) {
                    fprintf(stderr, "Error: Nesting depth exceeded at position %zu.\n", i);
                    return -1;
                }
                map_stack[stack_top].position = i;
                map_stack[stack_top].type = TYPE_BRACKET;
                break;

            case '{':
                if (++stack_top >= MAX_NESTING_DEPTH) {
                    fprintf(stderr, "Error: Nesting depth exceeded at position %zu.\n", i);
                    return -1;
                }
                map_stack[stack_top].position = i;
                map_stack[stack_top].type = TYPE_BRACE;
                break;

            case ']':
                if (stack_top < 0) {
                    fprintf(stderr, "Error: Unmatched ']' at position %zu.\n", i);
                    return -1;
                }
                if (map_stack[stack_top].type != TYPE_BRACKET) {
                    fprintf(stderr, "Error: Mismatched pair - found ']' but expected '}' at position %zu (matching open at %zu).\n", i, map_stack[stack_top].position);
                    return -1;
                }
                open_pos = map_stack[stack_top].position;
                interp->bracket_map[i] = open_pos;
                interp->bracket_map[open_pos] = i;
                stack_top--;
                break;

            case '}':
                if (stack_top < 0) {
                    fprintf(stderr, "Error: Unmatched '}' at position %zu.\n", i);
                    return -1;
                }
                if (map_stack[stack_top].type != TYPE_BRACE) {
                     fprintf(stderr, "Error: Mismatched pair - found '}' but expected ']' at position %zu (matching open at %zu).\n", i, map_stack[stack_top].position);
                    return -1;
                }
                open_pos = map_stack[stack_top].position;
                // Store in brace_map (though run() doesn't use it directly for jumps yet)
                interp->brace_map[i] = open_pos;
                interp->brace_map[open_pos] = i;
                stack_top--;
                break;
        }
    }

    // Check for unmatched opening brackets/braces
    if (stack_top != -1) {
        MapStackEntry unmatched = map_stack[stack_top];
        fprintf(stderr, "Error: Unmatched opening '%c' at position %zu.\n",
                (unmatched.type == TYPE_BRACKET ? '[' : '{'),
                unmatched.position);
        return -1;
    }

    return 0; // Success
}

/**
 * @brief Executes the BrainFuck++ code.
 * @param interp The interpreter instance containing the code, tape, and state.
 * @return 0 on successful completion, -1 on runtime error or timeout.
 */
int run(Interpreter* interp) {
    size_t ip = 0;
    struct Pointer* current_ptr;

    interp->pointer_stack_top = 0;
    interp->pointer_stack[0].index = 0;
    current_ptr = &interp->pointer_stack[0];

    const size_t MAX_INSTRUCTIONS = 100000000;
    size_t instruction_count = 0;
    int debug_enabled = 1; // Set to 1 to enable debug prints

    // Simple way to enable debug for specific test files if needed
    // if (argc > 1 && strstr(argv[1], "temp_pointer.bf")) debug_enabled = 1;

    while (ip < interp->code_length && instruction_count < MAX_INSTRUCTIONS) {
        char command = interp->code[ip];
        instruction_count++;

        if (debug_enabled) fprintf(stderr, "[IP:%zu Cmd:'%c' StackLvl:%zu LogicIdx:%d]", ip, command, interp->pointer_stack_top, current_ptr->index);

        switch (command) {
            case '>': {
                int old_idx = current_ptr->index;
                int next_logical_index = old_idx + 1;
                if (ensure_tape_capacity(&interp->tape, next_logical_index) != 0) {
                    fprintf(stderr, " Error: Failed tape capacity for '>'\n"); return -1;
                }
                current_ptr->index = next_logical_index;
                 if (debug_enabled) fprintf(stderr, " -> New LogicIdx:%d\n", current_ptr->index);
                break;
            }
            case '<': {
                int old_idx = current_ptr->index;
                int next_logical_index = old_idx - 1;
                 if (ensure_tape_capacity(&interp->tape, next_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for '<'\n"); return -1;
                }
                 current_ptr->index = next_logical_index;
                 if (debug_enabled) fprintf(stderr, " -> New LogicIdx:%d\n", current_ptr->index);
                break;
            }
            case '+': {
                int current_logical_index = current_ptr->index;
                if (ensure_tape_capacity(&interp->tape, current_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for '+'\n"); return -1;
                }
                size_t current_physical_index = interp->tape.zero_offset + current_logical_index;
                unsigned char old_val = interp->tape.cells[current_physical_index];
                log_undo_entry(interp, current_logical_index);
                interp->tape.cells[current_physical_index]++;
                 if (debug_enabled) fprintf(stderr, " (PhysIdx:%zu) Val:%d -> %d\n", current_physical_index, old_val, interp->tape.cells[current_physical_index]);
                break;
            }
            case '-': {
                int current_logical_index = current_ptr->index;
                if (ensure_tape_capacity(&interp->tape, current_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for '-'\n"); return -1;
                }
                size_t current_physical_index = interp->tape.zero_offset + current_logical_index;
                 unsigned char old_val = interp->tape.cells[current_physical_index];
                log_undo_entry(interp, current_logical_index);
                interp->tape.cells[current_physical_index]--;
                 if (debug_enabled) fprintf(stderr, " (PhysIdx:%zu) Val:%d -> %d\n", current_physical_index, old_val, interp->tape.cells[current_physical_index]);
                break;
            }
            case '.': {
                int current_logical_index = current_ptr->index;
                if (ensure_tape_capacity(&interp->tape, current_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for '.'\n"); return -1;
                }
                size_t current_physical_index = interp->tape.zero_offset + current_logical_index;
                unsigned char val_to_output = interp->tape.cells[current_physical_index];
                 if (debug_enabled) fprintf(stderr, " (PhysIdx:%zu ZeroOff:%zu) Outputting Val:%d ('%c')\n", current_physical_index, interp->tape.zero_offset, val_to_output, isprint(val_to_output)?val_to_output:'?');
                if (fputc(val_to_output, interp->output) == EOF) {
                    perror("Error writing output");
                    return -1;
                }
                break;
            }
            case ',': {
                int current_logical_index = current_ptr->index;
                if (ensure_tape_capacity(&interp->tape, current_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for ','\n"); return -1;
                }
                size_t current_physical_index = interp->tape.zero_offset + current_logical_index;
                if (debug_enabled) fprintf(stderr, " Waiting for input...");
                int input_char = fgetc(interp->input);
                unsigned char old_val = interp->tape.cells[current_physical_index];
                unsigned char new_val = (input_char == EOF) ? 0 : (unsigned char)input_char;
                log_undo_entry(interp, current_logical_index);
                interp->tape.cells[current_physical_index] = new_val;
                 if (debug_enabled) fprintf(stderr, " Read %d. (PhysIdx:%zu) Val:%d -> %d\n", input_char, current_physical_index, old_val, new_val);
                break;
            }
            case '[': {
                int current_logical_index = current_ptr->index;
                if (ensure_tape_capacity(&interp->tape, current_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for '['\n"); return -1;
                }
                size_t current_physical_index = interp->tape.zero_offset + current_logical_index;
                if (debug_enabled) fprintf(stderr, " (Test Val:%d at PhysIdx:%zu)", interp->tape.cells[current_physical_index], current_physical_index);
                if (interp->tape.cells[current_physical_index] == 0) {
                    int jump_target = interp->bracket_map[ip];
                    if (jump_target == -1) {
                        fprintf(stderr, " Error: Invalid jump target for '['\n"); return -1;
                    }
                    if (debug_enabled) fprintf(stderr, " -> Jumping to %d\n", jump_target);
                    ip = (size_t)jump_target;
                } else {
                     if (debug_enabled) fprintf(stderr, " -> Entering loop\n");
                }
                break;
            }
            case ']': {
                int current_logical_index = current_ptr->index;
                if (ensure_tape_capacity(&interp->tape, current_logical_index) != 0) {
                     fprintf(stderr, " Error: Failed tape capacity for ']'\n"); return -1;
                }
                size_t current_physical_index = interp->tape.zero_offset + current_logical_index;
                 if (debug_enabled) fprintf(stderr, " (Test Val:%d at PhysIdx:%zu)", interp->tape.cells[current_physical_index], current_physical_index);
                if (interp->tape.cells[current_physical_index] != 0) {
                    int jump_target = interp->bracket_map[ip];
                     if (jump_target == -1) {
                         fprintf(stderr, " Error: Invalid jump target for ']'\n"); return -1;
                    }
                     if (debug_enabled) fprintf(stderr, " -> Jumping back to %d\n", jump_target);
                     ip = (size_t)jump_target;
                } else {
                    if (debug_enabled) fprintf(stderr, " -> Exiting loop\n");
                }
                break;
            }
            case '{': {
                int current_logical_index = current_ptr->index;
                if (interp->pointer_stack_top + 1 >= MAX_TEMP_DEPTH) {
                     fprintf(stderr, " Error: Pointer stack overflow\n"); return -1;
                }
                 if (debug_enabled) fprintf(stderr, " -> Pushing scope. Old top:%zu", interp->pointer_stack_top);
                interp->pointer_stack_top++;
                interp->pointer_stack[interp->pointer_stack_top].index = current_logical_index;
                current_ptr = &interp->pointer_stack[interp->pointer_stack_top];
                 if (debug_enabled) fprintf(stderr, ", New top:%zu, Copied LogicIdx:%d\n", interp->pointer_stack_top, current_ptr->index);
                break;
            }
            case '}': {
                if (interp->pointer_stack_top == 0) {
                     fprintf(stderr, " Error: Pointer stack underflow\n"); return -1;
                }
                 if (debug_enabled) fprintf(stderr, " -> Popping scope. Restoring level %zu...", interp->pointer_stack_top);
                restore_undo_entries(interp);
                size_t old_top = interp->pointer_stack_top;
                interp->pointer_stack_top--;
                current_ptr = &interp->pointer_stack[interp->pointer_stack_top];
                 if (debug_enabled) fprintf(stderr, " Done. New top:%zu, Restored LogicIdx:%d\n", interp->pointer_stack_top, current_ptr->index);
                break;
            }
            default: // Ignore non-command characters (already filtered, but safe)
                 if (debug_enabled) fprintf(stderr, " -> Ignoring char '%c'\n", command);
                 break;
        }
        ip++;
    }

    if (debug_enabled) fprintf(stderr, "[Execution End] Reason: %s, Instructions: %zu\n", (ip >= interp->code_length ? "End of code" : "Instruction limit"), instruction_count);

    if (instruction_count >= MAX_INSTRUCTIONS) {
        fprintf(stderr, "Warning: Maximum instruction limit (%zu) reached.\n", MAX_INSTRUCTIONS);
        return -1;
    }

    while (interp->pointer_stack_top > 0) {
        if (debug_enabled) fprintf(stderr, "[Cleanup] Restoring scope level %zu\n", interp->pointer_stack_top);
        restore_undo_entries(interp);
        interp->pointer_stack_top--;
    }

    return 0;
}

// Add debug prints to undo functions as well
void log_undo_entry(Interpreter* interp, int logical_index) {
    int debug_enabled = 1; // Match run function
    // if (argc > 1 && strstr(argv[1], "temp_pointer.bf")) debug_enabled = 1;

    if (interp->pointer_stack_top == 0) return;

    for (size_t i = interp->undo_log_count; i > 0; --i) {
        if (interp->undo_log[i - 1].logical_index == logical_index &&
            interp->undo_log[i - 1].stack_level >= interp->pointer_stack_top) {
             if (debug_enabled) fprintf(stderr, "    [Undo Log] Skip logging idx %d at lvl %zu (already logged at lvl %zu)\n", logical_index, interp->pointer_stack_top, interp->undo_log[i - 1].stack_level);
            return;
        }
    }

    if (interp->undo_log_count >= interp->undo_log_capacity) {
        size_t new_capacity = interp->undo_log_capacity == 0 ? 16 : interp->undo_log_capacity * 2;
        if (new_capacity > MAX_CODE_SIZE * 4) { // Increased heuristic limit for log
             fprintf(stderr, "Error: Undo log capacity limit reached.\n"); return;
        }
        struct UndoEntry* new_log = (struct UndoEntry*)realloc(interp->undo_log, new_capacity * sizeof(struct UndoEntry));
        if (!new_log) {
            fprintf(stderr, "Error: Failed to reallocate undo log.\n"); return;
        }
        interp->undo_log = new_log;
        interp->undo_log_capacity = new_capacity;
    }

    if (ensure_tape_capacity(&interp->tape, logical_index) != 0) {
        fprintf(stderr, "Error: Failed tape capacity in log_undo_entry\n"); return;
    }
    size_t physical_index = interp->tape.zero_offset + logical_index;
    unsigned char original_value = interp->tape.cells[physical_index];

    if (debug_enabled) fprintf(stderr, "    [Undo Log] Logging: Idx:%d Val:%d Lvl:%zu\n", logical_index, original_value, interp->pointer_stack_top);

    interp->undo_log[interp->undo_log_count].logical_index = logical_index;
    interp->undo_log[interp->undo_log_count].original_value = original_value;
    interp->undo_log[interp->undo_log_count].stack_level = interp->pointer_stack_top;
    interp->undo_log_count++;
}

void restore_undo_entries(Interpreter* interp) {
     int debug_enabled = 1; // Match run function
     // if (argc > 1 && strstr(argv[1], "temp_pointer.bf")) debug_enabled = 1;
    size_t restore_count = 0;
    size_t target_level = interp->pointer_stack_top;

    while (interp->undo_log_count > 0 &&
           interp->undo_log[interp->undo_log_count - 1].stack_level == target_level) {

        interp->undo_log_count--;
        struct UndoEntry entry = interp->undo_log[interp->undo_log_count];

        if (ensure_tape_capacity(&interp->tape, entry.logical_index) != 0) {
            fprintf(stderr, "Error: Failed tape capacity in restore_undo_entries\n");
             interp->undo_log_count++; // Revert decrement
             break;
        }

        size_t physical_index = interp->tape.zero_offset + entry.logical_index;
         if (debug_enabled) fprintf(stderr, "    [Undo Restore] Restoring Idx:%d to Val:%d (was %d) for Lvl:%zu\n", entry.logical_index, entry.original_value, interp->tape.cells[physical_index], target_level);
        interp->tape.cells[physical_index] = entry.original_value;
        restore_count++;
    }
     if (debug_enabled && restore_count > 0) fprintf(stderr, "    [Undo Restore] Restored %zu entries for level %zu\n", restore_count, target_level);
}

/**
 * @brief Main entry point for the BrainFuck++ interpreter.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename.bfpp> [input_file] [output_file]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // --- File Handling ---
    FILE* code_file = fopen(argv[1], "r");
    if (!code_file) {
        perror("Error opening code file");
        return EXIT_FAILURE;
    }

    // Determine file size
    fseek(code_file, 0, SEEK_END);
    long file_size = ftell(code_file);
    if (file_size < 0) {
        perror("Error getting file size");
        fclose(code_file);
        return EXIT_FAILURE;
    }
    fseek(code_file, 0, SEEK_SET);

    // Check against a reasonable maximum file size before reading
    if (file_size > MAX_CODE_SIZE * 2) { // Allow for comments/whitespace
        fprintf(stderr, "Error: Code file size (%ld bytes) is excessively large.\n", file_size);
        fclose(code_file);
        return EXIT_FAILURE;
    }

    // Allocate memory and read the code
    char* code_buffer = (char*)malloc(file_size + 1);
    if (!code_buffer) {
        fprintf(stderr, "Error: Failed to allocate memory for code buffer.\n");
        fclose(code_file);
        return EXIT_FAILURE;
    }

    size_t bytes_read = fread(code_buffer, 1, file_size, code_file);
    if (bytes_read != (size_t)file_size && ferror(code_file)) {
        perror("Error reading code file");
        free(code_buffer);
        fclose(code_file);
        return EXIT_FAILURE;
    }
    code_buffer[bytes_read] = '\0'; // Null-terminate the string
    fclose(code_file);

    // --- Input/Output Stream Handling ---
    FILE* input_stream = stdin;
    FILE* output_stream = stdout;
    int input_opened = 0;
    int output_opened = 0;

    if (argc >= 3) {
        input_stream = fopen(argv[2], "r");
        if (!input_stream) {
            perror("Error opening input file");
            free(code_buffer);
            return EXIT_FAILURE;
        }
        input_opened = 1;
    }

    if (argc >= 4) {
        output_stream = fopen(argv[3], "w");
        if (!output_stream) {
            perror("Error opening output file");
            free(code_buffer);
            if (input_opened) fclose(input_stream);
            return EXIT_FAILURE;
        }
        output_opened = 1;
    }

    // --- Interpreter Execution ---
    Interpreter* interp = create_interpreter(code_buffer, input_stream, output_stream);
    int run_status = -1; // Default to error status

    if (interp) {
        run_status = run(interp);
        // --- Optimization: Flush output buffer after execution ---
        fflush(output_stream);
        // --- End Optimization ---
        free_interpreter(interp);
    }

    // --- Cleanup ---
    free(code_buffer);
    if (input_opened) fclose(input_stream);
    if (output_opened) fclose(output_stream);

    // Return appropriate exit code based on execution status
    return (run_status == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}