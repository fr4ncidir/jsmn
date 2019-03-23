#include "jsmn.h"

/**
 * Allocates a fresh unused token from the token pull.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
		jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *tok;
	if (parser->toknext >= num_tokens) {
		return NULL;
	}
	tok = &tokens[parser->toknext++];
	tok->start = tok->end = -1;
	tok->size = 0;
#ifdef JSMN_PARENT_LINKS
	tok->parent = -1;
#endif
	return tok;
}

/**
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type,
                            int start, int end) {
	token->type = type;
	token->start = start;
	token->end = end;
	token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
		size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;
	int start;

	start = parser->pos;

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		switch (js[parser->pos]) {
#ifndef JSMN_STRICT
			/* In strict mode primitive must be followed by "," or "}" or "]" */
			case ':':
#endif
			case '\t' : case '\r' : case '\n' : case ' ' :
			case ','  : case ']'  : case '}' :
				goto found;
		}
		if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		}
	}
#ifdef JSMN_STRICT
	/* In strict mode primitive must be followed by a comma/object/array */
	parser->pos = start;
	return JSMN_ERROR_PART;
#endif

found:
	if (tokens == NULL) {
		parser->pos--;
		return 0;
	}
	token = jsmn_alloc_token(parser, tokens, num_tokens);
	if (token == NULL) {
		parser->pos = start;
		return JSMN_ERROR_NOMEM;
	}
	jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
#ifdef JSMN_PARENT_LINKS
	token->parent = parser->toksuper;
#endif
	parser->pos--;
	return 0;
}

/**
 * Fills next token with JSON string.
 */
static int jsmn_parse_string(jsmn_parser *parser, const char *js,
		size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;

	int start = parser->pos;

	parser->pos++;

	/* Skip starting quote */
	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c = js[parser->pos];

		/* Quote: end of string */
		if (c == '\"') {
			if (tokens == NULL) {
				return 0;
			}
			token = jsmn_alloc_token(parser, tokens, num_tokens);
			if (token == NULL) {
				parser->pos = start;
				return JSMN_ERROR_NOMEM;
			}
			jsmn_fill_token(token, JSMN_STRING, start+1, parser->pos);
#ifdef JSMN_PARENT_LINKS
			token->parent = parser->toksuper;
#endif
			return 0;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\' && parser->pos + 1 < len) {
			int i;
			parser->pos++;
			switch (js[parser->pos]) {
				/* Allowed escaped symbols */
				case '\"': case '/' : case '\\' : case 'b' :
				case 'f' : case 'r' : case 'n'  : case 't' :
					break;
				/* Allows escaped symbol \uXXXX */
				case 'u':
					parser->pos++;
					for(i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
						/* If it isn't a hex character we have an error */
						if(!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
									(js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
									(js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
							parser->pos = start;
							return JSMN_ERROR_INVAL;
						}
						parser->pos++;
					}
					parser->pos--;
					break;
				/* Unexpected symbol */
				default:
					parser->pos = start;
					return JSMN_ERROR_INVAL;
			}
		}
	}
	parser->pos = start;
	return JSMN_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
		jsmntok_t *tokens, unsigned int num_tokens) {
	int r;
	int i;
	jsmntok_t *token;
	int count = parser->toknext;

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c;
		jsmntype_t type;

		c = js[parser->pos];
		switch (c) {
			case '{': case '[':
				count++;
				if (tokens == NULL) {
					break;
				}
				token = jsmn_alloc_token(parser, tokens, num_tokens);
				if (token == NULL)
					return JSMN_ERROR_NOMEM;
				if (parser->toksuper != -1) {
					tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
					token->parent = parser->toksuper;
#endif
				}
				token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
				token->start = parser->pos;
				parser->toksuper = parser->toknext - 1;
				break;
			case '}': case ']':
				if (tokens == NULL)
					break;
				type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
#ifdef JSMN_PARENT_LINKS
				if (parser->toknext < 1) {
					return JSMN_ERROR_INVAL;
				}
				token = &tokens[parser->toknext - 1];
				for (;;) {
					if (token->start != -1 && token->end == -1) {
						if (token->type != type) {
							return JSMN_ERROR_INVAL;
						}
						token->end = parser->pos + 1;
						parser->toksuper = token->parent;
						break;
					}
					if (token->parent == -1) {
						if(token->type != type || parser->toksuper == -1) {
							return JSMN_ERROR_INVAL;
						}
						break;
					}
					token = &tokens[token->parent];
				}
#else
				for (i = parser->toknext - 1; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						if (token->type != type) {
							return JSMN_ERROR_INVAL;
						}
						parser->toksuper = -1;
						token->end = parser->pos + 1;
						break;
					}
				}
				/* Error if unmatched closing bracket */
				if (i == -1) return JSMN_ERROR_INVAL;
				for (; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						parser->toksuper = i;
						break;
					}
				}
#endif
				break;
			case '\"':
				r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
				if (parser->toksuper != -1 && tokens != NULL)
					tokens[parser->toksuper].size++;
				break;
			case '\t' : case '\r' : case '\n' : case ' ':
				break;
			case ':':
				parser->toksuper = parser->toknext - 1;
				break;
			case ',':
				if (tokens != NULL && parser->toksuper != -1 &&
						tokens[parser->toksuper].type != JSMN_ARRAY &&
						tokens[parser->toksuper].type != JSMN_OBJECT) {
#ifdef JSMN_PARENT_LINKS
					parser->toksuper = tokens[parser->toksuper].parent;
#else
					for (i = parser->toknext - 1; i >= 0; i--) {
						if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT) {
							if (tokens[i].start != -1 && tokens[i].end == -1) {
								parser->toksuper = i;
								break;
							}
						}
					}
#endif
				}
				break;
