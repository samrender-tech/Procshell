#include "procshell.h"

VectorToken *getTokens(char *input, char **env)
{
  int i = 0;
  VectorToken *tokenVec = (VectorToken *)malloc(sizeof(VectorToken));
  if (!tokenVec)
  {
    return NULL;
  }
  if (initVecToken(tokenVec, 16) == -1)
  {
    free(tokenVec);
    return NULL;
  }

  Token *currToken = createToken(NULL, 0, false, 1);
  if (!currToken)
    goto errorHandle;

  bool isDoubleQuotes = false;
  bool isSingleQuotes = false;
  bool isOperator = false;
  bool isTokenEnd = false;
  while (true)
  {
    if (isTokenEnd || input[i] == '\0')
    {
      if (currToken->len > 0)
      {
        currToken->isOperator = isOperator;

        Token *expandedToken = expandToken(currToken, isOperator, isDoubleQuotes, isSingleQuotes, env);
        if (!expandedToken)
          goto errorHandle;

        if (expandedToken->len > 0 && pushVecToken(tokenVec, expandedToken) == -1)
        {
          freeToken(expandedToken);
          goto errorHandle;
        }
        freeToken(currToken); // currToken not needed as replaced by expanded tokens
        currToken = createToken(NULL, 0, false, 1);
        if (!currToken)
          goto errorHandle;
        isDoubleQuotes = false;
        isSingleQuotes = false;
        isOperator = false;
        isTokenEnd = false;
      }

      if (input[i] == '\0')
        break;
    }
    if (input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r') // whitespace ends a token
    {
      isTokenEnd = true;
      i++;
    }
    else if (input[i] == '"' || input[i] == '\'')
    {
      // a quote inside a word continues the same word: --msg="a b" is ONE argument
      char quote = input[i];

      i++; // skip " or '
      while (input[i] && input[i] != quote)
      {
        if (input[i] == '\\')
        {
          if (insertInTokenChar(currToken, input[i++]) == -1)
          {
            goto errorHandle;
          }
          if (!input[i])
          {
            fprintf(stderr, "syntax error\n");
            goto errorHandle;
          }
        }

        if (insertInTokenChar(currToken, input[i++]) == -1)
        {
          goto errorHandle;
        }
      }
      if (!input[i])
      {
        fprintf(stderr, "syntax error expected a %c\n", quote);
        goto errorHandle;
      }
      if (quote == '"')
        isDoubleQuotes = true;
      else
        isSingleQuotes = true;

      i++; // skip " or '
      isTokenEnd = false;
    }
    else if (input[i] == ';' || input[i] == '<')
    {
      if (currToken->len > 0) // flush the token
      {
        isTokenEnd = true;
        continue;
      }
      if (insertInTokenChar(currToken, input[i++]) == -1)
      {
        goto errorHandle;
      }
      isOperator = true;
      isTokenEnd = true;
    }
    else if (input[i] == '>' || input[i] == '&' || input[i] == '|')
    {
      if (currToken->len > 0) // flush the token
      {
        isTokenEnd = true;
        continue;
      }
      if (insertInTokenChar(currToken, input[i++]) == -1)
      {
        goto errorHandle;
      }
      if (input[i] && input[i] == input[i - 1])
      {
        if (insertInTokenChar(currToken, input[i++]) == -1)
        {
          goto errorHandle;
        }
      }
      isOperator = true;
      isTokenEnd = true;
    }
    else if (input[i] == '\\' && !input[i + 1])
    {
      fprintf(stderr, "syntax error near \\\n");
      goto errorHandle;
    }
    else // all general characters
    {
      if (input[i] == '\\')
      {
        if (insertInTokenChar(currToken, input[i++]) == -1)
        {
          goto errorHandle;
        }
        if (!input[i])
        {
          fprintf(stderr, "sytnax error expected a '\n");
          goto errorHandle;
        }
      }

      if (insertInTokenChar(currToken, input[i++]) == -1)
      {
        goto errorHandle;
      }
      isTokenEnd = false;
    }
  }

  freeToken(currToken); // at the end redundant token will be created
  return tokenVec;

errorHandle:
  freeVecToken(tokenVec);
  freeToken(currToken);
  return NULL;
}

Token *expandToken(Token *token, bool isOperator, bool isDQuotes, bool isSQuotes, char **env)
{

  (void)isDQuotes;
  if (!token)
    return NULL;

  char *input = token->token;
  Token *expandedToken;

  if (isOperator || isSQuotes) // if it's an operator or is in single quotes then no need for expansion
  {
    int len = myStrlen(input);
    expandedToken = createToken(input, len, isOperator, len);
    if (!expandedToken)
      goto errorHandle;

    return expandedToken;
  }
  else
  {
    expandedToken = createToken(NULL, 0, false, 1);
    if (!expandedToken)
      return NULL;
  }

  int i = 0;

  while (input[i])
  {
    if (input[i] == '\\')
    {
      i++;
      if (insertInTokenChar(expandedToken, input[i++]) == -1)
      {
        goto errorHandle;
      }
    }
    else if (input[i] == '$') // find the enviornment variable related to the given token
    {
      i++; // skip $
      if (!(isalnum((unsigned char)input[i]) || input[i] == '_'))
      {
        // a lone '$' (e.g. "echo $" or "cost $ 5") is just a dollar sign
        if (insertInTokenChar(expandedToken, '$') == -1)
          goto errorHandle;
        continue;
      }
      char *envKeyStart = &input[i]; // starting pointer of environment variable key

      while (isalnum((unsigned char)input[i]) || input[i] == '_') // find the end point of environment variable name
        i++;

      char lastChar = input[i]; // store the last character so that we can replace it back
      input[i] = '\0';          // after finding the enviornment variable we can subsitute back the input[i] = lastChar

      char *envVal = myGetenv(envKeyStart, env);
      if (insertInTokenStr(expandedToken, envVal) == -1)
      {
        goto errorHandle;
      }

      input[i] = lastChar;
      free(envVal);
    }
    else
    {
      // general characters
      if (insertInTokenChar(expandedToken, input[i++]) == -1)
      {
        goto errorHandle;
      }
    }
  }

  return expandedToken;

errorHandle:
  freeToken(expandedToken);

  return NULL;
}

Commands *splitCommands(VectorToken *tokenVec)
{
  PipelineComponent *pc = NULL; // pipeline component
  Pipeline *p = NULL;           // pipeline
  Command *fc = NULL;           // full command

  Commands *ac = createCommands();
  if (!ac)
    goto errorHandler;

  Token **allTokens = tokenVec->data;

  int i = 0;
  while (allTokens[i])
  {
    Token *token = allTokens[i];

    if (isLt(token) || isGt(token) || isAppend(token))
    {
      Token *nextToken = allTokens[i + 1];
      if (!pc || nextToken == NULL || isDelimiter(nextToken))
      {
        // if first token or next commnad is not a file then throw an error
        fprintf(stderr, "error near %s\n", token->token);
        goto errorHandler;
      }
    }
    else if (isPipe(token))
    {
      if (!pc)
      {
        fprintf(stderr, "error near %s\n", token->token);
        goto errorHandler;
      }

      if (!p)
      {
        p = createPipeline();
        if (!p)
          goto errorHandler;
      }

      if (pushVecPipelineComponent(p->components, pc) == -1)
      {
        goto errorHandler;
      }
      pc = NULL;
      i++;
      continue;
    }
    else if (isLogicalOp(token))
    {
      if (!pc)
      {
        fprintf(stderr, "error near %s\n", token->token);
        goto errorHandler;
      }

      if (!p)
      {
        p = createPipeline();
        if (!p)
          goto errorHandler;
      }

      if (!fc)
      {
        fc = createCommand();
        if (!fc)
          goto errorHandler;
      }

      if (pushVecPipelineComponent(p->components, pc) == -1)
      {
        goto errorHandler;
      }
      if (pushVecPipeline(fc->pipelines, p) == -1)
      {
        goto errorHandler;
      }

      if (strcmp(token->token, "&&") == 0)
      {
        p->separator = AND;
      }
      else if (strcmp(token->token, "||") == 0)
      {
        p->separator = OR;
      }

      pc = NULL;
      p = NULL;
      i++;
      continue;
    }
    else if (isSemicolon(token) || isBackground(token))
    {
      if (!pc)
      {
        fprintf(stderr, "error near %s\n", token->token);
        goto errorHandler;
      }

      if (!p)
      {
        p = createPipeline();
        if (!p)
          goto errorHandler;
      }

      if (!fc)
      {
        fc = createCommand();
        if (!fc)
          goto errorHandler;
      }

      if (pushVecPipelineComponent(p->components, pc) == -1)
      {
        goto errorHandler;
      }
      if (pushVecPipeline(fc->pipelines, p) == -1)
      {
        goto errorHandler;
      }

      if (isBackground(token))
      {
        fc->isBackground = true;
      }
      if (pushVecCommand(ac->commands, fc) == -1)
      {
        goto errorHandler;
      }

      p->separator = -1; // not real separator it just ;
      pc = NULL;
      p = NULL;
      fc = NULL;
      i++;
      continue;
    }

    if (!pc)
    {
      pc = createPipelineComponent();
      if (!pc)
        goto errorHandler;
    }

    if (isLt(token))
      pc->isLt = true;
    if (isGt(token) || isAppend(token))
      pc->isGt = true;

    int copyTokenLen = myStrlen(token->token);
    Token *newToken = createToken(token->token, copyTokenLen, token->isOperator, copyTokenLen);
    if (!newToken)
    {
      fprintf(stdout, "new token creation failed\n");
      goto errorHandler;
    }
    if (pushVecToken(pc->tokens, newToken) == -1)
    {
      goto errorHandler;
    }
    i++;
  }

  // if there is no ; or & at the end of the input it won't be added in the Commmands *
  // so manually adding it
  if (i > 0 && !isSemicolon(allTokens[i - 1]) && !isBackground(allTokens[i - 1]))
  {
    if (!pc)
    {
      fprintf(stderr, "error: Invalid input near 'EOF'\n");
      goto errorHandler;
    }

    if (!p)
    {
      p = createPipeline();
      if (!p)
        goto errorHandler;
    }

    if (!fc)
    {
      fc = createCommand();
      if (!fc)
        goto errorHandler;
    }

    if (pushVecPipelineComponent(p->components, pc) == -1)
    {
      goto errorHandler;
    }
    if (pushVecPipeline(fc->pipelines, p) == -1)
    {
      goto errorHandler;
    }
    if (pushVecCommand(ac->commands, fc) == -1)
    {
      goto errorHandler;
    }
  }

  /* for (int i = 0; i < ac->commands->size; i++)
  {
    Command *currCommand = ac->commands->data[i];

    for (int j = 0; j < currCommand->pipelines->size; j++)
    {
      Pipeline *currPipeline = currCommand->pipelines->data[j];
      for (int k = 0; k < currPipeline->components->size; k++)
      {
        PipelineComponent *currPipelineComp = currPipeline->components->data[k];
        for (int m = 0; m < currPipelineComp->tokens->size; m++)
        {
          printf("'%s' ", currPipelineComp->tokens->data[m]->token);
        }
        printf("\n-- Lt: %d Gt: %d\n", currPipelineComp->isLt, currPipelineComp->isGt);
      }
      printf("Separator: %d\n", currPipeline->separator);
      printf("----\n");
    }
    printf("--------\n");
  } */

  return ac;

errorHandler:
  if (pc)
    freePipelineComponent(pc);
  if (p)
    freePipeline(p);
  if (fc)
    freeCommand(fc);
  freeCommands(ac);

  return NULL;
}
