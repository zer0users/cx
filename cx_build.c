#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <zlib.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
    #define PATH_SEPARATOR "\\"
#else
    #include <sys/wait.h>
    #define PATH_SEPARATOR "/"
#endif

#define MAX_LINE_LENGTH 1024
#define MAX_MODULES 50
#define MAX_FILES 100
#define MAX_FOLDERS 50
#define MAX_SHELL_COMMANDS 100
#define MAX_TOKEN_LENGTH 512
#define CX_VERSION "1.0"
#define CX_MAGIC "CXAPP"
#define CX_VERSION_BINARY 1

// Token types
typedef enum {
    TOKEN_COMMENT,
    TOKEN_GET,
    TOKEN_FROM,
    TOKEN_PROGRAM,
    TOKEN_DEFINE,
    TOKEN_CLASS,
    TOKEN_FINISH,
    TOKEN_STRING,
    TOKEN_IDENTIFIER,
    TOKEN_EQUALS,
    TOKEN_DOT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_EOF,
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char value[MAX_TOKEN_LENGTH];
    int line;
    int column;
} Token;

// Project configuration
typedef struct {
    char name[256];
    char platform[64];
    char description[512];
    char version[64];
    char program_class[256];
} ProjectConfig;

// CX App configuration
typedef struct {
    char shell[256];
    char class[256];
} CXAppConfig;

// File entry
typedef struct {
    char src_path[512];
    char dest_path[512];
} FileEntry;

// Binary file header
typedef struct {
    char magic[6];          // "CXAPP\0"
    uint32_t version;       // Binary format version
    uint32_t settings_size; // Size of compressed settings
    uint32_t code_size;     // Size of compressed code
    uint32_t file_count;    // Number of additional files
} __attribute__((packed)) CXHeader;

// File entry in binary format
typedef struct {
    uint32_t name_len;      // Length of filename
    uint32_t data_size;     // Size of compressed file data
} __attribute__((packed)) CXFileHeader;

// CX Interpreter state
typedef struct {
    ProjectConfig project;
    CXAppConfig cx_app;
    char modules[MAX_MODULES][256];
    int module_count;
    char from_modules[MAX_MODULES][256];
    int from_module_count;
    char folders[MAX_FOLDERS][256];
    int folder_count;
    FileEntry files[MAX_FILES];
    int file_count;
    char shell_commands[MAX_SHELL_COMMANDS][512];
    int shell_command_count;
    char main_code[65536];
    bool in_cx_app_class;
    bool found_program_class;
    bool found_cx_app_class;
    int indent_level;
} CXInterpreter;

// Error handling
void cx_error(const char* message, int line) {
    fprintf(stderr, "CX Error at line %d: %s\n", line, message);
    exit(1);
}

// Initialize interpreter
void init_interpreter(CXInterpreter* interp) {
    memset(interp, 0, sizeof(CXInterpreter));
    strcpy(interp->project.version, "1.0");
    strcpy(interp->project.description, "Mi proyecto de amor");
    strcpy(interp->project.platform, "all");
}

// Get current platform
const char* get_current_platform() {
#ifdef _WIN32
    return "windows";
#elif __APPLE__
    return "darwin";
#else
    return "linux";
#endif
}

// String utilities
char* trim_whitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

