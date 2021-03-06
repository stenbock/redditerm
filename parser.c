/* FILENAME: parser.c
 *
 * DESCRIPTION: Functions for parsing JSON data.
 *
 * Copyright (C) 2016 Robin Karlsson <s.r.karlsson@gmail.com>.
 * Copyright (C) 2016 Daniel Ohlsson <dohlsson89@gmail.com>.
 *
 * This file is part of redditerm 
 *
 * redditerm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * redditerm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with redditerm.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "parser.h"
#include "error.h"

/* int get_string_jobj
 * get string "str" from json_object *jobj and save the results in dest_str
 * return 0 OK
 * 1 empty string
 * 2 jobj is not a string
 */
int get_string_jobj(json_object *jobj, const char *str, char **dest_str)
{
	size_t len;
	json_object *jstring;

	/* Get title */
	json_object_object_get_ex(jobj, str, &jstring);
	if(json_object_get_type(jstring) != 6) // Check if object is a string
		return JSON_OBJECT_WRONG_TYPE;
	len=strlen(json_object_get_string(jstring));
	if(len < 1)
		return GENERIC_ERROR;
	*dest_str = malloc(sizeof(char) * (len + 1));
	strcpy(*dest_str, json_object_get_string(jstring));

	return 0;

}

void parse_post_idx(post *p, json_object *jobj)
{
	get_string_jobj(jobj, "title", &p->title);
	get_string_jobj(jobj, "author", &p->author);
	get_string_jobj(jobj, "url", &p->url);
	get_string_jobj(jobj, "permalink", &p->permalink);
}

int sub_parse(char *str, post **post_list)
{
	json_object *jobj = json_tokener_parse(str);
	json_object *data;

	json_object *array;
	int array_len;
	json_object *post_json;
	json_object *post_json_data;
	post *parsed_post;
	post *parsed_post_prev;
	int i;

	if (json_object_object_get_ex(jobj, "data", &data) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if (json_object_object_get_ex(data, "children", &array) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if( json_object_get_type(array) != json_type_array )
		return JSON_OBJECT_WRONG_TYPE;

	array_len = json_object_array_length(array);

	for(i=0; i < array_len; i++) {
		parsed_post = malloc(sizeof(post));
		if (i == 0) {
			*post_list = parsed_post;
			parsed_post->head = NULL;
		}

		post_json = json_object_array_get_idx(array, i);
		if( json_object_object_get_ex(post_json, "data", &post_json_data) == 0)
			return JSON_OBJECT_NOT_FOUND;

		parse_post_idx(parsed_post, post_json_data);

		if (i != 0) {
			parsed_post_prev->tail = parsed_post;
			parsed_post->head = parsed_post_prev;
		}

		parsed_post_prev = parsed_post;
	}
	parsed_post->tail = NULL;

	json_object_put(jobj);

	return 0;
}

int traverse_comments_children(int depth, json_object *jobj, comment *c)
{
	int array_lenght;
	int i;

	json_object *data;
	json_object *tmp;
	json_object *children; // replies > data > children

	if( json_object_get_type(jobj) != json_type_array ) // check that we got an array
		return JSON_OBJECT_WRONG_TYPE;

	array_lenght = json_object_array_length(jobj);

	for(i = 0; i < array_lenght; i++) {
		tmp = json_object_array_get_idx(jobj, i);

		/* Get data */
		if (json_object_object_get_ex(tmp, "data", &data) == 0) {
			continue;
		}

		if( i > 0 ) {
			c->tail = malloc(sizeof(comment));
			c->tail->head = c;
			c = c->tail;
		}
		c->tail = NULL;
		c->child = NULL;

		/* Get body string */
		if( get_string_jobj(data, "body", &c->body) != 0)
			c->body = NULL;
		
		// parent_id
		if( get_string_jobj(data, "parent_id", &c->parent_id) != 0) {
			c->parent_id = NULL;	
		}

		// id
		if( get_string_jobj(data, "id", &c->id) != 0) {
			c->id = NULL;	
		}

		// author
		if( get_string_jobj(data, "author", &c->author) != 0) {
			c->author = NULL;	
		}

		/* Get children */
		if (json_object_object_get_ex(data, "replies", &children) != 0 &&
				(json_object_get_type(children) == 4) ) {
			if (json_object_object_get_ex(children, "data", &children) != 0) {
				if (json_object_object_get_ex(children, "children", &children) != 0) {
					if(json_object_get_type(children) == 5) { // is array
						c->child = malloc(sizeof(comment));
						c->child->head = c;
						traverse_comments_children(++depth, children, c->child);
					}
					
				}
			}
		} 
	}

	return 0;
}

/* Parse reddit comments.
 * The comments are stored in a json array[2], where [0] is the root post and
 * all replies lives under [1].
 * */
int comments_parse(char *comments_str, comment **comments_list)
{
	enum json_tokener_error err;
	json_tokener *tok = json_tokener_new_ex(200);
	json_object *jobj = json_tokener_parse_ex(tok, comments_str, strlen(comments_str));

	err = json_tokener_get_error(tok);

	if (err != json_tokener_success) {
		fprintf(stderr, "Error: %s\n", json_tokener_error_desc(err));
		return JSON_FAILED_TO_PARSE;
	}

	json_object *top_post;
	json_object *comments;

	comment *c;

	if( json_object_get_type(jobj) != json_type_array ) // check that we got an array
		return JSON_OBJECT_WRONG_TYPE;

	/* The JSON comments array needs to contain at least two elements */
	if( json_object_array_length(jobj) < 2) 
		return JSON_OBJECT_NOT_FOUND;

	/* Get the top post from:
	 * [0] {"data"} "children"[0] {"data"} string:"selfbody" */
	if(( top_post = json_object_array_get_idx(jobj, 0)) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if( json_object_object_get_ex(top_post, "data", &top_post) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if( json_object_object_get_ex(top_post, "children", &top_post) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if(( top_post = json_object_array_get_idx(top_post, 0)) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if( json_object_object_get_ex(top_post, "data", &top_post) == 0)
		return JSON_OBJECT_NOT_FOUND;

	/* Get the data from the top post */
	c = malloc(sizeof(comment));

	// selftext / body
	if( get_string_jobj(top_post, "selftext", &c->body) != 0) {
		c->body = NULL;	
	}
	// parent_id
	if( get_string_jobj(top_post, "parent_id", &c->parent_id) != 0) {
		c->parent_id = NULL;	
	}
	// id
	if( get_string_jobj(top_post, "id", &c->id) != 0) {
		c->id = NULL;	
	}
	// author
	if( get_string_jobj(top_post, "author", &c->author) != 0) {
		c->author = NULL;	
	}
	c->depth = 0;
	c->head = NULL;
	c->child = NULL;
	c->tail = NULL;
	*comments_list = c;

	c->tail = malloc(sizeof(comment));
	c->tail->head=c;

	/* Get Comments
	 * [1] > data > children
	 */
	if(( comments = json_object_array_get_idx(jobj, 1)) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if( json_object_object_get_ex(comments, "data", &comments) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if( json_object_object_get_ex(comments, "children", &comments) == 0)
		return JSON_OBJECT_NOT_FOUND;

	if(json_object_get_type(comments) == json_type_array) // is array
		traverse_comments_children(0, comments, c->tail);
	else
		return JSON_OBJECT_WRONG_TYPE;

	return 0;
}


