#include <stdlib.h>
#include <stdio.h>
#include <string.h> // For strlen, strchr, memset
#include <ctype.h>  // For isspace

// --- Constants ---
#define MAX_NESTING_DEPTH 1024 // Unified max depth for [] and ()
#define MAX_CODE_SIZE 65536    // Max filtered code size
#define MAX_POINTER_STACK_DEPTH 256 // Max nesting depth for ()
#define COMMENT_CHAR '#'

// Enum for paired symbol types
typedef enum {
    TYPE_BRACKET, // []
    TYPE_PAREN    // ()
} PairType;

// --- Struct Definitions ---

// Forward declare Node for Pointer struct
typedef struct Node Node;

struct Node {
    int data;  // 从unsigned char改为int，支持负数
    Node *next;
    Node *prev;
};

// Forward declare Pointer for Interpreter struct and function signatures
typedef struct Pointer Pointer;

// Pointer structure with function pointers (OOP-like approach)
struct Pointer {
    Node *current; // Pointer to the current Node in the linked list tape
    // Function pointers for basic operations
    int (*move_left)(Pointer *self);  // Returns 0 on success, -1 on failure
    int (*move_right)(Pointer *self); // Returns 0 on success, -1 on failure
    void (*set_value)(Pointer *self, int value);  // 改为使用int
    int (*get_value)(Pointer *self);  // 返回类型改为int
    void (*increment_value)(Pointer *self);
    void (*decrement_value)(Pointer *self);
    int (*move_relative)(Pointer *self, int offset); // 新增：相对移动函数
};

// --- Function Declarations (Prototypes) ---

// Node/Pointer operations (implementations below)
int move_left(Pointer *self);  // Changed return type
int move_right(Pointer *self); // Changed return type
void set_value(Pointer *self, int value);
int get_value(Pointer *self);
void increment_value(Pointer *self);
void decrement_value(Pointer *self);
int move_relative(Pointer *self, int offset); // 新增：相对移动函数
void init_pointer(Pointer *self);
Pointer* create_pointer();
void free_pointer_tape(Pointer *p); // Function to free the linked list
Pointer* create_temp_pointer(Pointer *self); // Creates a temp pointer pointing to the same node

// Interpreter state structure
typedef struct Interpreter {
    char* code;             // Filtered BrainFuck++ code
    size_t code_length;     // Length of the filtered code
    int* bracket_map;       // Maps '[' to ']' and vice versa
    int* paren_map;         // Maps '(' to ')' and vice versa

    Pointer* main_pointer;  // The primary data pointer operating on the tape

    Pointer* pointer_stack[MAX_POINTER_STACK_DEPTH]; // Stack for temporary pointers from ()
    int pointer_stack_top;   // Index of the current top (-1 for empty, 0 for main_pointer)

    FILE* input;            // Input stream
    FILE* output;           // Output stream
} Interpreter;

// Helper & Interpreter Lifecycle functions (implementations below)
int is_command_char(char c);
char* filter_code(const char* input);
int build_maps(Interpreter* interp);
Interpreter* create_interpreter(const char* code_str, FILE* input, FILE* output);
void free_interpreter(Interpreter* interp);
int run(Interpreter* interp);

// --- Function Implementations ---

// Helper: Check for valid command characters
int is_command_char(char c) {
    return (strchr("+-<>.,[]()/*", c) != NULL); // 添加 * 到命令字符列表
}

// -- Pointer Method Implementations --

int move_left(Pointer *self) {
    // Ensure current node exists (should always exist after initialization)
    if (!self->current) return -1; 
    if (self->current->prev == NULL) {
        Node* new_node = (Node*)malloc(sizeof(Node));
        if (new_node == NULL) {
            perror("Failed to allocate memory in move_left"); return -1;
        }
        new_node->data = 0;
        new_node->next = self->current;
        new_node->prev = NULL;
        self->current->prev = new_node;
    }
    self->current = self->current->prev;
    return 0;
}

int move_right(Pointer *self) {
    if (!self->current) return -1;
    if (self->current->next == NULL) {
        Node* new_node = (Node*)malloc(sizeof(Node));
        if (new_node == NULL) {
             perror("Failed to allocate memory in move_right"); return -1;
        }
        new_node->data = 0;
        new_node->prev = self->current;
        new_node->next = NULL;
        self->current->next = new_node;
    }
    self->current = self->current->next;
    return 0;
}