bool starts_with(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

// Extract string from quotes - FIXED
char* extract_string(const char* str) {
    static char result[512];
    const char* start = strchr(str, '"');
    if (!start) {
        // Try single quotes
        start = strchr(str, '\'');
        if (!start) return NULL;
        start++;
        const char* end = strchr(start, '\'');
        if (!end) return NULL;
        
        int len = end - start;
        strncpy(result, start, len);
        result[len] = '\0';
        return result;
    }
    start++;
    const char* end = strchr(start, '"');
    if (!end) return NULL;
    
    int len = end - start;
    strncpy(result, start, len);
    result[len] = '\0';
    return result;
}

// Tokenizer
TokenType get_token_type(const char* word) {
    if (strcmp(word, "#get") == 0) return TOKEN_GET;
    if (strcmp(word, "#from") == 0) return TOKEN_FROM;
    if (strcmp(word, "program") == 0) return TOKEN_PROGRAM;
    if (strcmp(word, "define") == 0) return TOKEN_DEFINE;
    if (strcmp(word, "class") == 0) return TOKEN_CLASS;
    if (strcmp(word, "finish") == 0) return TOKEN_FINISH;
    if (strcmp(word, "get") == 0) return TOKEN_IDENTIFIER;
    return TOKEN_IDENTIFIER;
}

// Simple tokenizer for CX syntax
Token* tokenize_line(const char* line, int line_num, int* token_count) {
    static Token tokens[100];
    *token_count = 0;
    
    if (line[0] == '?' || (line[0] == '/' && line[1] == '/')) {
        tokens[0].type = TOKEN_COMMENT;
        strcpy(tokens[0].value, line);
        tokens[0].line = line_num;
        *token_count = 1;
        return tokens;
    }
    
    char* line_copy = strdup(line);
    char* ptr = line_copy;
    char current_token[MAX_TOKEN_LENGTH];
    int token_pos = 0;
    bool in_string = false;
    char string_char = 0;
    
    while (*ptr && *token_count < 99) {
        char c = *ptr;
        
        if (!in_string && (c == ' ' || c == '\t')) {
            // End current token
            if (token_pos > 0) {
                current_token[token_pos] = '\0';
                Token* t = &tokens[*token_count];
                t->line = line_num;
                
                if (current_token[0] == '"' || current_token[0] == '\'') {
                    t->type = TOKEN_STRING;
                } else if (strcmp(current_token, "=") == 0) {
                    t->type = TOKEN_EQUALS;
                } else if (strcmp(current_token, ".") == 0) {
                    t->type = TOKEN_DOT;
                } else if (strcmp(current_token, "(") == 0) {
                    t->type = TOKEN_LPAREN;
                } else if (strcmp(current_token, ")") == 0) {
                    t->type = TOKEN_RPAREN;
                } else if (strcmp(current_token, ",") == 0) {
                    t->type = TOKEN_COMMA;
                } else {
                    t->type = get_token_type(current_token);
                }
                
                strcpy(t->value, current_token);
                (*token_count)++;
                token_pos = 0;
            }
        } else if (!in_string && (c == '"' || c == '\'')) {
            // Start of string
            if (token_pos > 0) {
                // End previous token first
                current_token[token_pos] = '\0';
                Token* t = &tokens[*token_count];
                t->line = line_num;
                t->type = get_token_type(current_token);
                strcpy(t->value, current_token);
                (*token_count)++;
                token_pos = 0;
            }
            
            in_string = true;
            string_char = c;
            current_token[token_pos++] = c;
        } else if (in_string && c == string_char) {
            // End of string
            current_token[token_pos++] = c;
            current_token[token_pos] = '\0';
            
            Token* t = &tokens[*token_count];
            t->line = line_num;
            t->type = TOKEN_STRING;
            strcpy(t->value, current_token);
            (*token_count)++;
            
            in_string = false;
            token_pos = 0;
        } else if (!in_string && (c == '=' || c == '.' || c == '(' || c == ')' || c == ',')) {
            // Special single character tokens
            if (token_pos > 0) {
                // End previous token first
                current_token[token_pos] = '\0';
                Token* t = &tokens[*token_count];
                t->line = line_num;
                t->type = get_token_type(current_token);
                strcpy(t->value, current_token);
                (*token_count)++;
                token_pos = 0;
            }
            
            // Add the special character as its own token
            Token* t = &tokens[*token_count];
            t->line = line_num;
            current_token[0] = c;
            current_token[1] = '\0';
            
            if (c == '=') t->type = TOKEN_EQUALS;
            else if (c == '.') t->type = TOKEN_DOT;
            else if (c == '(') t->type = TOKEN_LPAREN;
            else if (c == ')') t->type = TOKEN_RPAREN;
            else if (c == ',') t->type = TOKEN_COMMA;
            
            strcpy(t->value, current_token);
            (*token_count)++;
        } else {
            // Regular character
            if (token_pos < MAX_TOKEN_LENGTH - 1) {
                current_token[token_pos++] = c;
            }
        }
        
        ptr++;
    }
    
    // Handle last token
    if (token_pos > 0) {
        current_token[token_pos] = '\0';
        Token* t = &tokens[*token_count];
        t->line = line_num;
        
        if (current_token[0] == '"' || current_token[0] == '\'') {
            t->type = TOKEN_STRING;
        } else {
            t->type = get_token_type(current_token);
        }
        
        strcpy(t->value, current_token);
        (*token_count)++;
    }
    
    free(line_copy);
    return tokens;
}

// Parse #get statement
void parse_get_statement(CXInterpreter* interp, Token* tokens, int token_count, int line_num) {
    if (token_count < 2) {
        cx_error("Invalid #get statement", line_num);
    }
    
    char* module_name = extract_string(tokens[1].value);
    if (!module_name) {
        cx_error("Expected string after #get", line_num);
    }
    
    strcpy(interp->modules[interp->module_count], module_name);
    interp->module_count++;
    
    printf("Module \"%s\" imported\n", module_name);
}

// Parse #from statement
void parse_from_statement(CXInterpreter* interp, Token* tokens, int token_count, int line_num) {
    if (token_count < 4) {
        cx_error("Invalid #from statement", line_num);
    }
    
    char* from_module = extract_string(tokens[1].value);
    char* get_module = extract_string(tokens[3].value);
    
    if (!from_module || !get_module) {
        cx_error("Expected strings in #from statement", line_num);
    }
    
    strcpy(interp->from_modules[interp->from_module_count], get_module);
    interp->from_module_count++;
    
    printf("Module \"%s\" imported from \"%s\"\n", get_module, from_module);
}

// Parse program statement
void parse_program_statement(CXInterpreter* interp, Token* tokens, int token_count, int line_num) {
    if (token_count < 2) {
        cx_error("Invalid program statement", line_num);
    }
    
    char* class_name = extract_string(tokens[1].value);
    if (!class_name) {
        cx_error("Expected string after program", line_num);
    }
    
    strcpy(interp->project.program_class, class_name);
    printf("Program class set to \"%s\"\n", class_name);
}

// Parse assignment - FIXED JSON parsing
void parse_assignment(CXInterpreter* interp, Token* tokens, int token_count, int line_num) {
    if (token_count < 6) return;
    
    // Look for patterns like: cx.project.name = "value"
    if (token_count >= 7 &&
        strcmp(tokens[0].value, "cx") == 0 && 
        strcmp(tokens[1].value, ".") == 0 &&
        strcmp(tokens[2].value, "project") == 0 &&
        strcmp(tokens[3].value, ".") == 0 &&
        strcmp(tokens[5].value, "=") == 0) {
        
        char* property = tokens[4].value;
        char* value = extract_string(tokens[6].value);
        
        if (!value) {
            cx_error("Expected string value in assignment", line_num);
        }
        
        if (strcmp(property, "name") == 0) {
            strcpy(interp->project.name, value);
        } else if (strcmp(property, "platform") == 0) {
            strcpy(interp->project.platform, value);
        } else if (strcmp(property, "description") == 0) {
            strcpy(interp->project.description, value);
        } else if (strcmp(property, "version") == 0) {
            strcpy(interp->project.version, value);
        }
        
        printf("Project %s set to \"%s\"\n", property, value);
    }
    // Look for cx.app assignments
    else if (token_count >= 7 &&
             strcmp(tokens[0].value, "cx") == 0 && 
             strcmp(tokens[1].value, ".") == 0 &&
             strcmp(tokens[2].value, "app") == 0 &&
             strcmp(tokens[3].value, ".") == 0 &&
             strcmp(tokens[5].value, "=") == 0) {
        
        char* property = tokens[4].value;
        char* value = extract_string(tokens[6].value);
        
        if (!value) {
            cx_error("Expected string value in cx.app assignment", line_num);
        }
        
        if (strcmp(property, "shell") == 0) {
            strcpy(interp->cx_app.shell, value);
            printf("CX app shell set to \"%s\"\n", value);
        } else if (strcmp(property, "class") == 0) {
            strcpy(interp->cx_app.class, value);
            printf("CX app class set to \"%s\"\n", value);
        }
    }
}

// Parse shell.run call
void parse_shell_run(CXInterpreter* interp, Token* tokens, int token_count, int line_num) {
    if (token_count >= 4 && 
        strcmp(tokens[0].value, "shell") == 0 &&
        strcmp(tokens[1].value, ".") == 0 &&
        strcmp(tokens[2].value, "run") == 0 &&
        strcmp(tokens[3].value, "(") == 0) {
        
        char* command = extract_string(tokens[4].value);
        if (!command) {
            cx_error("Expected string in shell.run()", line_num);
        }
        
        strcpy(interp->shell_commands[interp->shell_command_count], command);
        interp->shell_command_count++;
        
        printf("Shell command added: \"%s\"\n", command);
    }
}

// ARREGLADO: Parse files.add call - Ahora funciona como en Five
void parse_files_add(CXInterpreter* interp, Token* tokens, int token_count, int line_num) {
    if (token_count >= 6 &&
        strcmp(tokens[0].value, "files") == 0 &&
        strcmp(tokens[1].value, ".") == 0 &&
        strcmp(tokens[2].value, "add") == 0 &&
        strcmp(tokens[3].value, "(") == 0) {
        
        char* type = extract_string(tokens[4].value);
        if (!type) {
            cx_error("Expected string in files.add()", line_num);
        }
        
        if (strcmp(type, "folder") == 0) {
            // files.add("folder", "nombre_carpeta")
            if (token_count >= 8) {
                char* folder_name = extract_string(tokens[6].value);
                if (!folder_name) {
                    cx_error("Expected folder name in files.add()", line_num);
                }
                strcpy(interp->folders[interp->folder_count], folder_name);
                interp->folder_count++;
                printf("Folder \"%s\" added\n", folder_name);
            }
        } else if (strcmp(type, "file") == 0) {
            // files.add("file", "ruta_origen", "ruta_destino")
            if (token_count >= 10) {
                char* src_path = extract_string(tokens[6].value);
                char* dest_path = extract_string(tokens[8].value);
                if (!src_path || !dest_path) {
                    cx_error("Expected source and destination paths in files.add()", line_num);
                }
                
                // Verificar si el archivo fuente existe y obtener su tamaño
                struct stat file_stat;
                if (stat(src_path, &file_stat) != 0) {
                    printf("Warning: Source file \"%s\" not found\n", src_path);
                } else {
                    long file_size = file_stat.st_size;
                    printf("File \"%s\" -> \"%s\" added (size: %ld bytes = %.2f MB)\n", 
                           src_path, dest_path, file_size, file_size / (1024.0 * 1024.0));
                    
                    if (file_size > 100 * 1024 * 1024) { // 100MB
                        printf("Warning: Large file detected (%ld bytes). This may take time to compress.\n", file_size);
                    }
                }
                
                strcpy(interp->files[interp->file_count].src_path, src_path);
                strcpy(interp->files[interp->file_count].dest_path, dest_path);
                interp->file_count++;
            }
        }
    }
}

// Get indentation level
int get_indent_level(const char* line) {
    int level = 0;
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == ' ') {
            level++;
        } else if (line[i] == '\t') {
            level += 4;
        } else {
            break;
        }
    }
    return level;
}

