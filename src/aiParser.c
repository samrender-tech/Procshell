#include "procshell.h"

int parseAI(AICommands **commands, AIQuestions **questions, const char *path)
{
  *commands = NULL;
  *questions = NULL;

  if (!path)
  {
    return -1;
  }
  // user should send both of them as null this function will initialize them
  if (*commands != NULL || *questions != NULL)
  {
    return -1;
  }

  FILE *f;
  if ((f = fopen(path, "r")) == NULL)
    return -1;

  char *buff = (char *)malloc(sizeof(char) * (1 << 14));
  int allocSize = sizeof(char) * (1 << 14);

  while (fgets(buff, allocSize, f) != NULL)
  {
    buff[strcspn(buff, "\n")] = '\0';
    if (strncmp(buff, "===OUTPUT===", strlen("===OUTPUT===")) == 0)
    {
      if (fgets(buff, allocSize, f) == NULL) // if eof and no output type written in file
        goto errorHandle;
      if (strncmp("======", buff, strlen("======")) == 0) // no warning
        goto errorHandle;                                 // no type specified
      buff[strcspn(buff, "\n")] = '\0';

      if (strncmp("command", buff, strlen("command")) == 0)
      {
        if (*commands || *questions) // if already initialized means it's reappearing
          goto errorHandle;

        *commands = createAICommand();
        if (!*commands)
          goto errorHandle;
      }
      else if (strncmp("question", buff, strlen("question")) == 0)
      {
        if (*commands || *questions) // if already initialized means it's reappearing
          goto errorHandle;

        *questions = createAIQuestions();
        if (!*questions)
          goto errorHandle;
      }
      else // if neither command nor question
      {
        goto errorHandle;
      }
      if (fgets(buff, allocSize, f) == NULL || strncmp("======", buff, strlen("======")) != 0) // if no segment termination
        goto errorHandle;
    }
    else if (strncmp("===EXPLANATION===", buff, strlen("===EXPLANATION===")) == 0)
    {
      if (fgets(buff, allocSize, f) == NULL)
        goto errorHandle;

      if (!*commands && !*questions)
        goto errorHandle;

      buff[strcspn(buff, "\n")] = '\0';
      char *explanation = strdup(buff);
      if (!explanation)
        goto errorHandle;
      if (*commands)
        (*commands)->explanation = explanation;
      else if (*questions)
        (*questions)->explanation = explanation;

      if (fgets(buff, allocSize, f) == NULL || strncmp("======", buff, strlen("======")) != 0) // segment termination
        goto errorHandle;
    }
    else if (strncmp("===WARNING===", buff, strlen("===WARNING===")) == 0)
    {
      if (fgets(buff, allocSize, f) == NULL)
        goto errorHandle;

      if (!*commands)
        goto errorHandle;

      buff[strcspn(buff, "\n")] = '\0';
      char *warning = strdup(buff);
      if (!warning)
        goto errorHandle;

      (*commands)->warning = warning;

      if (fgets(buff, allocSize, f) == NULL || strncmp("======", buff, strlen("======")) != 0) // segment termination
        goto errorHandle;
    }
    else if (strncmp("===COMMANDS===", buff, strlen("===COMMANDS===")) == 0)
    {
      if (!*commands)
        goto errorHandle;

      int commandCount = 0;
      bool segmentTerminationFound = false;
      while (fgets(buff, allocSize, f) != NULL)
      {
        if (strncmp("======", buff, strlen("======")) == 0)
        {
          segmentTerminationFound = true;
          break;
        }

        buff[strcspn(buff, "\n")] = '\0';
        if (commandCount >= MAX_AI_COMMANDS)
          goto errorHandle;
        char *temp = strdup(buff);
        if (!temp)
          goto errorHandle;
        (*commands)->commands[commandCount++] = temp;
      }
      (*commands)->commandsCount = commandCount;

      if (!segmentTerminationFound)
        goto errorHandle;
    }
    else if (strncmp("===QUESTIONS===", buff, strlen("===QUESTIONS===")) == 0)
    {
      if (!*questions)
        goto errorHandle;

      int questionsCount = 0;
      bool segmentTerminationFound = false;
      while (fgets(buff, allocSize, f) != NULL)
      {
        if (strncmp("======", buff, strlen("======")) == 0)
        {
          segmentTerminationFound = true;
          break;
        }

        buff[strcspn(buff, "\n")] = '\0';
        if (questionsCount >= MAX_AI_QUESTIONS)
          goto errorHandle;
        char *temp = strdup(buff);
        if (!temp)
          goto errorHandle;
        (*questions)->questions[questionsCount++] = temp;
      }
      (*questions)->questionsCount = questionsCount;

      if (!segmentTerminationFound)
        goto errorHandle;
    }
    else if (strncmp("======", buff, strlen("======")) == 0) // end of segment
    {
      continue;
    }
    else // if anything wrong input
    {
      goto errorHandle;
    }
  }

  free(buff);
  fclose(f);
  return 0;

errorHandle:
  free(buff);
  fclose(f);
  freeAICommands(*commands);
  *commands = NULL;
  freeAIQuestions(*questions);
  *questions = NULL;
  return -1;
}

/* int main()
{
  AICommands *commands = NULL;
  AIQuestions *questions = NULL;
  parseAI(&commands, &questions, "ai_output.txt");

  printf("%p, %p\n", commands, questions);

  if (commands)
  {
    printf("explaination: '%s'\n", commands->explanation);
    printf("warning: '%s'\n", commands->warning);
    for (int i = 0; i < commands->commandsCount; i++)
    {
      printf("command %d: '%s'\n", i + 1, commands->commands[i]);
    }
  }
  else if (questions)
  {
    printf("explaination: '%s'\n", questions->explanation);
    for (int i = 0; i < questions->questionsCount; i++)
    {
      printf("command %d: '%s'\n", i + 1, questions->questions[i]);
    }
  }
  printf("\n");

  freeAICommands(commands);
  commands = NULL;
  freeAIQuestions(questions);
  questions = NULL;

  return 0;
} */