void set_value(Pointer *self, int value) {
    if (!self->current) return;
    self->current->data = value;
}

int get_value(Pointer *self) {
    if (!self->current) return 0; // Or handle error
    return self->current->data;
}

void increment_value(Pointer *self) {
    if (!self->current) return;
    self->current->data++;
}

void decrement_value(Pointer *self) {
    if (!self->current) return;
    self->current->data--;
}

// 新增：相对移动函数实现
int move_relative(Pointer *self, int offset) {
    if (!self->current) return -1;
    
    // 处理相对位置移动
    int i;
    if (offset > 0) {
        // 正向移动
        for (i = 0; i < offset; i++) {
            if (move_right(self) != 0) return -1;
        }
    } else if (offset < 0) {
        // 负向移动
        for (i = 0; i < -offset; i++) {
            if (move_left(self) != 0) return -1;
        }
    }
    // offset为0时不移动
    return 0;
}

// Initialize a Pointer struct
void init_pointer(Pointer *self) {
    // Start with a single node for the tape
    self->current = (Node*)malloc(sizeof(Node));
    if (!self->current) {
         perror("Failed to allocate initial tape node");
         return;
    }
    self->current->data = 0;
    self->current->prev = NULL;
    self->current->next = NULL;

    // Assign function pointers
    self->move_left = move_left;
    self->move_right = move_right;
    self->set_value = set_value;
    self->get_value = get_value;
    self->increment_value = increment_value;
    self->decrement_value = decrement_value;
    self->move_relative = move_relative; // 新增：设置相对移动函数指针
}

// Create and initialize a new Pointer (allocates memory for Pointer struct)
Pointer* create_pointer() {
    Pointer *pointer = (Pointer *)malloc(sizeof(Pointer));
    if (!pointer) {
         perror("Failed to allocate memory for Pointer struct");
         return NULL;
    }
    init_pointer(pointer); // This allocates the initial Node
    if (!pointer->current) { // Check if init_pointer failed
        free(pointer);
        return NULL;
    }
    return pointer;
}

// Free the linked list nodes starting from the current pointer
// Goes left to the beginning, then frees all nodes to the right
void free_pointer_tape(Pointer *p) {
    if (!p || !p->current) return;

    Node *node = p->current;
    // Go to the leftmost node
    while (node->prev) {
        node = node->prev;
    }
    // Free all nodes from left to right
    while (node) {
        Node *next_node = node->next;
        free(node);
        node = next_node;
    }
    // Important: Nullify the current pointer in the struct after freeing
    p->current = NULL;
}


// Creates a temporary pointer pointing to the same node as the parent
Pointer* create_temp_pointer(Pointer *self) {
    Pointer *temp_pointer = (Pointer *)malloc(sizeof(Pointer)); // Allocate struct for temp pointer
     if (!temp_pointer) {
         perror("Failed to allocate memory for temporary Pointer struct");
         return NULL;
    }
    // Copy parent's state - *NO* new tape nodes allocated here
    temp_pointer->current = self->current; // Point to the *same* Node
    // Copy function pointers
    temp_pointer->move_left = self->move_left;
    temp_pointer->move_right = self->move_right;
    temp_pointer->set_value = self->set_value;
    temp_pointer->get_value = self->get_value;
    temp_pointer->increment_value = self->increment_value;
    temp_pointer->decrement_value = self->decrement_value;
    temp_pointer->move_relative = self->move_relative; // 添加对相对移动函数指针的复制
    return temp_pointer;
}

// --- Interpreter Helper Functions ---

// Filters code, removes comments and non-commands
char* filter_code(const char* input) {
    size_t input_len = strlen(input);
    char* filtered = (char*)malloc(input_len + 1);
    if (!filtered) return NULL;

    size_t j = 0;
    int in_comment = 0;
    for (size_t i = 0; i < input_len; i++) {
        if (in_comment) {
            if (input[i] == '\n') in_comment = 0;
            continue;
        }
        if (input[i] == COMMENT_CHAR) {
            in_comment = 1;
            continue;
        }
        if (is_command_char(input[i])) {
            filtered[j++] = input[i];
        }
    }
    filtered[j] = '\0';

    // Shrink allocation (optional optimization)
    char* final_code = (char*)realloc(filtered, j + 1);
    return (final_code ? final_code : filtered);
}