// Parse CX file
void parse_cx_file(const char* filename, CXInterpreter* interp) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        exit(1);
    }
    
    char line[MAX_LINE_LENGTH];
    int line_num = 0;
    bool in_define_class = false;
    char current_class[256] = "";
    
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Skip empty lines
        if (strlen(trim_whitespace(line)) == 0) continue;
        
        // Check if we're in a CX app class
        if (in_define_class && strcmp(current_class, interp->cx_app.class) == 0) {
            // We're in the cx.app.class, collect as main code
            int indent = get_indent_level(line);
            
            // Remove one level of indentation (the class indentation)
            const char* code_line = line;
            if (indent >= 4) {
                code_line = line + 4; // Skip 4 spaces
            } else if (indent > 0 && line[0] == '\t') {
                code_line = line + 1; // Skip 1 tab
            }
            
            // Check for finish
            if (strstr(trim_whitespace((char*)code_line), "finish") == trim_whitespace((char*)code_line)) {
                in_define_class = false;
                interp->found_cx_app_class = true;
                continue;
            }
            
            // Add to main code
            strcat(interp->main_code, code_line);
            strcat(interp->main_code, "\n");
            continue;
        }
        
        // Tokenize line
        int token_count;
        Token* tokens = tokenize_line(line, line_num, &token_count);
        
        if (token_count == 0) continue;
        
        // Skip comments
        if (tokens[0].type == TOKEN_COMMENT) continue;
        
        // Parse statements
        if (tokens[0].type == TOKEN_GET) {
            parse_get_statement(interp, tokens, token_count, line_num);
        } else if (tokens[0].type == TOKEN_FROM) {
            parse_from_statement(interp, tokens, token_count, line_num);
        } else if (tokens[0].type == TOKEN_PROGRAM) {
            parse_program_statement(interp, tokens, token_count, line_num);
        } else if (tokens[0].type == TOKEN_DEFINE && token_count >= 3 && tokens[1].type == TOKEN_CLASS) {
            char* class_name = extract_string(tokens[2].value);
            if (class_name) {
                strcpy(current_class, class_name);
                in_define_class = true;
                
                if (strcmp(class_name, interp->project.program_class) == 0) {
                    interp->found_program_class = true;
                }
                
                printf("Defining class \"%s\"\n", class_name);
            }
        } else if (tokens[0].type == TOKEN_FINISH) {
            in_define_class = false;
        } else if (in_define_class && strcmp(current_class, interp->project.program_class) == 0) {
            // We're in the main program class
            if (tokens[0].type == TOKEN_IDENTIFIER) {
                if (strcmp(tokens[0].value, "cx") == 0) {
                    parse_assignment(interp, tokens, token_count, line_num);
                } else if (strcmp(tokens[0].value, "shell") == 0) {
                    parse_shell_run(interp, tokens, token_count, line_num);
                } else if (strcmp(tokens[0].value, "files") == 0) {
                    parse_files_add(interp, tokens, token_count, line_num);
                }
            }
        }
    }
    
    fclose(file);
}

