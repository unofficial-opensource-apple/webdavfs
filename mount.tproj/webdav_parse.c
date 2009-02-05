/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*		@(#)webdav_parse.c		*
 *		(c) 1999   Apple Computer, Inc.	 All Rights Reserved
 *
 *
 *		webdav_parse.c -- WebDAV front end to CF XML parser
 *
 *		MODIFICATION HISTORY:
 *				19-JUL-99	  Clark Warner		File Creation
 */

#include <CoreFoundation/CoreFoundation.h>
#include <sys/syslog.h>
#include <sys/dirent.h>
#include "webdavd.h"
#include "webdav_parse.h"
#include "webdav_memcache.h"
#include "webdav_inode.h"
#include "fetch.h"
#include "pathnames.h"

/*****************************************************************************/

extern time_t parse_http_date(char *string);

/*****************************************************************************/

/* local function prototypes */

static void *parser_opendir_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context);
static void *parser_file_count_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context);
static void *parser_stat_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context);
static void *parser_statfs_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context);
static void *parser_lock_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context);
static void parser_add(CFXMLParserRef parser, void *parent, void *child, void *context);
static void parser_opendir_add(CFXMLParserRef parser, void *parent, void *child, void *context);
static void parser_stat_add(CFXMLParserRef parser, void *parent, void *child, void *context);
static void parser_statfs_add(CFXMLParserRef parser, void *parent, void *child, void *context);
static void parser_end(CFXMLParserRef parser, void *xml_type, void *context);
static void parser_opendir_end(CFXMLParserRef parser, void *my_element, void *context);
static CFDataRef parser_resolve(CFXMLParserRef parser, CFXMLExternalID *extID, void *context);

/*****************************************************************************/