// Unified stack entry for build_maps
typedef struct {
    size_t position;
    PairType type;
} MapStackEntry;

// Build jump maps for [] and ()
int build_maps(Interpreter* interp) {
    MapStackEntry map_stack[MAX_NESTING_DEPTH];
    int stack_top = -1;

    // Initialize maps
    interp->bracket_map = (int*)malloc(sizeof(int) * interp->code_length);
    interp->paren_map = (int*)malloc(sizeof(int) * interp->code_length);
    if (!interp->bracket_map || !interp->paren_map) {
        fprintf(stderr, "Error: Failed to allocate memory for jump maps.\n");
        return -1; // Indicate failure
    }
    memset(interp->bracket_map, -1, sizeof(int) * interp->code_length);
    memset(interp->paren_map, -1, sizeof(int) * interp->code_length);


    for (size_t i = 0; i < interp->code_length; i++) {
        char c = interp->code[i];
        size_t open_pos;

        switch (c) {
            case '[':
                if (++stack_top >= MAX_NESTING_DEPTH) { /* Error */ return -1; }
                map_stack[stack_top].position = i;
                map_stack[stack_top].type = TYPE_BRACKET;
                break;
            case '(':
                if (++stack_top >= MAX_NESTING_DEPTH) { /* Error */ return -1; }
                map_stack[stack_top].position = i;
                map_stack[stack_top].type = TYPE_PAREN;
                break;
            case ']':
                if (stack_top < 0 || map_stack[stack_top].type != TYPE_BRACKET) { /* Error */ return -1; }
                open_pos = map_stack[stack_top].position;
                interp->bracket_map[i] = open_pos;
                interp->bracket_map[open_pos] = i;
                stack_top--;
                break;
            case ')':
                if (stack_top < 0 || map_stack[stack_top].type != TYPE_PAREN) { /* Error */ return -1; }
                open_pos = map_stack[stack_top].position;
                interp->paren_map[i] = open_pos; // Store paren map too
                interp->paren_map[open_pos] = i;
                stack_top--;
                break;
        }
    }

    if (stack_top != -1) { /* Error: Unmatched open brackets/parens */ return -1; }
    return 0; // Success
}

// --- Interpreter Lifecycle ---

Interpreter* create_interpreter(const char* code_str, FILE* input, FILE* output) {
    Interpreter* interp = (Interpreter*)malloc(sizeof(Interpreter));
    if (!interp) { perror("Failed malloc for Interpreter"); return NULL; }

    interp->input = input ? input : stdin;
    interp->output = output ? output : stdout;

    // Filter code
    interp->code = filter_code(code_str);
    if (!interp->code) { free(interp); return NULL; }
    interp->code_length = strlen(interp->code);
    if (interp->code_length > MAX_CODE_SIZE) {
        fprintf(stderr, "Error: Code exceeds maximum size.\n");
        free(interp->code); free(interp); return NULL;
    }

    // Create main pointer (this also creates the initial tape node)
    interp->main_pointer = create_pointer();
    if (!interp->main_pointer) {
         free(interp->code); free(interp); return NULL;
    }

    // Initialize pointer stack (-1 means only main_pointer is active)
    interp->pointer_stack_top = -1;

    // Build maps
    interp->bracket_map = NULL; // Initialize map pointers
    interp->paren_map = NULL;
    if (build_maps(interp) != 0) {
        fprintf(stderr, "Error: Mismatched brackets or parentheses in code.\n");
        free_pointer_tape(interp->main_pointer); // Free tape nodes
        free(interp->main_pointer); // Free pointer struct
        free(interp->code);
        free(interp->bracket_map); // build_maps allocates them
        free(interp->paren_map);
        free(interp);
        return NULL;
    }

    return interp;
}

void free_interpreter(Interpreter* interp) {
    if (!interp) return;

    // Free the linked list tape via the main pointer
    free_pointer_tape(interp->main_pointer);
    // Free the main pointer struct itself
    free(interp->main_pointer);

    // Free any temporary pointers left on the stack (shouldn't happen with correct code)
    for(int i = 0; i <= interp->pointer_stack_top; ++i) {
        // Don't free the tape again, just the pointer struct
        free(interp->pointer_stack[i]);
    }

    // Free code buffer and maps
    free(interp->code);
    free(interp->bracket_map);
    free(interp->paren_map);

    // Free the interpreter struct itself
    free(interp);
}