// Validate configuration
void validate_config(CXInterpreter* interp) {
    if (strlen(interp->project.name) == 0) {
        cx_error("Missing cx.project.name", 0);
    }
    
    if (strlen(interp->project.program_class) == 0) {
        cx_error("Missing program statement", 0);
    }
    
    if (!interp->found_program_class) {
        cx_error("Program class not found", 0);
    }
    
    if (strlen(interp->cx_app.shell) == 0) {
        cx_error("Missing cx.app.shell", 0);
    }
    
    if (strlen(interp->cx_app.class) == 0) {
        cx_error("Missing cx.app.class", 0);
    }
    
    if (!interp->found_cx_app_class) {
        cx_error("CX app class not found", 0);
    }
}

// Create directory recursively
int create_directory(const char* path) {
    char tmp[512];
    char* p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\')
        tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

// Copy file
int copy_file(const char* src, const char* dest) {
    FILE* source = fopen(src, "rb");
    if (!source) {
        return -1;
    }
    
    // Create destination directory if needed
    char dest_dir[512];
    strcpy(dest_dir, dest);
    char* last_slash = strrchr(dest_dir, '/');
    if (!last_slash) last_slash = strrchr(dest_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
        create_directory(dest_dir);
    }
    
    FILE* destination = fopen(dest, "wb");
    if (!destination) {
        fclose(source);
        return -1;
    }
    
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        fwrite(buffer, 1, bytes, destination);
    }
    
    fclose(source);
    fclose(destination);
    return 0;
}

// Get temp directory based on platform
const char* get_temp_dir() {
#ifdef _WIN32
    const char* temp = getenv("TEMP");
    return temp ? temp : "C:\\temp";
#else
    return "/tmp";
#endif
}