#define SUBSTITUTE_PHYSICAL_ENTITY(node, text_ptr, size) \
	{ \
		if (CFXMLNodeGetTypeCode(node) == kCFXMLNodeTypeEntityReference) \
		{ \
			if (!strcmp(text_ptr, "amp")) \
				strcpy(text_ptr, "&"); \
			else if (!strcmp(text_ptr, "lt")) \
				strcpy(text_ptr, "<"); \
			else if (!strcmp(text_ptr, "gt")) \
				strcpy(text_ptr, ">"); \
			else if (!strcmp(text_ptr, "apos")) \
				strcpy(text_ptr, "'"); \
			else if (!strcmp(text_ptr, "quot")) \
				strcpy(text_ptr, """"); \
			size = 1; \
		} \
	}

static void *parser_opendir_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context)
{
	#pragma unused(parser)
	webdav_parse_opendir_element_t * element_ptr = NULL;
	webdav_parse_opendir_element_t * list_ptr;
	webdav_parse_opendir_struct_t * struct_ptr = (webdav_parse_opendir_struct_t *)context;
	webdav_parse_opendir_return_t * return_ptr;
	webdav_parse_opendir_text_t * text_ptr;
	void *return_value;
	CFRange comparison_range;
	CFStringRef nodeString = CFXMLNodeGetString(node);

#ifdef DEBUG_PARSE
	char buffer[257];
	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			fprintf(stderr, "Element Type");
			break;
			
		case kCFXMLNodeTypeDocument:
			fprintf(stderr, "Document Type");
			break;
			
		case kCFXMLNodeTypeWhitespace:
			fprintf(stderr, "WhiteSpace");
			break;
			
		case kCFXMLNodeTypeText:
			fprintf(stderr, "TextType");
			break;
			
		default:
			fprintf(stderr, "Unknown Type");
	}

	CFStringGetCString(CFXMLNodeGetString(node), buffer, (CFIndex)256, kCFStringEncodingUTF8);
	fprintf(stderr, ": Element string is: %s\n", buffer);

#endif

	/* set up our return */
	return_ptr = malloc(sizeof(webdav_parse_opendir_return_t));
#ifdef DEBUG_PARSE
	fprintf(stderr, "parser_opendir_create: malloc return_ptr %d\n", (int)return_ptr);
#endif

	/* XXX Need to set up generic error mechanism in case
	 * we can only partially make the tree */

	if (!return_ptr)
	{
		syslog(LOG_ERR, "parser_opendir_create: return_ptr could not be allocated");
		struct_ptr->error = ENOMEM;
		return (NULL);
	}

	return_ptr->id = WEBDAV_OPENDIR_IGNORE;
	return_ptr->data_ptr = (void *)NULL;
	return_value = (void *)return_ptr;

	/* See if this is the resource type.  If it is, malloc a webdav_parse_opendir_element_t element and
	  add it to the list.	If not, return something but keep parsing */

	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			comparison_range = CFStringFind(nodeString, CFSTR(":"), 0);
			comparison_range.location++;
			comparison_range.length = CFStringGetLength(nodeString) - comparison_range.location;
			if (((CFStringCompareWithOptions(nodeString, CFSTR("href"), comparison_range,
				kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				element_ptr = malloc(sizeof(webdav_parse_opendir_element_t));
#ifdef DEBUG_PARSE
				fprintf(stderr, "parser_opendir_create: malloc element_ptr %d\n", (int)element_ptr);
#endif

				if (!element_ptr)
				{
#ifdef DEBUG_PARSE
					fprintf(stderr,
						"parser_opendir_create: free return_ptr on failed element_ptr malloc %d\n",
						(int)return_ptr);
#endif
					syslog(LOG_ERR, "parser_opendir_create: element_ptr could not be allocated");
					free(return_ptr);
					struct_ptr->error = ENOMEM;
					return (NULL);
				}
				bzero(element_ptr, sizeof(webdav_parse_opendir_element_t));
				return_ptr->id = WEBDAV_OPENDIR_ELEMENT;
				return_ptr->data_ptr = (void *)element_ptr;
				element_ptr->dir_data.d_type = DT_REG;
				element_ptr->dir_data.d_namlen = 0;
				element_ptr->dir_data.d_name_URI_length = 0;
				element_ptr->dir_data.d_reclen = sizeof(struct dirent);
				element_ptr->next = NULL;

				if (struct_ptr->head == NULL)
				{
					struct_ptr->head = element_ptr;
				}
				else
				{
					list_ptr = struct_ptr->tail;
					list_ptr->next = element_ptr;
				}
				struct_ptr->tail = element_ptr;
				element_ptr->next = NULL;
			}	/* end if href */
			else if (((CFStringCompareWithOptions(nodeString, CFSTR("collection"),
					comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				/* If we have a collection property, we should have an
					* element ptr in the context already.	If not, its
					* an error and we should barf. Since we are parsing an
					* xml tree, we can rest assured that the last element
					* found (href) is the one have just seen a resource type
					* on and hence the one where collection is found and
					* trapped here */

				element_ptr = struct_ptr->tail;
				if (element_ptr)
				{
					element_ptr->dir_data.d_type = DT_DIR;

					/* not interested in child of collection element. We can 
						* can and should free the return_ptr in this case.*/

					return_value = NULL;
#ifdef DEBUG_PARSE
					fprintf(stderr, "parser_opendir_create: free return_ptr on collection %d\n",
						(int)return_ptr);
#endif

					free(return_ptr);
				}
			}	/* end if collection */
			else if (((CFStringCompareWithOptions(nodeString, CFSTR("getcontentlength"),
					comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				/* If we have size then mark up the element pointer so that the
					* child will know to parse the upcoming text size and store it.
					*/
				element_ptr = struct_ptr->tail;
				if (element_ptr)
				{
					return_ptr->id = WEBDAV_OPENDIR_ELEMENT_LENGTH;
					return_ptr->data_ptr = (void *)element_ptr;
				}
			}	/* end if length */
			else if (((CFStringCompareWithOptions(nodeString, CFSTR("getlastmodified"),
					comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				/* If we have size then mark up the element pointer so that the
					* child will know to parse the upcoming text size and store it.
					*/
				element_ptr = struct_ptr->tail;
				if (element_ptr)
				{
					return_ptr->id = WEBDAV_OPENDIR_ELEMENT_MODDATE;
					return_ptr->data_ptr = (void *)element_ptr;
				}
			}	/* end if modified */
			else if (((CFStringCompareWithOptions(nodeString, CFSTR("appledoubleheader"),
					comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				/* If we have size then mark up the element pointer so that the
					* child will know to parse the upcoming text size and store it.
					*/
				element_ptr = struct_ptr->tail;
				if (element_ptr)
				{
					return_ptr->id = WEBDAV_OPENDIR_APPLEDOUBLEHEADER;
					return_ptr->data_ptr = (void *)element_ptr;
				}
			}	/* end if appledoubleheader */
			break;
			
		case kCFXMLNodeTypeEntityReference:
		case kCFXMLNodeTypeText:
		case kCFXMLNodeTypeCDATASection:
			/* If it is a text element, create a structure and pass it back 
			 * we have no way of knowing if this is the text of an href in the
			 * xml (what we are looking for) or some other random text.	 Set it
			 * up and then addchild will figure it out by checking the type of
			 * the child to add and the type of the parent.
			 */

			text_ptr = malloc(sizeof(webdav_parse_opendir_text_t));
#ifdef DEBUG_PARSE
			fprintf(stderr, "parser_opendir_create: malloc text_ptr %d\n", (int)text_ptr);
#endif

			if (!text_ptr)
			{
#ifdef DEBUG_PARSE

				fprintf(stderr,
					"parser_opendir_create: free return_ptr on failed text_ptr malloc %d\n",
					(int)return_ptr);
#endif

				syslog(LOG_ERR, "parser_opendir_create: text_ptr could not be allocated");
				free(return_ptr);
				struct_ptr->error = ENOMEM;
				return (NULL);
			}

			/* Get the bytes out in UTF8 form */

			if (CFStringGetBytes(nodeString, CFRangeMake(0, CFStringGetLength(nodeString)),
				kCFStringEncodingUTF8, /*stop on loss */ 0,/*not ext*/  0, text_ptr->name,
				sizeof(text_ptr->name) - 1, &text_ptr->size) != CFStringGetLength(nodeString))
			{
#ifdef DEBUG_PARSE
				fprintf(stderr, "Get Length Says %d\n", (int)CFStringGetLength(nodeString));
				fprintf(stderr, "Get Bytes Says %d\n",
					(int)CFStringGetBytes(nodeString, CFRangeMake(0,
						CFStringGetLength(nodeString)), kCFStringEncodingUTF8, 0, 0,
							text_ptr->name, sizeof(text_ptr->name) - 1, &text_ptr->size));
				fprintf(stderr, "parser_opendir_create: free return ptr on name too long %d\n",
					(int)return_ptr);
#endif
				syslog(LOG_ERR, "parser_opendir_create: name too long");
				free(return_ptr);
				struct_ptr->error = ENAMETOOLONG;
				return (NULL);
			}

			text_ptr->name[text_ptr->size] = '\0';

			/* Workaround for lack of support for kCFXMLParserReplacePhysicalEntities option *** */
			SUBSTITUTE_PHYSICAL_ENTITY(node, text_ptr->name, text_ptr->size);

			return_ptr->id = WEBDAV_OPENDIR_TEXT;
			return_ptr->data_ptr = text_ptr;
			break;

		default:
			break;

	}	/* end switch */

	return (return_value);
}

/*****************************************************************************/

static void *parser_file_count_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context)
{
	#pragma unused(parser)
	CFRange comparison_range;

#ifdef DEBUG_PARSE
	char buffer[257];
	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			fprintf(stderr, "Element Type");
			break;
			
		case kCFXMLNodeTypeDocument:
			fprintf(stderr, "Document Type");
			break;
			
		case kCFXMLNodeTypeWhitespace:
			fprintf(stderr, "WhiteSpace");
			break;
			
		case kCFXMLNodeTypeText:
			fprintf(stderr, "TextType");
			break;
			
		default:
			fprintf(stderr, "Unknown Type");
	}

	CFStringGetCString(CFXMLNodeGetString(node), buffer, (CFIndex)256, kCFStringEncodingUTF8);
	fprintf(stderr, ": Element string is: %s\n", buffer);

#endif

	/* Look for hrefs & count them. */

	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			{
				CFStringRef str = CFXMLNodeGetString(node);
				comparison_range = CFStringFind(str, CFSTR(":"), 0);
				comparison_range.location++;
				comparison_range.length = CFStringGetLength(str) - comparison_range.location;
				if (((CFStringCompareWithOptions(str, CFSTR("href"), comparison_range,
					kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
				{
					*((int *)context) += 1;
				}
				/* end if href */

				break;
			}
			
		default:
			break;
	}											/* end switch */

	return ((void *)1);							/* have to return something or the parser will stop parsing */
}

/*****************************************************************************/

static void *parser_stat_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context)
{
	#pragma unused(parser)
	void *return_val = (void *)WEBDAV_STAT_IGNORE;
	char *text_ptr;
	size_t size;
	CFRange comparison_range;
	CFStringRef nodeString = CFXMLNodeGetString(node);
	/* See if this is a type we are interested in  If it is, we'll return
	  the appropriate constant */

	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			comparison_range = CFStringFind(nodeString, CFSTR(":"), 0);
			comparison_range.location++;
			comparison_range.length = CFStringGetLength(nodeString) - comparison_range.location;

			if (((CFStringCompareWithOptions(nodeString, CFSTR("getcontentlength"),
				comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				return_val = (void *)WEBDAV_STAT_LENGTH;
			}
			else
			{
				if (((CFStringCompareWithOptions(nodeString, CFSTR("getlastmodified"),
					comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
				{
					return_val = (void *)WEBDAV_STAT_MODDATE;
				}
				else
				{
					if (((CFStringCompareWithOptions(nodeString, CFSTR("collection"),
						comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
					{
						/* It's a collection so set the type as VDIR */
						((struct stat *)context)->st_mode = S_IFDIR;
					}	/* end if collection */
				}	/* end of if-else mod date */
			}	/* end if-else length*/
			break;

		case kCFXMLNodeTypeEntityReference:
		case kCFXMLNodeTypeText:
		case kCFXMLNodeTypeCDATASection:
			text_ptr = malloc(WEBDAV_MAX_STAT_SIZE);
			
			/* Get the bytes out in UTF8 form */
			if (CFStringGetBytes(nodeString, CFRangeMake(0, CFStringGetLength(nodeString)),
				kCFStringEncodingUTF8, /*stop on loss */ 0,/*not ext*/  0, text_ptr,
				WEBDAV_MAX_STAT_SIZE - 1, &size) != CFStringGetLength(nodeString))
			{
				/* we could not get the name in the buffer (possibly because the server spewed
				 * crap xpl at us) so return nothing
				 */
				syslog(LOG_ERR, "parser_stat_create: name too long or malformed");
				free(text_ptr);
				return_val = NULL;
			}
			else
			{
				/* null terminate the string */
				text_ptr[size] = '\0';

				/* Workaround for lack of support for kCFXMLParserReplacePhysicalEntities option *** */
				SUBSTITUTE_PHYSICAL_ENTITY(node, text_ptr, size);

				return_val = (void *)text_ptr;
			}

		default:
			break;

	}	/* end switch */

	return (return_val);
}

/*****************************************************************************/

static void *parser_statfs_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context)
{
	#pragma unused(parser, context)
	void *return_val = (void *)WEBDAV_STATFS_IGNORE;
	char *text_ptr;
	size_t size;
	CFRange comparison_range;
	CFStringRef nodeString = CFXMLNodeGetString(node);
	/* See if this is a type we are interested in  If it is, we'll return
	  the appropriate constant */

	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			comparison_range = CFStringFind(nodeString, CFSTR(":"), 0);
			comparison_range.location++;
			comparison_range.length = CFStringGetLength(nodeString) - comparison_range.location;

			if (((CFStringCompareWithOptions(nodeString, CFSTR("quota"), comparison_range,
				kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				return_val = (void *)WEBDAV_STATFS_QUOTA;
			}
			else
			{
				if (((CFStringCompareWithOptions(nodeString, CFSTR("quotaused"),
					comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
				{
					return_val = (void *)WEBDAV_STATFS_QUOTAUSED;
				}
			}
			break;

		case kCFXMLNodeTypeEntityReference:
		case kCFXMLNodeTypeText:
		case kCFXMLNodeTypeCDATASection:

			text_ptr = malloc(WEBDAV_MAX_STATFS_SIZE);
			/* Get the bytes out in UTF8 form */

			if (CFStringGetBytes(nodeString, CFRangeMake(0, CFStringGetLength(nodeString)),
				kCFStringEncodingUTF8, /*stop on loss */ 0,/*not ext*/  0, text_ptr,
				WEBDAV_MAX_STATFS_SIZE - 1, &size) != CFStringGetLength(nodeString))
			{
				/* we could not get the name in the buffer so return
				  nothing */
				syslog(LOG_ERR, "parser_statfs_create: name too long");
				free(text_ptr);
				return_val = NULL;
			}
			else
			{
				/* null terminate the string */
				text_ptr[size] = '\0';

				/* Workaround for lack of support for kCFXMLParserReplacePhysicalEntities option *** */
				SUBSTITUTE_PHYSICAL_ENTITY(node, text_ptr, size);

				return_val = (void *)text_ptr;
			}
			break;

		default:
			break;

	}	/* end switch */

	return (return_val);
}

/*****************************************************************************/

static void *parser_lock_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context)
{
	#pragma unused(parser)
	void *return_val = (void *)WEBDAV_LOCK_CONTINUE;
	char *text_ptr;
	size_t text_size, string_size;
	CFRange comparison_range;
	CFStringRef nodeString = CFXMLNodeGetString(node);
	webdav_parse_lock_struct_t *lock_struct = (webdav_parse_lock_struct_t *)context;

	/* See if this is a type we are interested in  If it is, we'll return
	  the appropriate constant */

	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			comparison_range = CFStringFind(nodeString, CFSTR(":"), 0);
			comparison_range.location++;
			comparison_range.length = CFStringGetLength(nodeString) - comparison_range.location;

			if (((CFStringCompareWithOptions(nodeString, CFSTR("locktoken"), comparison_range,
				kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				lock_struct->context = WEBDAV_LOCK_TOKEN;
			}
			else
			{
				if (((CFStringCompareWithOptions(nodeString, CFSTR("href"), comparison_range,
					kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
				{
					if (lock_struct->context == WEBDAV_LOCK_TOKEN)
					{
						lock_struct->context = WEBDAV_LOCK_HREF;
					}
					else
					{
						lock_struct->context = 0;
					}
				}
			}	/* end if-else locktoken*/
			break;

		case kCFXMLNodeTypeEntityReference:
		case kCFXMLNodeTypeText:
		case kCFXMLNodeTypeCDATASection:

			if (lock_struct->context == WEBDAV_LOCK_HREF)
			{
				lock_struct->context = 0; /* clear the context so the next time through it's not in WEBDAV_LOCK_HREF */
				
				/* Since the context is set to WEBDAV_LOCK_HREF, we have
				 * found the token and we have found the href indicating
				 * that the locktoken is coming so squirrel it away.
				 */
				text_size = CFStringGetLength(nodeString);
				text_ptr = malloc(text_size + 1);
				/* Get the bytes out in UTF8 form */

				if ((size_t)CFStringGetBytes(nodeString, CFRangeMake(0, text_size), kCFStringEncodingUTF8,
					/*stop on loss */ 0,/*not ext*/  0, text_ptr, WEBDAV_MAX_STAT_SIZE - 1,
					&string_size) != text_size)
				{
					/* we could not get the name in the buffer so return
					  nothing */
					syslog(LOG_ERR, "parser_lock_create: name too long");
					free(text_ptr);
					return_val = NULL;
				}
				else
				{
					/* null terminate the string */
					text_ptr[string_size] = '\0';

					/* Workaround for lack of support for kCFXMLParserReplacePhysicalEntities option *** */
					SUBSTITUTE_PHYSICAL_ENTITY(node, text_ptr, string_size);

					lock_struct->locktoken = text_ptr;

					/* Since we found what we are looking for, return NULL
					 * to stop the parse.
					 */

					return_val = NULL;
				}
			}

		default:
			break;

	}											/* end switch */

	return (return_val);
}

/*****************************************************************************/

static void *parser_getlastmodified_create(CFXMLParserRef parser, CFXMLNodeRef node, void *context)
{
	#pragma unused(parser, context)
	void *return_val = (void *)WEBDAV_GETLASTMODIFIED_IGNORE;
	char *text_ptr;
	size_t text_size;
	size_t size;
	CFRange comparison_range;
	CFStringRef nodeString = CFXMLNodeGetString(node);
	
	/* See if this is a type we are interested in  If it is, we'll return
	  the appropriate constant */
	switch (CFXMLNodeGetTypeCode(node))
	{
		case kCFXMLNodeTypeElement:
			comparison_range = CFStringFind(nodeString, CFSTR(":"), 0);
			comparison_range.location++;
			comparison_range.length = CFStringGetLength(nodeString) - comparison_range.location;

			if (((CFStringCompareWithOptions(nodeString, CFSTR("getlastmodified"),
				comparison_range, kCFCompareCaseInsensitive)) == kCFCompareEqualTo))
			{
				return_val = (void *)WEBDAV_GETLASTMODIFIED_MODDATE;
			}
			break;

		case kCFXMLNodeTypeEntityReference:
		case kCFXMLNodeTypeText:
		case kCFXMLNodeTypeCDATASection:
			text_size = CFStringGetLength(nodeString);
			text_ptr = malloc(text_size + 1);
			
			/* Get the bytes out in UTF8 form */
			if ((size_t)CFStringGetBytes(nodeString, CFRangeMake(0, text_size),
				kCFStringEncodingUTF8, /*stop on loss */ 0,/*not ext*/  0, text_ptr,
				text_size, &size) != text_size)
			{
				/* we could not get the name in the buffer (possibly because the server spewed
				 * crap xpl at us) so return nothing
				 */
				syslog(LOG_ERR, "parser_getlastmodified_create: name too long or malformed");
				free(text_ptr);
				return_val = NULL;
			}
			else
			{
				/* null terminate the string */
				text_ptr[size] = '\0';

				/* Workaround for lack of support for kCFXMLParserReplacePhysicalEntities option *** */
				SUBSTITUTE_PHYSICAL_ENTITY(node, text_ptr, size);

				return_val = (void *)text_ptr;
			}

		default:
			break;

	}	/* end switch */

	return (return_val);
}

/*****************************************************************************/

static void parser_add(CFXMLParserRef parser, void *parent, void *child, void *context)
{
	#pragma unused(parser, parent, child, context)
	/* Add nothing and do nothing.	We are not actually creating the tree */
	return;
}

/*****************************************************************************/

static void parser_opendir_add(CFXMLParserRef parser, void *parent, void *child, void *context)
{
	#pragma unused(parser)
	webdav_parse_opendir_element_t * element_ptr;
	webdav_parse_opendir_return_t * parent_ptr = (webdav_parse_opendir_return_t *)parent;
	webdav_parse_opendir_return_t * child_ptr = (webdav_parse_opendir_return_t *)child;
	webdav_parse_opendir_text_t * text_ptr;
	webdav_parse_opendir_struct_t * struct_ptr = (webdav_parse_opendir_struct_t *)context;
	char *ep;

	/* If the parent is one of our returned directory elements, and if this is a
	 * text element, than copy the text into the name buffer provided we have room */

	if (child_ptr->id == WEBDAV_OPENDIR_TEXT)
	{
		text_ptr = (webdav_parse_opendir_text_t *)child_ptr->data_ptr;

		switch (parent_ptr->id)
		{
			case WEBDAV_OPENDIR_ELEMENT:
				element_ptr = (webdav_parse_opendir_element_t *)parent_ptr->data_ptr;

				/* make sure the complete name will fit in the structure */
				if ((element_ptr->dir_data.d_name_URI_length + text_ptr->size) <=
					(sizeof(element_ptr->dir_data.d_name) - 1))
				{
					bcopy(text_ptr->name,
						&element_ptr->dir_data.d_name[element_ptr->dir_data.d_name_URI_length],
						text_ptr->size);
					element_ptr->dir_data.d_name_URI_length += text_ptr->size;
				}
				else
				{
					syslog(LOG_ERR, "parser_opendir_add: URI too long");
					struct_ptr->error = ENAMETOOLONG;
				}
				break;

			case WEBDAV_OPENDIR_ELEMENT_LENGTH:
				element_ptr = (webdav_parse_opendir_element_t *)parent_ptr->data_ptr;
				element_ptr->statsize = strtoq(text_ptr->name, &ep, 10);
				break;

			case WEBDAV_OPENDIR_ELEMENT_MODDATE:
				element_ptr = (webdav_parse_opendir_element_t *)parent_ptr->data_ptr;
				element_ptr->stattime.tv_sec = parse_http_date(text_ptr->name);
				element_ptr->stattime.tv_nsec = 0;

				if (element_ptr->stattime.tv_sec == -1)
				{
					element_ptr->stattime.tv_sec = 0;
				}
				break;

			case WEBDAV_OPENDIR_APPLEDOUBLEHEADER:
				{
					size_t	len = APPLEDOUBLEHEADER_LENGTH;

					element_ptr = (webdav_parse_opendir_element_t *)parent_ptr->data_ptr;
					from_base64(text_ptr->name, element_ptr->appledoubleheader, &len);
					if (len == APPLEDOUBLEHEADER_LENGTH)
					{
						element_ptr->appledoubleheadervalid = TRUE;
					}
				}
				break;

			default:
				break;
		}	/* end of switch statement */
		/* The text pointer is not an element that persists in the context and is
		 * thus not freed by parse_opendir like element pointers are.  Since we
		 * allocated it in the create routine, we must free it here */
#ifdef DEBUG_PARSE
		fprintf(stderr, "parser_opendir_add: free text_ptr %d\n", (int)text_ptr);
#endif

		free(text_ptr);
	}	/* end of if it is our text element */
}

/*****************************************************************************/

static void parser_stat_add(CFXMLParserRef parser, void *parent, void *child, void *context)
{
	#pragma unused(parser)
	char *text_ptr = (char *)child;
	struct stat *statbuf = (struct stat *)context;
	char *ep;

	/* If the parent reflects one of our properties than the child must
	  be the text pointer with the data we want */

	switch ((int)parent)
	{
		case WEBDAV_STAT_LENGTH:
			/* the text pointer is the lenth in bytes so fill that
			  in in the stat buffer */
			if (text_ptr && (text_ptr != (char *)WEBDAV_STAT_IGNORE))
			{
				statbuf->st_size = strtoq(text_ptr, &ep, 10);
				free(text_ptr);
			}
			else
			{
				/* If we got a string set to null we could not fit the value
				  in our buffer so the length is longer than our max. Set
				  the value to -1 then.
				*/

				statbuf->st_size = -1LL;
			}
			break;

		case WEBDAV_STAT_MODDATE:
			/* the text pointer is the date so translate it and
			  get it into the stat buffer */

			if (text_ptr && (text_ptr != (char *)WEBDAV_STAT_IGNORE))
			{
				statbuf->st_mtimespec.tv_sec = parse_http_date(text_ptr);
				if (statbuf->st_mtimespec.tv_sec == -1)
				{
					statbuf->st_mtimespec.tv_sec = 0;
				}
				statbuf->st_mtimespec.tv_nsec = 0;
				free(text_ptr);
			}
			else
			{
				/* If we got a string set to null we could not fit the value
				  in our buffer leave it blank and the kernel will fill in
				  to the best of its ability */
			}
			break;

		default:
			/* if it is ignore or something else, check the child.	If
			  it is not a return value and isn't null, it's a text ptr
			  so free it */

			if (text_ptr &&
				text_ptr != (char *)WEBDAV_STAT_IGNORE &&
				text_ptr != (char *)WEBDAV_STAT_LENGTH &&
				text_ptr != (char *)WEBDAV_STAT_MODDATE)
			{
				free(text_ptr);
			}
			break;
	}

	return;
}

/*****************************************************************************/

static void parser_statfs_add(CFXMLParserRef parser, void *parent, void *child, void *context)
{
	#pragma unused(parser)
	char *text_ptr = (char *)child;
	struct statfs	  *statfsbuf = (struct statfs *)context;
	char *ep;

	/* If the parent reflects one of our properties than the child must
	  be the text pointer with the data we want */

	switch ((int)parent)
	{
		case WEBDAV_STATFS_QUOTA:
			/* the text pointer is the lenth in blocks so fill that
			  in in the stat buffer */
			if (text_ptr && (text_ptr != (char *)WEBDAV_STATFS_IGNORE))
			{
				statfsbuf->f_blocks = strtoq(text_ptr, &ep, 10);
				free(text_ptr);
			}
			else
			{
				/* If we got a string set to null we couldnot fit the value
				  in our buffer so the length is longer than our max.	Set
				  the value to 0 then.
				*/

				statfsbuf->f_blocks = 0;
			}
			break;

		case WEBDAV_STATFS_QUOTAUSED:
			/* the text pointer is the data so translate it and
			  get it into the stat buffer */

			if (text_ptr && (text_ptr != (char *)WEBDAV_STATFS_IGNORE))
			{
				statfsbuf->f_bavail = statfsbuf->f_bfree = strtoq(text_ptr, &ep, 10);
				free(text_ptr);
			}
			else
			{
				statfsbuf->f_bavail = statfsbuf->f_bfree = 0;
				/* If we got a string set to null we could not fit the value
				  in our buffer leave it blank and the kernel will fill in
				  to the best of its ability */
			}
			break;

		default:
			/* if it is ignore or something else, check the child.	If
			  it is not a return value and isn't null, it's a text ptr
			  so free it */

			if (text_ptr &&
				text_ptr != (char *)WEBDAV_STATFS_IGNORE &&
				text_ptr != (char *)WEBDAV_STAT_LENGTH && 
				text_ptr != (char *)WEBDAV_STAT_MODDATE)
			{
				free(text_ptr);
			}
			break;
	}

	return;
}

/*****************************************************************************/

static void parser_getlastmodified_add(CFXMLParserRef parser, void *parent, void *child, void *context)
{
	#pragma unused(parser)
	char *text_ptr = (char *)child;
	time_t *last_modified = (time_t *)context;

	/* If the parent reflects one of our properties than the child must
	  be the text pointer with the data we want */
	switch ((int)parent)
	{
		case WEBDAV_GETLASTMODIFIED_MODDATE:
			/* the text pointer is the date so translate it and
			  get it into the stat buffer */
			if (text_ptr && (text_ptr != (char *)WEBDAV_GETLASTMODIFIED_IGNORE))
			{
				*last_modified = parse_http_date(text_ptr);
				free(text_ptr);
			}
			else
			{
				/* If we got a string set to null we could not fit the value
				  in our buffer leave it blank and the kernel will fill in
				  to the best of its ability */
			}
			break;

		default:
			/* if it is ignore or something else, check the child.	If
			  it is not a return value and isn't null, it's a text ptr
			  so free it */
			if (text_ptr &&
				text_ptr != (char *)WEBDAV_GETLASTMODIFIED_IGNORE &&
				text_ptr != (char *)WEBDAV_GETLASTMODIFIED_MODDATE)
			{
				free(text_ptr);
			}
			break;
	}

	return;
}

/*****************************************************************************/

static void parser_end(CFXMLParserRef parser, void *xml_type, void *context)
{
	#pragma unused(parser, xml_type, context)
	return;										/* deallocate the tree ? */
}

/*****************************************************************************/

static void parser_opendir_end(CFXMLParserRef parser, void *my_element, void *context)
{
	#pragma unused(parser, context)
	if (my_element)
	{
#ifdef DEBUG_PARSE
		fprintf(stderr, "parser_opendir_end: free return_ptr %d\n", (int)my_element);
#endif

		free(my_element);						/* free up the space we reserved for the return_ptr */
	}
	return;
}

/*****************************************************************************/

static CFDataRef parser_resolve(CFXMLParserRef parser, CFXMLExternalID *extID, void *context)
{
	#pragma unused(parser, extID, context)
	return (NULL);
}

/*****************************************************************************/

int parse_opendir(char *xmlp, int xmlp_len, int fd, char *dir_ref, char *hostname, uid_t uid)
{
	CFDataRef xml_dataref;
	CFXMLParserCallBacks callbacks =
	{
		0, parser_opendir_create, parser_opendir_add, parser_opendir_end, parser_resolve, NULL
	};
	webdav_parse_opendir_struct_t opendir_struct;
	webdav_parse_opendir_element_t * element_ptr,  *prev_element_ptr;
	CFXMLParserOptions options = kCFXMLParserNoOptions;
	/* *** if supported, could use kCFXMLParserReplacePhysicalEntities *** */
	CFXMLParserContext context =
	{
		0,	&opendir_struct, NULL, NULL, NULL
	};
	CFXMLParserRef parser;
	CFURLRef fake_url;
	int error = 0;
	int ignore_error;
	ssize_t size;
	char *name_ptr;
	char *decoded_dir_ref;
	char *after_dir_ref_hostname = NULL;		/* used to point to part of dir_ref which is after the hostname */
	char *after_hostname = NULL;				/* used to point to part of entry name after the hoate name */
	char *cache_uri;
	char *temp_uri;
	u_int32_t last_char;
	int name_len = 0;
	struct dirent dir_data[2];
	struct stat statbuf;

	opendir_struct.head = opendir_struct.tail = NULL;
	opendir_struct.error = 0;
	bzero(&statbuf, sizeof(struct stat));

	/* Start by getting the data into a form Core Foundation can
	  understand
	*/

	xml_dataref = CFDataCreateWithBytesNoCopy(kCFAllocatorSystemDefault, xmlp,
		(CFIndex)xmlp_len, kCFAllocatorNull);

	/* Fake up a URL ref since we don't have one */

	fake_url = CFURLCreateWithString(NULL, CFSTR("fakeurl"), NULL);

	/* Create the Parser, set it's call backs and away we go */
	parser = CFXMLParserCreate(kCFAllocatorSystemDefault, xml_dataref, fake_url, options,
		kCFXMLNodeCurrentVersion, &callbacks, &context);
	CFXMLParserParse(parser);
	CFRelease(parser);
	CFRelease(xml_dataref);
	CFRelease(fake_url);

	/* figure out what our actual name is */

	/* If in proxy mode, the "http://" and the destination hostname need to be skipped. */

	if (strncmp(dir_ref, _WEBDAVPREFIX, strlen(_WEBDAVPREFIX)) == 0)
	{
		decoded_dir_ref = percent_decode(dir_ref);
		if (!decoded_dir_ref)
		{
			return ENOMEM;
		}
	}
	else
	{
		/* dir_ref did not contain the prefix which meaans it is a partial uri 
		 * and won't contain the host either, but we need the host for the inode
		 * cache, so we will build one
		 */

		after_dir_ref_hostname = dir_ref;

		error = reconstruct_url(hostname, dir_ref, &temp_uri);
		if (error)
		{
			return ENOMEM;
		}

		decoded_dir_ref = percent_decode(temp_uri);
		if (!decoded_dir_ref)
		{
			free(temp_uri);
			return ENOMEM;
		}
		else
		{
			free(temp_uri);
		}
	}


	/* Either way, decoded_dir_ref contains a full uri, utf8_decoded
	 * now lets get past the prefix for the uri we will put in the inode
	 * cache */


	cache_uri = &decoded_dir_ref[strlen(_WEBDAVPREFIX)];
	after_dir_ref_hostname = &cache_uri[strlen(_WEBDAVPREFIX)];
	after_dir_ref_hostname = strchr(after_dir_ref_hostname, '/');


	/* Now we'll write each element into the local file
	  which is serving as our directory starting with . and ..*/

	bzero(dir_data, sizeof(dir_data));

	dir_data[0].d_namlen = 1;
	dir_data[0].d_reclen = sizeof(struct dirent);
	dir_data[0].d_name[0] = '.';

	/* Now get our own inode.  If we are the root we will get back
	 * WEBDAV_ROOTFILEID becuase the table was preinitialized with that
	 */
	{
		object_ref  obj_ref;
		error = webdav_get_inode(cache_uri, strlen(cache_uri), TRUE, &dir_data[0].d_fileno, &obj_ref);
	}
	if (error)
	{
		goto free_decoded_dir_ref;
	}

	dir_data[0].d_type = DT_DIR;

	dir_data[1].d_namlen = 2;
	dir_data[1].d_reclen = sizeof(struct dirent);
	dir_data[1].d_name[0] = '.';
	dir_data[1].d_name[1] = '.';
	if (dir_data[0].d_fileno == WEBDAV_ROOTFILEID)
	{
		dir_data[1].d_fileno = 2;				/* parent of the root always 2 */
	}
	else
	{
		/* Ok, just lop of the last part of the name and calculate
		 * the fileno.	Unlike the above case, we have to special case the
		 * root.  Why ?	 becuase the root is cached with the trailing
		 * slash.  That's how the kernel keeps it. If the root is what we
		 * are enumerating we will have done it right but genearlly we don't
		 * include trailing slashes with directories so if we ae enumerating
		 * one level below the root we have to special case.
		 * To do that we'll do some fancy pointer math using afterhostname.
		 * If we are enumerating one level below the root the url will be
		 * of the form http://host/dir and afterhost name will point to d.
		 * thus strrchar of afterhostname will come back null and voila, we have
		 * our result.
		 */

		if (after_dir_ref_hostname && strchr(after_dir_ref_hostname, '/'))
		{
			{
				object_ref  obj_ref;
				error = webdav_get_inode(cache_uri, (unsigned int)(strrchr(cache_uri, '/') - cache_uri),
					TRUE, &dir_data[1].d_fileno, &obj_ref);
			}
			if (error)
			{
				goto free_decoded_dir_ref;
			}
		}
		else
		{
			dir_data[1].d_fileno = WEBDAV_ROOTFILEID;
		}

	}
	dir_data[1].d_type = DT_DIR;

	size = write(fd, (void *)dir_data, (sizeof(struct dirent)) * 2);

	/* If for some reason we did not get all the data into
	  the file, report the error, but keep going to free
	  all the records */

	if (size != (sizeof(struct dirent)) * 2)
	{
		if (size == -1)
		{
			syslog(LOG_ERR, "parser_opendir: write() . & ..: %s", strerror(errno));
			error = errno;
		}
		else
		{
			syslog(LOG_ERR, "parser_opendir: write() . & .. was short");
			error = EIO;
		}
	}

	element_ptr = opendir_struct.head;
	if (!element_ptr && !opendir_struct.error)
	{
		/* It's ok for a directory to be empty, just return as
		 * long as we didn't take a parse error */

		error = 0;
		goto free_decoded_dir_ref;
	}
	else
	{
		while (element_ptr)
		{
			/* Fix up the element by truncating to just the name part of the URL
			 * First we'll add the null byte. Note that we purposely made sure the
			 * name buffer one character longer than the maximum length name we copy
			 * into it so that we could always hold the null byte
			 */
			element_ptr->dir_data.d_name[element_ptr->dir_data.d_name_URI_length] = '\0';

			/* Now get rid of trailing slashes if we have any */

			if (element_ptr->dir_data.d_name[element_ptr->dir_data.d_name_URI_length - 1] == '/')
			{
				--element_ptr->dir_data.d_name_URI_length;
				element_ptr->dir_data.d_name[element_ptr->dir_data.d_name_URI_length] = '\0';
			}

			/* decode the whole string and readjust the name length appopriately, also
			  set last_char since we will be using it again and it needs to be right.*/

			percent_decode_in_place(element_ptr->dir_data.d_name);
			element_ptr->dir_data.d_name_URI_length = strlen(element_ptr->dir_data.d_name);
			last_char = element_ptr->dir_data.d_name_URI_length;

			/* now figure out the post hostname part and compare it with our dirref to
			 * see if we are looking at the directory.	The xml we get back from the server 
			 * includes the info on the parent directory as well as all of the children */

			after_hostname = element_ptr->dir_data.d_name;
			if (!(strncmp(after_hostname, _WEBDAVPREFIX, strlen(_WEBDAVPREFIX))))
			{
				/* Get past the http: prefix */
				after_hostname = &(after_hostname[strlen(_WEBDAVPREFIX)]);
				/* Since this was a full uri, get past the hostname */
				after_hostname = strchr(after_hostname, '/');
			}

			/* Ok, we now have the relative uri percent decoded in afterhostname and the
			 * percent decoded directory reference in after_dir_ref_hostname.  We can now
			 * compare these uri's and make sure that we are not about to put in the
			 * directory before proceeding.	 There are a number of possibilities for 
			 * the match and they are described in the following if statement */

			if ((!after_hostname) ||			/* This entry is for the root */
				(after_dir_ref_hostname && (!(strncmp(after_hostname, after_dir_ref_hostname,
				strlen(after_hostname))))))
			{
				/* We don't want to include this one in the name cache or the directory
				 * so do nothing
				 */
			}
			else
			{
				/* Since we have gotten rid of the trailing slash, and renormalized the name
				 * to utf8 (that is to say got rid of the % escapted characters) we now have a name
				 * suitable for entry in the stat cache, so let's do that now before
				 * proceeding.	Putting this stuff in the stat cache will make the stat
				 * that is highly likely to follow this opendir happen quickly.	 
				 */

				/* the first thing to do is fill in the fields we cannot get from the server. */
				statbuf.st_dev = 0;
				/* Why 1 for st_nlink?
				 * Getting the real link count for directories is expensive.
				 * Setting it to 1 lets FTS(3) (and other utilities that assume
				 * 1 means a file system doesn't support link counts) work.
				 */
				statbuf.st_nlink = 1;
				statbuf.st_uid = UNKNOWNUID;
				statbuf.st_gid = UNKNOWNUID;
				statbuf.st_rdev = 0;
				statbuf.st_blksize = WEBDAV_IOSIZE;
				statbuf.st_flags = 0;
				statbuf.st_gen = 0;
				
				/* set all times to the last modified time since we cannot get the other times */
				statbuf.st_atimespec = statbuf.st_mtimespec = statbuf.st_ctimespec =
					element_ptr->stattime;
					
				if (element_ptr->dir_data.d_type == DT_DIR)
				{
					statbuf.st_mode = S_IFDIR | ACCESSPERMS;
					statbuf.st_size = WEBDAV_DIR_SIZE;
					/* appledoubleheadervalid is never valid for directories */
					element_ptr->appledoubleheadervalid = FALSE;
				}
				else
				{
					statbuf.st_mode = S_IFREG | ACCESSPERMS;
					statbuf.st_size = element_ptr->statsize;
					/* appledoubleheadervalid is valid for files only if the server
					 * returned the appledoubleheader property and file size is
					 * the size of the appledoubleheader (APPLEDOUBLEHEADER_LENGTH bytes).
					 */
					element_ptr->appledoubleheadervalid =
						(element_ptr->appledoubleheadervalid && (element_ptr->statsize == APPLEDOUBLEHEADER_LENGTH));
				}

				/* calculate number of S_BLKSIZE blocks */
				statbuf.st_blocks = ((statbuf.st_size + S_BLKSIZE - 1) / S_BLKSIZE);

				/* Find the last '/', and make sure the string after that is not
				  too long to fit into the d_name field in the dirent structure */
				name_ptr = strrchr(element_ptr->dir_data.d_name, '/');
				if (name_ptr)
				{
					name_len = (int)(((int)element_ptr->dir_data.d_name + last_char) -
						(((int)name_ptr) + 1));
					if (name_len > MAXNAMLEN)
					{
						syslog(LOG_ERR, "parser_opendir: URI too long");
						error = ENAMETOOLONG;
						goto free_decoded_dir_ref;
					}
				}

				/* Check to see if we have a relative uri. If we do, make an absolute uri
				 * since we always cache complete url's minus the "http://"
				 */
				{
					char *full_url = NULL;
					object_ref  obj_ref;
					
					if (element_ptr->dir_data.d_name_URI_length && element_ptr->dir_data.d_name[0] == '/')
					{
						/* '/' as the first character indicates that this is a partial uri */

						/* If in proxy mode, reconstructing from the hostname won't work,
						  however the complete uri is in dir_ref. */
						if (strncmp(dir_ref, _WEBDAVPREFIX, strlen(_WEBDAVPREFIX)) == 0)
						{
							size_t tmp_name_len;
							
							tmp_name_len = (strlen(dir_ref) + (name_ptr ? strlen(name_ptr) : 0));
							full_url = malloc(tmp_name_len);
							if (full_url)
							{
								strncpy(full_url, &dir_ref[strlen(_WEBDAVPREFIX)], tmp_name_len);
								if (name_ptr)
								{
									full_url = strncat(full_url, name_ptr, tmp_name_len);
								}
							}
							else
							{
								syslog(LOG_ERR, "parser_opendir: full_url could not be allocated");
								error = ENOMEM;
								goto free_decoded_dir_ref;
							}

							/* Now we have the uri, generate the inode number and put it in
							 * the stat structure */
							cache_uri = full_url;
						}
						else
						{
							error = reconstruct_url(hostname, element_ptr->dir_data.d_name,
								&full_url);
							if (!error)
							{
								/* Now we have the uri, generate the inode number and put it in
								 * the stat structure */
								cache_uri = &full_url[strlen(_WEBDAVPREFIX)];
							}
							else
							{
								goto free_decoded_dir_ref;
							}
						}
					}
					else
					{
						/* This is a full uri so just enter it directly */
						cache_uri = &element_ptr->dir_data.d_name[strlen(_WEBDAVPREFIX)];
					}
					
					/* this fills in statbuf.st_ino */
					error = webdav_get_inode(cache_uri, strlen(cache_uri), TRUE, &statbuf.st_ino, &obj_ref);
					if (error)
					{
						if ( full_url != NULL )
						{
							free(full_url);
						}
						goto free_decoded_dir_ref;
					}
					
					/* Now cache the stat structure */
					ignore_error = webdav_memcache_insert(uid, cache_uri, &gmemcache_header, &statbuf,
						element_ptr->appledoubleheadervalid ? element_ptr->appledoubleheader: NULL);
					
					if ( full_url != NULL )
					{
						free(full_url);
					}
				}

				/* Ok now we'll complete the task of getting the regular name into the dirent */

				/* Complete the task of getting the regular name into the dirent */

				/* if there's a '/', fix up element_ptr->dir_data.d_name so that it
				  contains the name that starts right after the last '/'. */
				if (name_ptr)
				{
					bcopy(name_ptr + 1, element_ptr->dir_data.d_name, (size_t)name_len);
					element_ptr->dir_data.d_name[name_len] = '\0';
					element_ptr->dir_data.d_namlen = name_len;
					element_ptr->dir_data.d_fileno = statbuf.st_ino;
				}

				size = write(fd, (void *) & element_ptr->dir_data, element_ptr->dir_data.d_reclen);

				/* If for some reason we did not get all the data into
				  the file, report the error, but keep going to free
				  all the records */

				if (size != element_ptr->dir_data.d_reclen)
				{
					if (size == -1)
					{
						syslog(LOG_ERR, "parser_opendir: write(): %s", strerror(errno));
						error = errno;
					}
					else
					{
						syslog(LOG_ERR, "parser_opendir: write() was short");
						error = EIO;
					}
				}
			}									/* end if not ourselves */

			prev_element_ptr = element_ptr;
			element_ptr = element_ptr->next;
#ifdef DEBUG_PARSE
			fprintf(stderr, "parse_opendir: freeing previous element ptr %d\n", (int)prev_element_ptr);
#endif

			free(prev_element_ptr);
		}										/* while */
	}
	/*	Now that we've taken care of freeing all the elements, check for a
	 *	parser error.  If we have one, it will overide any error found while
	 *	writing the file, since it will have happened first. */

	if (opendir_struct.error)
	{
		error = opendir_struct.error;
	}

free_decoded_dir_ref:

	free(decoded_dir_ref);

	return (error);
}

/*****************************************************************************/

int parse_file_count(char *xmlp, int xmlp_len, int *file_count)
{
	CFDataRef xml_dataref;
	CFXMLParserCallBacks callbacks =
	{
		0, parser_file_count_create, parser_add, parser_end, parser_resolve, NULL
	};
	CFXMLParserOptions options = kCFXMLParserNoOptions;
	/* *** if supported, could use kCFXMLParserReplacePhysicalEntities *** */
	CFXMLParserContext context =
	{
		0, file_count, NULL, NULL, NULL
	};
	CFXMLParserRef parser;
	CFURLRef fake_url;
	int error = 0;
	*file_count = 0;

	/* Start by getting the data into a form Core Foundation can
	  understand
	*/

	xml_dataref = CFDataCreateWithBytesNoCopy(kCFAllocatorSystemDefault, xmlp,
		(CFIndex)xmlp_len, kCFAllocatorNull);

	/* Fake up a URL ref since we don't have one */

	fake_url = CFURLCreateWithString(NULL, CFSTR("fakeurl"), NULL);

	/* Create the Parser, set it's call backs and away we go */
	parser = CFXMLParserCreate(kCFAllocatorSystemDefault, xml_dataref, fake_url, options, 
		kCFXMLNodeCurrentVersion, &callbacks, &context);
	CFXMLParserParse(parser);
	CFRelease(parser);
	CFRelease(xml_dataref);
	CFRelease(fake_url);

	return (error);
}

/*****************************************************************************/

int parse_stat(char *xmlp, int xmlp_len, const char *orig_uri, struct stat *statbuf, uid_t uid, int add_cache)
{
	CFDataRef xml_dataref;
	CFXMLParserCallBacks callbacks =
	{
		0, parser_stat_create, parser_stat_add, parser_end, parser_resolve, NULL
	};
	CFXMLParserContext context =
	{
		0, statbuf, NULL, NULL, NULL
	};
	CFXMLParserOptions options = kCFXMLParserNoOptions;
	/* *** if supported, could use kCFXMLParserReplacePhysicalEntities *** */
	CFXMLParserRef parser;
	CFURLRef fake_url;
	int error = 0;
	int ignore_error;

	/* Start by getting the data into a form Core Foundation can
	  understand
	*/

	xml_dataref = CFDataCreateWithBytesNoCopy(kCFAllocatorSystemDefault, xmlp, 
		(CFIndex)xmlp_len, kCFAllocatorNull);

	/* Fake up a URL ref since we don't have one */

	fake_url = CFURLCreateWithString(NULL, CFSTR("fakeurl"), NULL);

	/* Create the Parser, set it's call backs and away we go */
	bzero((void *)statbuf, sizeof(struct stat));
	parser = CFXMLParserCreate(kCFAllocatorSystemDefault, xml_dataref, fake_url, options, 
		kCFXMLNodeCurrentVersion, &callbacks, &context);
	CFXMLParserParse(parser);
	CFRelease(parser);
	CFRelease(xml_dataref);
	CFRelease(fake_url);

	/* Coming back from the parser:
	 *   statbuf->st_mode will be 0 or will have S_IFDIR set if the object is a directory.
	 *   statbuf->st_mtimespec will be 0 or will have the last modified time.
	 *   statbuf->st_size will be set to the resource size if the object is a file.
	 *
	 * So, the first thing to do is fill in the fields we cannot get from the server.
	 */
	
	statbuf->st_dev = 0;
	/* Why 1 for st_nlink?
	 * Getting the real link count for directories is expensive.
	 * Setting it to 1 lets FTS(3) (and other utilities that assume
	 * 1 means a file system doesn't support link counts) work.
	 */
	statbuf->st_nlink = 1;
	statbuf->st_uid = UNKNOWNUID;
	statbuf->st_gid = UNKNOWNUID;
	statbuf->st_rdev = 0;
	statbuf->st_blksize = WEBDAV_IOSIZE;
	statbuf->st_flags = 0;
	statbuf->st_gen = 0;
	
	/* set all times to the last modified time since we cannot get the other times */
	statbuf->st_atimespec = statbuf->st_ctimespec = statbuf->st_mtimespec;
	
	/* was this a directory? */
	if ( S_ISDIR(statbuf->st_mode) )
	{
		/* yes - add the directory access permissions */
		statbuf->st_mode |= ACCESSPERMS;
		/* fake up the directory size */
		statbuf->st_size = WEBDAV_DIR_SIZE;
	}
	else
	{
		/* no - mark it as a regular file and set the file access permissions
		 * (for now, everything is either a file or a directory)
		 */
		statbuf->st_mode = S_IFREG | ACCESSPERMS;
	}

	/* calculate number of S_BLKSIZE blocks */
	statbuf->st_blocks = ((statbuf->st_size + S_BLKSIZE - 1) / S_BLKSIZE);
	
	if ( add_cache )
	{
		/* Now put the URI into the inode hash.	 Orig URI is the non decoded (pure utf8)
		  URI which came from the kernel and was passed all the way from activate. */

		if (strncmp(orig_uri, _WEBDAVPREFIX, strlen(_WEBDAVPREFIX)) == 0)
		{
			orig_uri += strlen(_WEBDAVPREFIX);
		}

		{
			object_ref  obj_ref;
			/* this fills in statbuf->st_ino */
			error = webdav_get_inode(orig_uri, strlen(orig_uri), TRUE,
				&statbuf->st_ino, &obj_ref);
		}
		if (error)
		{
			return (error);
		}
		
		/* Now cache the stat structure */
		ignore_error = webdav_memcache_insert(uid, (const char *)orig_uri, &gmemcache_header,
								statbuf, NULL);
	}
	/* else this is getting called during mount when the caches are not initialized
	 * and statbuf->st_ino is not used.
	 */

	return (error);
}

/*****************************************************************************/

int parse_statfs(char *xmlp, int xmlp_len, struct statfs *statfsbuf)
{
	CFDataRef xml_dataref;
	CFXMLParserCallBacks callbacks =
	{
		0, parser_statfs_create, parser_statfs_add, parser_end, parser_resolve, NULL
	};
	CFXMLParserContext context =
	{
		0, statfsbuf, NULL, NULL, NULL
	};
	CFXMLParserOptions options = kCFXMLParserNoOptions;
	/* *** if supported, could use kCFXMLParserReplacePhysicalEntities *** */
	CFXMLParserRef parser;
	CFURLRef fake_url;
	int error = 0;

	/* Start by getting the data into a form Core Foundation can
	  understand
	*/

	xml_dataref = CFDataCreateWithBytesNoCopy(kCFAllocatorSystemDefault, xmlp,
		(CFIndex)xmlp_len, kCFAllocatorNull);

	/* Fake up a URL ref since we don't have one */

	fake_url = CFURLCreateWithString(NULL, CFSTR("fakeurl"), NULL);

	/* Create the Parser, set it's call backs and away we go */
	bzero((void *)statfsbuf, sizeof(struct statfs));
	parser = CFXMLParserCreate(kCFAllocatorSystemDefault, xml_dataref, fake_url, options, 
		kCFXMLNodeCurrentVersion, &callbacks, &context);
	CFXMLParserParse(parser);
	CFRelease(parser);
	CFRelease(xml_dataref);
	CFRelease(fake_url);

	/* Ok now turn quota-used, which we temporarily stored in f_bfree, into 
	  blocks-available, with a little subtraction */

	if (statfsbuf->f_blocks && (statfsbuf->f_blocks > statfsbuf->f_bfree))
	{
		statfsbuf->f_bavail = statfsbuf->f_bfree = (statfsbuf->f_blocks - statfsbuf->f_bfree);
	}
	else
	{
		statfsbuf->f_bavail = statfsbuf->f_bfree = 0;
	}

	return (error);
}

/*****************************************************************************/

int parse_lock(char *xmlp, int xmlp_len, char **locktoken)
{
	webdav_parse_lock_struct_t lock_struct;
	CFDataRef xml_dataref;
	CFXMLParserCallBacks callbacks =
	{
		0, parser_lock_create, parser_add, parser_end, parser_resolve, NULL
	};
	CFXMLParserOptions options = kCFXMLParserNoOptions;
	/* *** if supported, could use kCFXMLParserReplacePhysicalEntities *** */
	CFXMLParserContext context =
	{
		0, &lock_struct, NULL, NULL, NULL
	};
	CFXMLParserRef parser;
	CFURLRef fake_url;
	
	lock_struct.context = 0;
	lock_struct.locktoken = NULL;	/* NULL coming into this function */

	/* Start by getting the data into a form Core Foundation can
	  understand
	*/

	xml_dataref = CFDataCreateWithBytesNoCopy(kCFAllocatorSystemDefault, xmlp,
		(CFIndex)xmlp_len, kCFAllocatorNull);

	/* Fake up a URL ref since we don't have one */

	fake_url = CFURLCreateWithString(NULL, CFSTR("fakeurl"), NULL);

	/* Create the Parser, set it's call backs and away we go */
	parser = CFXMLParserCreate(kCFAllocatorSystemDefault, xml_dataref, fake_url, options, 
		kCFXMLNodeCurrentVersion, &callbacks, &context);
	CFXMLParserParse(parser);
	CFRelease(parser);
	CFRelease(xml_dataref);
	CFRelease(fake_url);

	*locktoken = lock_struct.locktoken;
	if (*locktoken == NULL)
	{
		syslog(LOG_ERR, "parse_lock: error parsing lock token");
	}

	return (0);
}

/*****************************************************************************/

int parse_getlastmodified(char *xmlp, int xmlp_len, time_t *last_modified)
{
	CFDataRef xml_dataref;
	CFXMLParserCallBacks callbacks =
	{
		0, parser_getlastmodified_create, parser_getlastmodified_add, parser_end, parser_resolve, NULL
	};
	CFXMLParserContext context =
	{
		0, last_modified, NULL, NULL, NULL
	};
	CFXMLParserOptions options = kCFXMLParserNoOptions;
	/* *** if supported, could use kCFXMLParserReplacePhysicalEntities *** */
	CFXMLParserRef parser;
	CFURLRef fake_url;
	
	/* Start by getting the data into a form Core Foundation can
	  understand
	*/
	xml_dataref = CFDataCreateWithBytesNoCopy(kCFAllocatorSystemDefault, xmlp, 
		(CFIndex)xmlp_len, kCFAllocatorNull);

	/* Fake up a URL ref since we don't have one */
	fake_url = CFURLCreateWithString(NULL, CFSTR("fakeurl"), NULL);

	/* Create the Parser, set it's call backs and away we go */
	parser = CFXMLParserCreate(kCFAllocatorSystemDefault, xml_dataref, fake_url, options, 
		kCFXMLNodeCurrentVersion, &callbacks, &context);
	CFXMLParserParse(parser);
	CFRelease(parser);
	CFRelease(xml_dataref);
	CFRelease(fake_url);
	
	return (0);
}

/*****************************************************************************/