// --- Main Execution Logic ---

int run(Interpreter* interp) {
    size_t ip = 0;
    Pointer* current_active_pointer = interp->main_pointer; // Use a clear name

    const size_t MAX_INSTRUCTIONS = 100000000;
    size_t instruction_count = 0;
    int debug_enabled = 0; // 禁用调试

    while (ip < interp->code_length && instruction_count < MAX_INSTRUCTIONS) {
        char command = interp->code[ip];
        instruction_count++;

        if (debug_enabled) {
            int cell_value = get_value(current_active_pointer);
            fprintf(stderr, "[指令:%zu 命令:'%c' 堆栈级别:%d 当前值:%d(%c)] ", 
                ip, command, interp->pointer_stack_top, 
                cell_value, isprint(cell_value) ? cell_value : '.');
        }

        switch (command) {
            case '>': {
                if (current_active_pointer->move_right(current_active_pointer) != 0) {
                    fprintf(stderr, "Runtime Error: move_right failed at ip %zu\n", ip);
                    return -1;
                }
                if (debug_enabled) fprintf(stderr, " -> NewVal: %d\n", get_value(current_active_pointer)); 
                break;
            }
            case '<': {
                if (current_active_pointer->move_left(current_active_pointer) != 0) {
                     fprintf(stderr, "Runtime Error: move_left failed at ip %zu\n", ip);
                    return -1;
                }
                 if (debug_enabled) fprintf(stderr, " -> NewVal: %d\n", get_value(current_active_pointer)); 
                break;
            }
            case '+': {
                int old_val = get_value(current_active_pointer);
                current_active_pointer->increment_value(current_active_pointer);
                if (debug_enabled) fprintf(stderr, " Val:%d -> %d\n", old_val, get_value(current_active_pointer));
                break;
            }
            case '-': {
                 int old_val = get_value(current_active_pointer);
                 current_active_pointer->decrement_value(current_active_pointer);
                 if (debug_enabled) fprintf(stderr, " Val:%d -> %d\n", old_val, get_value(current_active_pointer));
                 break;
            }
            case '.': {
                 int val_to_output = get_value(current_active_pointer);
                 if (debug_enabled) fprintf(stderr, " Outputting Val:%d ('%c')\n", val_to_output, isprint(val_to_output)?val_to_output:'?');
                 fputc(val_to_output, interp->output);
                 break;
            }
            case ',': {
                int input_char = fgetc(interp->input);
                int old_val = get_value(current_active_pointer);
                int new_val = (input_char == EOF) ? 0 : input_char;
                current_active_pointer->set_value(current_active_pointer, new_val);
                if (debug_enabled) fprintf(stderr, " Read %d. Val:%d -> %d\n", input_char, old_val, new_val);
                break;
            }
            case '[': {
                 int current_val = get_value(current_active_pointer);
                 if (debug_enabled) fprintf(stderr, " (Test Val:%d)", current_val);
                if (current_val == 0) {
                    if (interp->bracket_map[ip] == -1) { fprintf(stderr, " Error: Unmatched '['\n"); return -1;}
                     if (debug_enabled) fprintf(stderr, " -> Jumping to %d\n", interp->bracket_map[ip]);
                    ip = interp->bracket_map[ip]; // Jump past matching ]
                } else {
                     if (debug_enabled) fprintf(stderr, " -> Entering loop\n");
                }
                break;
            }
            case ']': {
                 int current_val = get_value(current_active_pointer);
                 if (debug_enabled) fprintf(stderr, " (Test Val:%d)", current_val);
                if (current_val != 0) {
                     if (interp->bracket_map[ip] == -1) { fprintf(stderr, " Error: Unmatched ']'\n"); return -1;}
                      if (debug_enabled) fprintf(stderr, " -> Jumping back to %d\n", interp->bracket_map[ip]);
                    ip = interp->bracket_map[ip]; // Jump back to matching [
                } else {
                     if (debug_enabled) fprintf(stderr, " -> Exiting loop\n");
                }
                break;
            }
            case '(': {
                if (interp->pointer_stack_top + 1 >= MAX_POINTER_STACK_DEPTH) {
                    fprintf(stderr, "错误: 临时指针堆栈溢出\n"); return -1;
                }
                
                // 将当前指针放入堆栈
                interp->pointer_stack_top++;
                interp->pointer_stack[interp->pointer_stack_top] = current_active_pointer;
                
                // 创建新的临时指针作为当前活动指针
                current_active_pointer = create_temp_pointer(current_active_pointer);
                if (!current_active_pointer) {
                    fprintf(stderr, "错误: 无法创建临时指针\n");
                    return -1;
                }
                
                if (debug_enabled) {
                    int cell_value = get_value(current_active_pointer);
                    fprintf(stderr, "-> 推入堆栈. 新堆栈顶: %d. 临时指针指向值: %d\n", 
                        interp->pointer_stack_top, cell_value);
                }
                break;
            }
            case ')': {
                if (interp->pointer_stack_top < 0) { 
                    fprintf(stderr, "错误: 临时指针堆栈下溢\n"); return -1;
                }
                
                // 释放当前临时指针
                Pointer* ptr_to_free = current_active_pointer;
                
                // 从堆栈中恢复之前的指针
                current_active_pointer = interp->pointer_stack[interp->pointer_stack_top];
                interp->pointer_stack_top--;
                
                if (debug_enabled) {
                    int cell_value = get_value(current_active_pointer);
                    fprintf(stderr, "-> 弹出堆栈. 新堆栈顶: %d. 活动指针指向值: %d\n", 
                        interp->pointer_stack_top, cell_value);
                }
                
                // 释放临时指针结构体
                free(ptr_to_free);
                break;
            }
            case '*': {
                // 获取当前单元格的值作为偏移量
                int offset = current_active_pointer->get_value(current_active_pointer);
                
                // 执行相对跳转
                if (current_active_pointer->move_relative(current_active_pointer, offset) != 0) {
                    fprintf(stderr, "运行时错误: 相对跳转失败，偏移量: %d, 指令位置: %zu\n", offset, ip);
                    return -1;
                }
                
                if (debug_enabled) {
                    fprintf(stderr, " -> 相对跳转%d个单元格\n", offset);
                }
                break;
            }
        }
        ip++;
    }

     if (instruction_count >= MAX_INSTRUCTIONS) {
        fprintf(stderr, "Warning: Maximum instruction limit reached.\n");
        // Consider returning error or success based on requirements
    }

    // Clean up any remaining temporary pointers if execution ended unexpectedly inside ()
    // (Shouldn't happen with matched parens, but good practice)
    while(interp->pointer_stack_top >= 0) {
        free(interp->pointer_stack[interp->pointer_stack_top]);
        interp->pointer_stack_top--;
    }

    return 0; // Success
}