// Remove directory recursively
int remove_directory(const char* path) {
#ifdef _WIN32
    char command[1024];
    snprintf(command, sizeof(command), "rmdir /s /q \"%s\"", path);
    return system(command);
#else
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
    return system(command);
#endif
}

// Compress data using zlib
int compress_data(const char* input, size_t input_len, char** output, uLongf* output_len) {
    *output_len = compressBound(input_len);
    *output = malloc(*output_len);
    if (!*output) return -1;
    
    int result = compress((Bytef*)*output, output_len, (const Bytef*)input, input_len);
    if (result != Z_OK) {
        free(*output);
        return -1;
    }
    
    return 0;
}

// Decompress data using zlib - IMPROVED
int decompress_data(const char* input, size_t input_len, char** output, uLongf* output_len) {
    // Start with a reasonable estimate and grow if needed
    uLongf estimated_len = input_len * 4;
    *output = malloc(estimated_len + 1);
    if (!*output) return -1;
    
    uLongf actual_len = estimated_len;
    int result = uncompress((Bytef*)*output, &actual_len, (const Bytef*)input, input_len);
    
    // If buffer was too small, try with larger buffer
    if (result == Z_BUF_ERROR) {
        free(*output);
        estimated_len = input_len * 10; // Try 10x larger
        *output = malloc(estimated_len + 1);
        if (!*output) return -1;
        
        actual_len = estimated_len;
        result = uncompress((Bytef*)*output, &actual_len, (const Bytef*)input, input_len);
        
        // If still too small, try even larger
        if (result == Z_BUF_ERROR) {
            free(*output);
            estimated_len = input_len * 20; // Try 20x larger
            *output = malloc(estimated_len + 1);
            if (!*output) return -1;
            
            actual_len = estimated_len;
            result = uncompress((Bytef*)*output, &actual_len, (const Bytef*)input, input_len);
        }
    }
    
    if (result != Z_OK) {
        free(*output);
        return -1;
    }
    
    (*output)[actual_len] = '\0';
    *output_len = actual_len;
    return 0;
}

