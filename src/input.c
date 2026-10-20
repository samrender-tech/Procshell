#include <signal.h>
#include "procshell.h"

extern struct termios orig_termios;

void enableRawMode()
{
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_oflag |= OPOST; // enable post-processing
  raw.c_oflag |= ONLCR; // map NL to CR-NL

  // Set read timeout so it doesn't block forever
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // Apply the new attributes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    exit(EXIT_FAILURE);
}

void disableRawMode()
{
  // Restore the original terminal attributes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
  {
    exit(EXIT_FAILURE);
  }
}

int getInputString(ForgettingDoublyLinkedList *history, char **input)
{
  char *buffer = NULL;
  size_t buffer_size = 2048;       // initial buffer length
  size_t lastPosition = 0;         // last position of string
  size_t currPosition = 0;         // incase the caret goes left by <- key
  ListNode *currentHistory = NULL; // current History
  char c;

  buffer = malloc(buffer_size);
  if (buffer == NULL)
  {
    return -1;
  }

  while (1)
  {
    // Use read() to get one byte from standard input
    ssize_t nread = read(STDIN_FILENO, &c, 1);
    if (nread == -1)
    {
      if (errno == EINTR) // EINTR means the function was interrupted by a signal!
      {
        clearerr(stdin);
        if (whichSignal == SIGWINCH)
        {
          whichSignal = INT_MIN;
        }
        else if (whichSignal == SIGCHLD)
        {
          whichSignal = INT_MIN;
          printf("\r");
          printPrompt(PROMPT);
          writeBufferOnTerminal(buffer, lastPosition, termCols);
          seek(lastPosition, currPosition, termCols);
        }
        else
        {
          whichSignal = INT_MIN;
          free(buffer);
          return 0;
        }
      }
      else
      {
        perror("getline");
        exit(EXIT_FAILURE);
      }
    }
    else if (termCols < 0) // means needs handling as there's sigwinch
    {
      // reset termCols
      termCols = -termCols;
      if (lastPosition == 0) // nothing has been written
      {
        free(buffer);
        return 1; // invalid output
      }
      write(STDOUT_FILENO, "\n", 1);
      break;
    }
    else if (nread == 0) // means no reading done
    {
      continue;
    }
    else if (c == '\n' || c == EOF || c == 4) // return the string and 4 is EOT(ASCII) for Ctrl+D
    {
      write(STDOUT_FILENO, "\n", 1);
      break;
    }
    else if (c == 12 || c == 13 || c == 11 || c == 9) //  Ctrl + L   Ctrl + M   Ctrl + K   \t
    {
      continue;
    }
    else if (c == '\x1b') // '\x1b' is the escape character
    {
      // Check for arrow keys (Escape Sequences)
      char seq[5] = {};
      // Try to read the rest of the escape sequence
      read(STDIN_FILENO, &seq[0], 5);

      if (seq[0] == '[') // for ansi escape sequence
      {
        if (history && history->size > 0 && seq[1] == 'A') // Up key or Previous history
        {
          // clear previous text on terminal
          seek(currPosition, lastPosition, termCols);
          clearText(lastPosition, termCols);

          if (!currentHistory)
          {
            currentHistory = history->tail;
          }
          else
          {
            currentHistory = getPrevNode(currentHistory);
          }

          int len = myStrlen(currentHistory->command);

          // update the lengths to history command length
          lastPosition = len;
          currPosition = len;

          // update the buffer with new history string
          myStrcpy(buffer, currentHistory->command);
          writeBufferOnTerminal(buffer, lastPosition, termCols);
        }
        else if (history && history->size > 0 && seq[1] == 'B') // Down key or next history
        {
          // clear previous text on terminal
          seek(currPosition, lastPosition, termCols);
          clearText(lastPosition, termCols);

          currentHistory = getNextNode(currentHistory);
          if (!currentHistory)
          {
            lastPosition = 0;
            currPosition = 0;
            buffer[0] = '\0';
          }
          else
          {
            int len = myStrlen(currentHistory->command);

            // update the lengths to history command length
            lastPosition = len;
            currPosition = len;

            // update the buffer with new history string
            myStrcpy(buffer, currentHistory->command);
            writeBufferOnTerminal(buffer, lastPosition, termCols);
          }
        }
        else if (currPosition < lastPosition && seq[1] == 'C') // Right key
        {
          // go right
          if ((myStrlen(PROMPT) + currPosition + 1) % termCols == 0)
          {
            printf(DOWN);
            printf("\r"); // go to start
            fflush(stdout);
          }
          else
          {
            write(STDOUT_FILENO, RIGHT, 3);
          }
          currPosition++;
        }
        else if (currPosition > 0 && seq[1] == 'D') // Left key
        {
          // go left
          if ((myStrlen(PROMPT) + currPosition) % termCols == 0)
          {
            printf(UP);
            printf("\033[%dG", termCols); // goto last column
            fflush(stdout);
          }
          else
          {
            write(STDOUT_FILENO, LEFT, 3);
          }
          currPosition--;
        }
        else if (seq[1] == '1' && seq[2] == ';' && seq[3] == '5') // if control + arrow keys
        {
          if (seq[4] == 'D') // Arrow Left
          {
            continue; // not implemented
          }
          else if (seq[4] == 'C') // Arrow right
          {
            continue; // not implemented
          }
        }
      }
    }
    else if (c == 8 || c == 127) // backspace
    {
      if (currPosition > 0)
      {
        for (size_t i = currPosition; i < lastPosition; i++)
        {
          buffer[i - 1] = buffer[i];
        }
        seek(currPosition, lastPosition, termCols);
        // clear the command
        clearText(lastPosition, termCols);
        lastPosition--;
        currPosition--;
        // write the buffer command
        writeBufferOnTerminal(buffer, lastPosition, termCols);
        // reposition the cursor to where it was before
        seek(lastPosition, currPosition, termCols);
      }
    }
    else if (c == 23)
    {
      // for Ctrl + backspace
      // remove spaces initially
      int leftPos = currPosition;
      while (leftPos > 0 && buffer[leftPos - 1] == ' ')
      {
        leftPos--;
      }
      // on reaching a character remove it till space is reached
      while (leftPos > 0 && buffer[leftPos - 1] != ' ')
      {
        leftPos--;
      }
      int removedCount = currPosition - leftPos;
      for (size_t i = currPosition; i < lastPosition; i++)
      {
        buffer[leftPos++] = buffer[i];
      }

      seek(currPosition, lastPosition, termCols);
      // clear the command
      clearText(lastPosition, termCols);
      lastPosition -= removedCount;
      currPosition -= removedCount;
      // write the buffer command
      writeBufferOnTerminal(buffer, lastPosition, termCols);
      seek(lastPosition, currPosition, termCols);
    }
    else
    {
      // go copy all the words from current position one word later -> i->i+1 ...
      for (size_t i = lastPosition; i > currPosition; i--)
      {
        buffer[i] = buffer[i - 1];
      }

      // add new character
      buffer[currPosition] = c;

      seek(currPosition, lastPosition, termCols);
      // clear current command
      clearText(lastPosition, termCols);
      lastPosition++;
      currPosition++;
      // Write new command
      writeBufferOnTerminal(buffer, lastPosition, termCols);

      // reposition the cursor
      seek(lastPosition, currPosition, termCols);
    }

    // Resize buffer if it's full
    if (lastPosition >= buffer_size)
    {
      buffer_size *= 2;
      char *temp = realloc(buffer, buffer_size);
      if (temp == NULL)
      {
        free(buffer);
        return -1;
      }
      buffer = temp;
    }
  }

  // to start the output of command from next line
  seek(currPosition, lastPosition, termCols);
  printf("\r");
  fflush(stdout);

  buffer[lastPosition] = '\0';
  *input = buffer;

  return 0;
}