#ifdef JSMN_STRICT
			/* In strict mode primitives are: numbers and booleans */
			case '-': case '0': case '1' : case '2': case '3' : case '4':
			case '5': case '6': case '7' : case '8': case '9':
			case 't': case 'f': case 'n' :
				/* And they must not be keys of the object */
				if (tokens != NULL && parser->toksuper != -1) {
					jsmntok_t *t = &tokens[parser->toksuper];
					if (t->type == JSMN_OBJECT ||
							(t->type == JSMN_STRING && t->size != 0)) {
						return JSMN_ERROR_INVAL;
					}
				}
#else
			/* In non-strict mode every unquoted value is a primitive */
			default:
#endif
				r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
				if (parser->toksuper != -1 && tokens != NULL)
					tokens[parser->toksuper].size++;
				break;

#ifdef JSMN_STRICT
			/* Unexpected char in strict mode */
			default:
				return JSMN_ERROR_INVAL;
#endif
		}
	}

	if (tokens != NULL) {
		for (i = parser->toknext - 1; i >= 0; i--) {
			/* Unmatched opened object or array */
			if (tokens[i].start != -1 && tokens[i].end == -1) {
				return JSMN_ERROR_PART;
			}
		}
	}

	return count;
}

/**
 * Creates a new parser based over a given  buffer with an array of tokens
 * available.
 */
void jsmn_init(jsmn_parser *parser) {
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}

/**
 * From a json text, a start index and the text length, returns an integer as 
 * the number of tokens necessary for this text to be parsed with jsmn.
 */
int jsmn_getTokenLen(const char* json, int startIndex, int len) {
    jsmn_parser _parser;
    jsmntok_t *_jstokens;
    int _jstok_dim;
    
    jsmn_init(&_parser);
    _jstok_dim = jsmn_parse(&_parser, json+startIndex, len, NULL, 0);
    return _jstok_dim;
}

/**
 * See jsmn_explore, jsmn_parse_explore.
 */
int jsmn_variadic_explore(const char* json,
                          char **result,
                          jsmntok_t *jstokens,
                          int jstok_dim,
                          int len,
                          va_list args) {
    int i, j=1, tok_len, game_over=jstok_dim, i_result, error=0;
    char *t_content, *current_parameter;
    
    current_parameter = va_arg(args, char*);
    for (i=0; (i<jstok_dim) && (game_over>0); i++) {
        if (j<len) {
            if ((jstokens[i].type == JSMN_STRING) && (jstokens[i+1].type == JSMN_OBJECT)) {
                tok_len = jstokens[i].end-jstokens[i].start;
                t_content = strndup(&(json[jstokens[i].start]), tok_len);
                if (t_content == NULL) {
                    error = -2;
                    break;
                }
                
                if (!strcmp(t_content, current_parameter)) {
                    j++;
                    current_parameter = va_arg(args, char*);
                    game_over = jsmn_getTokenLen(json, jstokens[i+1].start, jstokens[i+1].end-jstokens[i+1].start);
                }
            }
        }
        else {
            if (jstokens[i].type == JSMN_STRING) {
                tok_len = jstokens[i].end-jstokens[i].start;
                t_content = strndup(&(json[jstokens[i].start]), tok_len);
                if (t_content == NULL) {
                    error = -3;
                    break;
                }
                
                if (!strcmp(t_content, current_parameter)) {
                    i_result = i+1;
                    tok_len = jstokens[i+1].end-jstokens[i_result].start;
                    *result = strndup(&(json[jstokens[i_result].start]), tok_len);
                    break;
                }
            }
        }
        game_over--;
    }
    if ((i==jstok_dim) || (!game_over) || (error)) {
        i_result = -1+error;
        *result = NULL;
    }
    return i_result;
}

/**
 * Given a json string, we look for a path within it in variadic arguments.
 * The content of the path is stored in the string pointed to by 'result',
 * while the parsed json is given through jstokens array and jstok_dim.
 * 'len' is the number of variadic arguments.
 *
 * Will return the token index upon success, storing a string in 'result', in case of correct path;
 * or -1 and NULL, in case of 'path not found'.
 */
int jsmn_explore(const char* json,
                 char **result,
                 jsmntok_t *jstokens,
                 int jstok_dim,
                 int len, ...) {
    int r;
    va_list args;
    
    va_start(args, len);
    r = jsmn_variadic_explore(json, result, jstokens, jstok_dim, len, args);
    va_end(args);
    return r;
}

/**
 * Given a json string, we look for a path within it in variadic arguments.
 * The content of the path is stored in the string pointed to by 'result'.
 * The json string is parsed on-the-go.
 *
 * Will return the token number upon success, storing a string in 'result', in case of correct path.
 * Or NULL and a negative integer, in case of error or 'path not found'.
 */
int jsmn_parse_explore(const char *json, char **result, int len, ...) {
    jsmn_parser parser;
    jsmntok_t *jstokens;
    int jstok_dim, json_len, r;
    va_list args;
    
    va_start(args, len);
    *result = NULL;
    json_len = strlen(json);
    jstok_dim = jsmn_getTokenLen(json, 0, json_len);
    if (jstok_dim<0) return -1;

    jstokens = (jsmntok_t *) malloc(jstok_dim*sizeof(jsmntok_t));
    if (jstokens == NULL) return -2;

    jsmn_init(&parser);
    if (jsmn_parse(&parser, json, json_len, jstokens, jstok_dim)<0) {
        free(jstokens);
        return -3;
    }

    r = jsmn_variadic_explore(json, result, jstokens, jstok_dim, len, args);
    va_end(args);
    free(jstokens);
    return r;
}