// MEJORADO: Build CX application - Ahora incluye archivos correctamente
void build_cx_app(CXInterpreter* interp) {
    // Create settings JSON
    char settings_json[2048];
    snprintf(settings_json, sizeof(settings_json),
        "{\n"
        "  \"project\": \"%s\",\n"
        "  \"platform\": \"%s\",\n"
        "  \"shell\": \"%s\",\n"
        "  \"description\": \"%s\",\n"
        "  \"version\": \"%s\"\n"
        "}",
        interp->project.name,
        interp->project.platform,
        interp->cx_app.shell,
        interp->project.description,
        interp->project.version
    );
    
    // Compress settings and code
    char* compressed_settings;
    char* compressed_code;
    uLongf settings_compressed_len, code_compressed_len;
    
    if (compress_data(settings_json, strlen(settings_json), &compressed_settings, &settings_compressed_len) != 0) {
        cx_error("Failed to compress settings", 0);
    }
    
    if (compress_data(interp->main_code, strlen(interp->main_code), &compressed_code, &code_compressed_len) != 0) {
        cx_error("Failed to compress code", 0);
    }
    
    // Create .cxA file
    char app_filename[512];
    snprintf(app_filename, sizeof(app_filename), "%s.cxA", interp->project.name);
    
    FILE* app_file = fopen(app_filename, "wb");
    if (!app_file) {
        cx_error("Cannot create .cxA file", 0);
    }
    
    // Write header
    CXHeader header;
    memcpy(header.magic, CX_MAGIC, 6);
    header.version = CX_VERSION_BINARY;
    header.settings_size = settings_compressed_len;
    header.code_size = code_compressed_len;
    header.file_count = interp->file_count;
    
    fwrite(&header, sizeof(header), 1, app_file);
    
    // Write compressed settings
    fwrite(compressed_settings, 1, settings_compressed_len, app_file);
    
    // Write compressed code
    fwrite(compressed_code, 1, code_compressed_len, app_file);
    
    // ARREGLADO: Write additional files - Ahora incluye los archivos correctamente
    printf("Including %d files in the build...\n", interp->file_count);
    
    for (int i = 0; i < interp->file_count; i++) {
        printf("Processing file %d/%d: %s\n", i+1, interp->file_count, interp->files[i].src_path);
        
        FILE* src_file = fopen(interp->files[i].src_path, "rb");
        if (!src_file) {
            fprintf(stderr, "Warning: Could not open file %s (Error: %s)\n", 
                    interp->files[i].src_path, strerror(errno));
            // Actualizar el header para reflejar menos archivos
            header.file_count--;
            continue;
        }
        
        // Get file size
        fseek(src_file, 0, SEEK_END);
        long file_size = ftell(src_file);
        fseek(src_file, 0, SEEK_SET);
        
        printf("File size: %ld bytes (%.2f MB)\n", file_size, file_size / (1024.0 * 1024.0));
        
        if (file_size <= 0) {
            fclose(src_file);
            fprintf(stderr, "Warning: File %s is empty or invalid\n", interp->files[i].src_path);
            header.file_count--;
            continue;
        }
        
        // For very large files, process in chunks to avoid memory issues
        char* file_content;
        size_t bytes_read = 0;
        
        if (file_size > 500 * 1024 * 1024) { // 500MB threshold
            fprintf(stderr, "Warning: File %s is very large (%ld bytes). Consider using smaller files.\n", 
                    interp->files[i].src_path, file_size);
        }
        
        // Try to allocate memory for the file
        file_content = malloc(file_size + 1);
        if (!file_content) {
            fclose(src_file);
            fprintf(stderr, "Error: Cannot allocate %ld bytes for file %s\n", 
                    file_size, interp->files[i].src_path);
            header.file_count--;
            continue;
        }
        
        printf("Reading file into memory...\n");
        bytes_read = fread(file_content, 1, file_size, src_file);
        fclose(src_file);
        
        if (bytes_read != file_size) {
            free(file_content);
            fprintf(stderr, "Warning: Cannot read file %s completely (read %zu of %ld bytes)\n", 
                    interp->files[i].src_path, bytes_read, file_size);
            header.file_count--;
            continue;
        }
        
        printf("Compressing file... (this may take a while for large files)\n");
        
        // Compress file content
        char* compressed_file;
        uLongf compressed_file_len;
        
        // Set compression level based on file size
        int compression_result;
        if (file_size > 50 * 1024 * 1024) { // 50MB+, use faster compression
            printf("Using fast compression for large file...\n");
            compressed_file_len = compressBound(file_size);
            compressed_file = malloc(compressed_file_len);
            if (!compressed_file) {
                free(file_content);
                fprintf(stderr, "Error: Cannot allocate memory for compression of %s\n", interp->files[i].src_path);
                header.file_count--;
                continue;
            }
            compression_result = compress2((Bytef*)compressed_file, &compressed_file_len, 
                                         (const Bytef*)file_content, file_size, Z_BEST_SPEED);
        } else {
            compression_result = compress_data(file_content, file_size, &compressed_file, &compressed_file_len);
        }
        
        if (compression_result != 0) {
            free(file_content);
            fprintf(stderr, "Warning: Cannot compress file %s (compression error: %d)\n", 
                    interp->files[i].src_path, compression_result);
            header.file_count--;
            continue;
        }
        
        printf("Compressed from %ld to %lu bytes (%.1f%% reduction)\n", 
               file_size, compressed_file_len, 
               100.0 * (1.0 - (double)compressed_file_len / file_size));
        
        // Write file header
        CXFileHeader file_header;
        file_header.name_len = strlen(interp->files[i].dest_path);
        file_header.data_size = compressed_file_len;
        
        if (fwrite(&file_header, sizeof(file_header), 1, app_file) != 1) {
            free(file_content);
            free(compressed_file);
            fprintf(stderr, "Error: Cannot write file header for %s\n", interp->files[i].src_path);
            header.file_count--;
            continue;
        }
        
        // Write filename
        if (fwrite(interp->files[i].dest_path, 1, file_header.name_len, app_file) != file_header.name_len) {
            free(file_content);
            free(compressed_file);
            fprintf(stderr, "Error: Cannot write filename for %s\n", interp->files[i].src_path);
            header.file_count--;
            continue;
        }
        
        // Write compressed file data
        printf("Writing compressed data to .cxA file...\n");
        if (fwrite(compressed_file, 1, compressed_file_len, app_file) != compressed_file_len) {
            free(file_content);
            free(compressed_file);
            fprintf(stderr, "Error: Cannot write compressed data for %s\n", interp->files[i].src_path);
            header.file_count--;
            continue;
        }
        
        free(file_content);
        free(compressed_file);
        printf("✓ Successfully added: \"%s\" -> \"%s\" (original: %ld bytes, compressed: %lu bytes)\n", 
               interp->files[i].src_path, interp->files[i].dest_path, file_size, compressed_file_len);
    }
    
    // Update the header with correct file count
    fseek(app_file, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, app_file);
    
    fclose(app_file);
    
    free(compressed_settings);
    free(compressed_code);
    
    printf("Build done! Check your app as \"%s\"\n", app_filename);
    printf("Total files included: %d\n", header.file_count);
    printf("Total folders created: %d\n", interp->folder_count);
}

