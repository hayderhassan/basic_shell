#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUF_SIZE 1000
#define MAX_ARGS_SIZE 1000

#define DELIM ";|&"

typedef struct statement {
    char *argv[MAX_ARGS_SIZE];
    FILE *input_redir;
    FILE *output_redir;
    char terminator;
    struct statement *next;
} statement;

struct statement *head;
struct statement *temp;

FILE *output_file, *input_file;
FILE *writable, *readable;

int args_length;
char *args[MAX_ARGS_SIZE + 1];
char cwd[1024];

int run_in_background = 0;

// creates a pipe
int fpipe(FILE **readable, FILE **writable) {
  int fd[2];

  if (pipe(fd) == -1) {
    return 0;
  }

  *readable = fdopen(fd[0], "r");
  *writable = fdopen(fd[1], "w");

  if (!*readable || !*writable) {
    return 0;
  }

  return 1;
}

/* takes a line of text from stdin and stores it in buffer
   returns buffer or NULL if no text is enterred */
char *read_line(char *buf, size_t sz) {


    // loop while text is being enterred
    while (fgets(buf, sz, stdin)) {

      /* if there was no new line character in buf: deal with any extra characters
          being taken as input in next iteration of main loop */
      if (!strchr(buf, '\n')) {
        while(fgetc(stdin) != '\n');
      }

      // replace trailing new line character from buffer with null terminator
      if (buf[strlen(buf) - 1] == '\n'){
        buf[strlen(buf) - 1] = '\0';
      }

      return buf;
    }
    return NULL;

}

void show_prompt() {
   if (getcwd(cwd, sizeof(cwd)) != NULL){
    printf("%s> ", cwd);
   } else {
    perror("getcwd() error");
   }
}

// returns last character from a string
char last_char(char *word) {

  char *last_word = word;
  int last_word_length = strlen(last_word);
  char last_character = last_word[last_word_length - 1];

  return last_character;
}

// returns position of a redirection operator in string, or -1 if not found
int redirection(char *str){

    for(int i = 0; i < strlen(str); i++){
        if (str[i] == '>' || str[i] == '<'){
            return i;
        }
    }

    return -1;
}

// reads statement into a struct
void read_statement(char *buf) {

  char *token;
  char last_character;
  int counter = 0;
  int length = strlen(buf);

  temp = head;
  temp = (statement*) malloc(sizeof(statement));

  int redir_pos = redirection(buf);

  char *filename, *line;

  line = strdup(buf);

  last_character = last_char(line);

  if (strchr(DELIM, last_character)) {
    temp->terminator = last_character;
    line[length - 1] = '\0';
  } else {
    temp->terminator = ';';
  }

  //check if statement contains a redirection operator
  if (redir_pos > 0){
    filename = strdup(line + redir_pos + 1);
    filename = strtok(filename, " ");

    if(line[redir_pos] == '>'){
        output_file = fopen(filename, "w");
        temp->output_redir = output_file;
        temp->input_redir = NULL;
    } else{
        input_file = fopen(filename, "r");
        temp->input_redir = input_file;
        temp->output_redir = NULL;
    }
    line = strndup(line, redir_pos);
  } else {
    temp->input_redir = NULL;
    temp->output_redir = NULL;
  }

  // extract the first token
  token = strtok(line, " ");

  // extract remaining tokens
  while(token){
    temp->argv[counter] = token;
    token = strtok(NULL, " ");
    counter++;
  }
    temp->next = head;
    head = temp;
}

// returns 1 if process should run in background, otherwise 0
int background_process(statement *stmt) {
    if(stmt->terminator == '&'){
        return 1;
    }else{
        return 0;
    }
}

// logic to redirect processes
void redirect_process(statement *stmt) {

    if (stmt->output_redir) {
        dup2(fileno_unlocked(output_file), STDOUT_FILENO);
        close(fileno_unlocked(output_file));
    }

    if (stmt->input_redir) {
        dup2(fileno_unlocked(input_file), STDIN_FILENO);
        close(fileno_unlocked(input_file));
    }

}

// code for piping a process (doesn't work!!)
void pipe_process(statement *stmt){

    if (stmt->output_redir) {
        dup2(fileno_unlocked(writable), STDOUT_FILENO);
        close(fileno_unlocked(writable));
    }

    if (stmt->input_redir) {
        dup2(fileno_unlocked(readable), STDIN_FILENO);
        close(fileno_unlocked(readable));
    }

}

// runs the command
pid_t execute_statement(statement *stmt) {

   int child_status;
   pid_t pid;

  // if command is 'cd' change directory
    if (strcmp(stmt->argv[0], "cd") == 0){
        chdir(stmt->argv[1]);
        return pid;
    }

    run_in_background = background_process(stmt);

    if (stmt->terminator == '|') {
        if (!fpipe(&readable, &writable)) {
            return -1;
        }
    }

  // create new process
  pid = fork();

  if (pid > 0) {
        if (run_in_background) {
        printf("[%d]\n", pid);
    } else {
        waitpid(pid, &child_status, 0);
    }
  } else if (pid == 0){

      redirect_process(stmt);

      if (execvp(stmt->argv[0], stmt->argv)) {
          perror("Error");
      }
      exit(0);
  } else {
      // error in creating process
      perror("Error: \n");
      exit(-1);
  }

  return pid;
}

// splits statements by their terminators
statement *split_statements(char *inBuf) {

  char *tokens[100];
  char *line;
  int line_size = strlen(inBuf);
  int start = 0, end = 0;

  int statements = 0;

  line = strdup(inBuf);

  for (int i = 0; i < line_size; i++) {
    if (strchr(DELIM, line[i])) {
        end = i + 1;
        tokens[statements] = strndup(inBuf + start, end - start);
        start = i + 1;
        statements++;
    }
  }

    if (end < line_size){
        tokens[statements] = strndup(inBuf + start, line_size);
        statements++;
    }

    head = (statement*) malloc(sizeof(statement));

    for (int i = statements - 1; i >= 0; i--) {
        read_statement(tokens[i]);
    }

  free(line);

return head;

}

int main(int argc, char *argv[]) {

  struct statement *node;
  char buf[BUF_SIZE + 1];
  size_t sz = sizeof(buf);

  // main program loop
  while (1) {

    show_prompt();

    // read line while user is typing
    if (!read_line(buf, sz)) {
      break;
    }
     node = (statement*) malloc(sizeof(statement));

    // only split buf and execute commands if user enters a string
    if (*buf) {

     node = split_statements(buf);

     // loop through all statements
     while(node->next) {
        execute_statement(node);
        node = node->next;
     }

    }

    free(node);

  }

  printf("\nGoodbye!\n");

  return 0;
}