// --- Main Program Entry ---

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename.bfpp>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // --- Read Code File ---
    FILE* code_file = fopen(argv[1], "r");
    if (!code_file) {
        perror("Error opening code file");
        return EXIT_FAILURE;
    }

    // Get file size
    fseek(code_file, 0, SEEK_END);
    long file_size = ftell(code_file);
    if (file_size < 0) {
        perror("Error getting file size"); fclose(code_file); return EXIT_FAILURE;
    }
    fseek(code_file, 0, SEEK_SET);

    // Basic size check
     if (file_size > MAX_CODE_SIZE * 5) { // Allow extra for comments/whitespace
        fprintf(stderr, "Error: Code file size (%ld bytes) seems excessively large.\n", file_size);
        fclose(code_file); return EXIT_FAILURE;
    }

    // Read into buffer
    char* code_buffer = (char*)malloc(file_size + 1);
    if (!code_buffer) {
        fprintf(stderr, "Error: Failed to allocate memory for code buffer.\n");
        fclose(code_file); return EXIT_FAILURE;
    }
    size_t bytes_read = fread(code_buffer, 1, file_size, code_file);
    if (bytes_read != (size_t)file_size && ferror(code_file)) {
         perror("Error reading code file");
         free(code_buffer); fclose(code_file); return EXIT_FAILURE;
    }
    code_buffer[bytes_read] = '\0';
    fclose(code_file);

    // --- Create and Run Interpreter ---
    Interpreter* interp = create_interpreter(code_buffer, stdin, stdout);
    int run_status = -1;

    if (interp) {
        run_status = run(interp);
        fflush(interp->output); // Ensure all output is written
        free_interpreter(interp);
    }

    // --- Cleanup ---
    free(code_buffer); // Free the raw code buffer read from file

    return (run_status == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