// Run CX application - BINARY FORMAT
void run_cx_app(const char* app_filename) {
    FILE* app_file = fopen(app_filename, "rb");
    if (!app_file) {
        fprintf(stderr, "Error: Cannot open %s\n", app_filename);
        exit(1);
    }
    
    // Read and verify header
    CXHeader header;
    if (fread(&header, sizeof(header), 1, app_file) != 1) {
        fprintf(stderr, "Error: Cannot read file header\n");
        fclose(app_file);
        exit(1);
    }
    
    if (memcmp(header.magic, CX_MAGIC, 5) != 0) {
        fprintf(stderr, "Error: Invalid .cxA file (bad magic)\n");
        fclose(app_file);
        exit(1);
    }
    
    if (header.version != CX_VERSION_BINARY) {
        fprintf(stderr, "Error: Unsupported .cxA version %d\n", header.version);
        fclose(app_file);
        exit(1);
    }
    
    printf("Loading CX application with %d files...\n", header.file_count);
    
    // Read compressed settings
    char* compressed_settings = malloc(header.settings_size);
    if (!compressed_settings || fread(compressed_settings, 1, header.settings_size, app_file) != header.settings_size) {
        fprintf(stderr, "Error: Cannot read settings data\n");
        if (compressed_settings) free(compressed_settings);
        fclose(app_file);
        exit(1);
    }
    
    // Read compressed code
    char* compressed_code = malloc(header.code_size);
    if (!compressed_code || fread(compressed_code, 1, header.code_size, app_file) != header.code_size) {
        fprintf(stderr, "Error: Cannot read code data\n");
        free(compressed_settings);
        if (compressed_code) free(compressed_code);
        fclose(app_file);
        exit(1);
    }
    
    // Decompress settings - IMPROVED
    char* settings_content;
    uLongf settings_uncompressed_len;
    if (decompress_data(compressed_settings, header.settings_size, &settings_content, &settings_uncompressed_len) != 0) {
        fprintf(stderr, "Error: Cannot decompress settings\n");
        free(compressed_settings);
        free(compressed_code);
        fclose(app_file);
        exit(1);
    }
    
    // Decompress code - IMPROVED
    char* code_content;
    uLongf code_uncompressed_len;
    if (decompress_data(compressed_code, header.code_size, &code_content, &code_uncompressed_len) != 0) {
        fprintf(stderr, "Error: Cannot decompress code\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        fclose(app_file);
        exit(1);
    }
    
    // Create temp extraction directory
    char temp_dir[512];
    snprintf(temp_dir, sizeof(temp_dir), "%s%stemp_cx_run_%d", get_temp_dir(), PATH_SEPARATOR, getpid());
    create_directory(temp_dir);
    
    // Write extracted files
    char settings_path[512];
    snprintf(settings_path, sizeof(settings_path), "%s%ssettings.json", temp_dir, PATH_SEPARATOR);
    FILE* settings_file = fopen(settings_path, "w");
    if (settings_file) {
        fputs(settings_content, settings_file);
        fclose(settings_file);
    } else {
        fprintf(stderr, "Error: Cannot create settings file\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        free(code_content);
        fclose(app_file);
        remove_directory(temp_dir);
        exit(1);
    }
    
    char code_path[512];
    snprintf(code_path, sizeof(code_path), "%s%smain.cx-code", temp_dir, PATH_SEPARATOR);
    FILE* code_file = fopen(code_path, "w");
    if (code_file) {
        fputs(code_content, code_file);
        fclose(code_file);
    } else {
        fprintf(stderr, "Error: Cannot create code file\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        free(code_content);
        fclose(app_file);
        remove_directory(temp_dir);
        exit(1);
    }
    
    // MEJORADO: Extract additional files - Ahora funciona correctamente
    for (uint32_t i = 0; i < header.file_count; i++) {
        CXFileHeader file_header;
        if (fread(&file_header, sizeof(file_header), 1, app_file) != 1) {
            fprintf(stderr, "Warning: Cannot read file header %d\n", i);
            continue;
        }
        
        // Read filename
        char* filename = malloc(file_header.name_len + 1);
        if (!filename) {
            fprintf(stderr, "Warning: Cannot allocate memory for filename %d\n", i);
            continue;
        }
        
        if (fread(filename, 1, file_header.name_len, app_file) != file_header.name_len) {
            free(filename);
            fprintf(stderr, "Warning: Cannot read filename %d\n", i);
            continue;
        }
        filename[file_header.name_len] = '\0';
        
        // Read compressed file data
        char* compressed_file_data = malloc(file_header.data_size);
        if (!compressed_file_data) {
            free(filename);
            fprintf(stderr, "Warning: Cannot allocate memory for file data %d\n", i);
            continue;
        }
        
        if (fread(compressed_file_data, 1, file_header.data_size, app_file) != file_header.data_size) {
            free(filename);
            free(compressed_file_data);
            fprintf(stderr, "Warning: Cannot read file data %d\n", i);
            continue;
        }
        
        // Decompress file data - IMPROVED
        char* file_data;
        uLongf file_uncompressed_len;
        if (decompress_data(compressed_file_data, file_header.data_size, &file_data, &file_uncompressed_len) == 0) {
            // Write extracted file
            char extracted_path[512];
            snprintf(extracted_path, sizeof(extracted_path), "%s%s%s", temp_dir, PATH_SEPARATOR, filename);
            
            // Create directory if needed
            char extracted_dir[512];
            strcpy(extracted_dir, extracted_path);
            char* last_slash = strrchr(extracted_dir, '/');
            if (!last_slash) last_slash = strrchr(extracted_dir, '\\');
            if (last_slash) {
                *last_slash = '\0';
                create_directory(extracted_dir);
            }
            
            FILE* extracted_file = fopen(extracted_path, "wb");
            if (extracted_file) {
                fwrite(file_data, 1, file_uncompressed_len, extracted_file);
                fclose(extracted_file);
                printf("Extracted file: %s (%lu bytes)\n", filename, file_uncompressed_len);
            } else {
                fprintf(stderr, "Warning: Cannot create extracted file %s\n", filename);
            }
            
            free(file_data);
        } else {
            fprintf(stderr, "Warning: Cannot decompress file %s\n", filename);
        }
        
        free(filename);
        free(compressed_file_data);
    }
    
    fclose(app_file);
    
    // Parse shell command from JSON settings - IMPROVED
    char shell_command[256] = {0};
    char* shell_start = strstr(settings_content, "\"shell\":");
    if (shell_start) {
        // Find the value after "shell":
        shell_start = strchr(shell_start, ':');
        if (shell_start) {
            shell_start++; // Skip ':'
            
            // Skip whitespace
            while (*shell_start && (*shell_start == ' ' || *shell_start == '\t' || *shell_start == '\n')) {
                shell_start++;
            }
            
            // Should be a quote now
            if (*shell_start == '"') {
                shell_start++; // Skip opening quote
                char* shell_end = strchr(shell_start, '"');
                if (shell_end) {
                    int shell_len = shell_end - shell_start;
                    if (shell_len < sizeof(shell_command)) {
                        strncpy(shell_command, shell_start, shell_len);
                        shell_command[shell_len] = '\0';
                    }
                }
            }
        }
    }
    
    if (strlen(shell_command) == 0) {
        fprintf(stderr, "Error: Cannot find shell command in settings\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        free(code_content);
        remove_directory(temp_dir);
        exit(1);
    }
    
    // Debug output
    printf("Shell command: %s\n", shell_command);
    printf("Running application...\n");
    
    // Verify that the code file exists
    if (access(code_path, F_OK) != 0) {
        fprintf(stderr, "Error: Code file was not created properly\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        free(code_content);
        remove_directory(temp_dir);
        exit(1);
    }
    
    // Execute the code with the specified shell
    char execute_command[1024];
    snprintf(execute_command, sizeof(execute_command), "%s \"%s\"", shell_command, code_path);
    
    // Change to temp directory and execute
    char original_dir[512];
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        fprintf(stderr, "Error: Cannot get current directory\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        free(code_content);
        remove_directory(temp_dir);
        exit(1);
    }
    
    if (chdir(temp_dir) != 0) {
        fprintf(stderr, "Error: Cannot change to temp directory\n");
        free(compressed_settings);
        free(compressed_code);
        free(settings_content);
        free(code_content);
        remove_directory(temp_dir);
        exit(1);
    }
    
    int result = system(execute_command);
    
    // Return to original directory
    chdir(original_dir);
    
    // Clean up
    free(compressed_settings);
    free(compressed_code);
    free(settings_content);
    free(code_content);
    remove_directory(temp_dir);
    
    if (result != 0) {
        fprintf(stderr, "Application execution completed with code %d\n", result);
    }
}

// Build command
void build_command() {
    printf("Building app: ");
    
    // Check for main.cx
    if (access("main.cx", F_OK) != 0) {
        cx_error("main.cx not found in current directory", 0);
    }
    
    printf("Checking for main.cx..\n");
    
    // Initialize interpreter
    CXInterpreter interp;
    init_interpreter(&interp);
    
    // Parse main.cx
    parse_cx_file("main.cx", &interp);
    
    // Validate configuration
    validate_config(&interp);
    
    printf("%s..\n", interp.project.name);
    
    // Build the application
    build_cx_app(&interp);
}

// Show help
void show_help() {
    printf("CX Programming Language v%s - Faster than Five ❤️\n", CX_VERSION);
    printf("Usage:\n");
    printf("  cx build                     - Build application from main.cx\n");
    printf("  cx <app.cxA>                 - Run CX application\n");
    printf("  cx version                   - Show version\n");
    printf("  cx help                      - Show this help\n");
    printf("\n");
    printf("File structure:\n");
    printf("  main.cx                      - Main source file\n");
    printf("  (other files for files.add) - Additional resources\n");
    printf("\n");
    printf("Build output:\n");
    printf("  <project_name>.cxA           - CX Application file (binary compressed)\n");
    printf("\n");
    printf("Binary .cxA structure:\n");
    printf("  Header (magic, version, sizes)\n");
    printf("  Compressed settings.json\n");
    printf("  Compressed main.cx-code\n");
    printf("  Additional compressed files\n");
    printf("\n");
    printf("files.add() syntax:\n");
    printf("  files.add(\"folder\", \"folder_name\")           - Create folder in app\n");
    printf("  files.add(\"file\", \"src_path\", \"dest_path\")    - Include file in app\n");
}

// Main function
int main(int argc, char* argv[]) {
    if (argc < 2) {
        show_help();
        return 0;
    }
    
    const char* command = argv[1];
    
    if (strcmp(command, "build") == 0) {
        build_command();
    } else if (strcmp(command, "version") == 0) {
        printf("CX Programming Language v%s - Faster than Five ❤️\n", CX_VERSION);
    } else if (strcmp(command, "help") == 0) {
        show_help();
    } else if (strstr(command, ".cxA") != NULL) {
        // Run CX application
        run_cx_app(command);
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        fprintf(stderr, "Use 'cx help' for usage information\n");
        return 1;
    }
    
    return 0;
}
