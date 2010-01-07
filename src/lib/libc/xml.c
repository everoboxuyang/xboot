/*
 * lib/libc/xml.c
 */

#include <configs.h>
#include <default.h>
#include <ctype.h>
#include <types.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>
#include <vsprintf.h>
#include <fs/fsapi.h>
#include <xml.h>

#define XML_ERROR_LENGTH		(256)		/* maximum error string length */
#define XML_WHITESPACE			"\t\r\n "	/* whitespace */

/*
 * additional data for the root tag
 */
struct xml_root {
	struct xml xml;							/* is a super-struct built on top of xml struct */
	struct xml * cur;						/* current xml tree insertion point */
	char * m;								/* original xml string */
	x_size len;								/* length of allocated memory for mmap, -1 for malloc */
	char * u;								/* UTF-8 conversion of string if original was UTF-16 */
	char * s;								/* start of work area */
	char * e;								/* end of work area */
	char ** ent;							/* general entities (ampersand sequences) */
	char *** attr;							/* default attributes */
	char *** pi;							/* processing instructions */
	x_s16 standalone;						/* non-zero if <?xml standalone="yes"?> */
	char err[XML_ERROR_LENGTH];				/* error string */
};

/*
 * empty, null terminated array of strings
 */
static char * xml_nil[] = { NULL };

/*
 * returns the first child tag with the given name or NULL if not found
 */
struct xml * xml_child(struct xml * xml, const char * name)
{
	xml = (xml) ? xml->child : NULL;

	while(xml && strcmp((const x_s8 *)name, (const x_s8 *)xml->name))
    	xml = xml->sibling;

	return xml;
}

/*
 * returns the Nth tag with the same name in the same subsection or NULL if not found
 */
struct xml * xml_idx(struct xml * xml, x_s32 idx)
{
	for(; xml && idx; idx--)
		xml = xml->next;

	return xml;
}

/*
 * returns the value of the requested tag attribute or NULL if not found
 */
const char * xml_attr(struct xml * xml, const char * attr)
{
	x_s32 i = 0, j = 1;
	struct xml_root * root = (struct xml_root *)xml;

	if(! xml || ! xml->attr)
		return NULL;

	while(xml->attr[i] && strcmp((const x_s8 *)attr, (const x_s8 *)xml->attr[i]))
		i += 2;

	if(xml->attr[i])
		return xml->attr[i + 1];								/* found attribute */

	while(root->xml.parent)
		root = (struct xml_root *)root->xml.parent;				/* root tag */

	for(i = 0; root->attr[i] && strcmp((const x_s8 *)xml->name, (const x_s8 *)root->attr[i][0]); i++);

	if(!root->attr[i])
		return NULL;											/* no matching default attributes */

	while(root->attr[i][j] && strcmp((const x_s8 *)attr, (const x_s8 *)root->attr[i][j]))
		j += 3;

	return (root->attr[i][j]) ? root->attr[i][j + 1] : NULL;	/* found default */
}

/*
 * same as xml_get but takes an already initialized va_list
 */
static struct xml * xml_vget(struct xml * xml, va_list ap)
{
	char * name = va_arg(ap, char *);
	x_s32 idx = -1;

	if(name && *name)
	{
		idx = va_arg(ap, int);
		xml = xml_child(xml, name);
    }

	return (idx < 0) ? xml : xml_vget(xml_idx(xml, idx), ap);
}

/*
 * traverses the xml tree to retrieve a specific subtag. takes a variable
 * length list of tag names and indexes. the argument list must be terminated
 * by either an index of -1 or an empty string tag name.
 *
 * example:
 * title = xml_get(library, "shelf", 0, "book", 2, "title", -1);
 * this retrieves the title of the 3rd book on the 1st shelf of library.
 * returns NULL if not found.
 */
struct xml * xml_get(struct xml * xml, ...)
{
	va_list ap;
	struct xml * r;

	va_start(ap, xml);
	r = xml_vget(xml, ap);
	va_end(ap);

	return r;
}

/*
 * returns a null terminated array of processing instructions for the given target
 */
const char ** xml_pi(struct xml * xml, const char * target)
{
	struct xml_root * root = (struct xml_root *)xml;
	x_s32 i = 0;

	if(!root)
		return (const char **)xml_nil;

	while(root->xml.parent)
		root = (struct xml_root *)root->xml.parent; 			/* root tag */

	while(root->pi[i] && strcmp((const x_s8 *)target, (const x_s8 *)root->pi[i][0]))
		i++;													/* find target */

	return (const char **)((root->pi[i]) ? root->pi[i] + 1 : xml_nil);
}

/*
 * set an error string and return root
 */
static struct xml * xml_err(struct xml_root * root, char * s, const char * err, ...)
{
	va_list ap;
	x_s32 line = 1;
	char *t, fmt[XML_ERROR_LENGTH];

	for(t = root->s; t < s; t++)
	{
		if(*t == '\n')
			line++;
	}
	snprintf((x_s8 *)fmt, XML_ERROR_LENGTH, (const x_s8 *)"[error near line %ld]: %s", line, err);

	va_start(ap, err);
	vsnprintf((x_s8 *)root->err, XML_ERROR_LENGTH, (const x_s8 *)fmt, ap);
	va_end(ap);

	return &root->xml;
}

/*
 * recursively decodes entity and character references and normalizes new lines
 * ent is a null terminated array of alternating entity names and values. set t
 * to '&' for general entity decoding, '%' for parameter entity decoding, 'c'
 * for cdata sections, ' ' for attribute normalization, or '*' for non-cdata
 * attribute normalization. Returns s, or if the decoded string is longer than
 * s, returns a malloced string that must be freed.
 */
static char * xml_decode(char * s, char ** ent, char t)
{
	char *e, *r = s, *m = s;
	x_s32 b, c, d, l;

	for(; *s; s++)												/* normalize line endings */
	{
		while(*s == '\r')
		{
			*(s++) = '\n';
			if(*s == '\n')
				memmove(s, (s + 1), strlen((const x_s8 *)s));
		}
	}

	for(s = r; ; )
	{
		while(*s && *s != '&' && (*s != '%' || t != '%') && !isspace(*s))
			s++;

		if(! *s)
			break;
		else if(t != 'c' && ! strncmp((const x_s8 *)s, (const x_s8 *)"&#", 2))				/* character reference */
        {
			if(s[2] == 'x')
				c = simple_strtos64((const x_s8 *)(s + 3), (x_s8 **)(&e), 16);
			else
				c = simple_strtos64((const x_s8 *)(s + 2), (x_s8 **)(&e), 10);
			if(! c || *e != ';')								/* not a character ref */
			{
				s++;
				continue;
			}

			if(c < 0x80)										/* US-ASCII subset */
				*(s++) = c;
            else												/* multi-byte UTF-8 sequence */
            {
				for(b = 0, d = c; d; d /= 2)					/* number of bits in c */
                	b++;
				b = (b - 2) / 5;								/* number of bytes in payload */
				*(s++) = (0xFF << (7 - b)) | (c >> (6 * b));	/* head */
				while(b) *(s++) = 0x80 | ((c >> (6 * --b)) & 0x3F);
            }

			memmove(s, strchr((const x_s8 *)s, ';') + 1, strlen(strchr((const x_s8 *)s, ';')));
        }
		else if((*s == '&' && (t == '&' || t == ' ' || t == '*')) || (*s == '%' && t == '%'))	/*entity reference */
        {
			for(b = 0; ent[b] && strncmp((x_s8 *)(s + 1), (const x_s8 *)ent[b], strlen((const x_s8 *)ent[b]));
				b += 2);										/* find entity in entity list */

			if(ent[b++])										/* found a match */
			{
				if(((c = strlen((const x_s8 *)ent[b])) - 1) > (e = (char *)strchr((const x_s8 *)s, ';')) - s)
				{
					l = (d = (s - r)) + c + strlen((const x_s8 *)e);			/* new length */
					r = (r == m) ? strcpy(malloc(l), (const x_s8 *)r) : realloc(r, l);
					e = (char *)strchr((const x_s8 *)(s = r + d), ';');			/* fix up pointers */
                }

				memmove(s + c, e + 1, strlen((const x_s8 *)e));					/* shift rest of string */
				strncpy((x_s8 *)s, (const x_s8 *)ent[b], c);					/* copy in replacement text */
            }
            else												/* not a known entity */
            	s++;
        }
		else if((t == ' ' || t == '*') && isspace(*s))
        	*(s++) = ' ';
        else													/* no decoding needed */
        	s++;
    }

	if(t == '*')												/* normalize spaces for non-cdata attributes */
	{
		for (s = r; *s; s++)
		{
			if((l = strspn((const x_s8 *)s, (const x_s8 *)" ")))
				memmove(s, s + l, strlen((const x_s8 *)(s + l)) + 1);
            while(*s && *s != ' ')
            	s++;
        }
        if(--s >= r && *s == ' ')
        	*s = '\0';											/* trim any trailing space */
	}
	return r;
}

/*
 * inserts an existing tag into an ezxml structure
 */
struct xml * xml_insert(struct xml * xml, struct xml * dest, x_size off)
{
	struct xml * cur, * prev, * head;

	xml->next = xml->sibling = xml->ordered = NULL;
	xml->off = off;
	xml->parent = dest;

	if((head = dest->child))									/* already have sub tags */
	{
		if(head->off <= off)									/* not first subtag */
		{
			for(cur = head; cur->ordered && cur->ordered->off <= off; cur = cur->ordered);
			xml->ordered = cur->ordered;
			cur->ordered = xml;
		}
		else													/* first subtag */
		{
			xml->ordered = head;
			dest->child = xml;
		}

		for(cur = head, prev = NULL; cur && strcmp((const x_s8 *)cur->name, (const x_s8 *)xml->name);
			 prev = cur, cur = cur->sibling);					/* find tag type */
		if (cur && cur->off <= off)
		{														/* not first of type */
			while (cur->next && cur->next->off <= off)
				cur = cur->next;
			xml->next = cur->next;
			cur->next = xml;
		}
		else													/* first tag of this type */
		{
			if(prev && cur)
				prev->sibling = cur->sibling;					/* remove old first */
			xml->next = cur;									/* old first tag is now next */
			for(cur = head, prev = NULL; cur && cur->off <= off;
				 prev = cur, cur = cur->sibling);				/* new sibling insert point */
			xml->sibling = cur;
			if(prev)
				prev->sibling = xml;
		}
	}
	else
		dest->child = xml; 										/* only sub tag */

	return xml;
}

/*
 * adds a child tag. off is the offset of the child tag relative to the start
 * of the parent tag's character content. returns the child tag.
 */
struct xml * xml_add_child(struct xml * xml, const char * name, x_size off)
{
	struct xml * child;

	if(! xml)
		return NULL;

	child = (struct xml *)memset(malloc(sizeof(struct xml)), '\0', sizeof(struct xml));
	child->name = (char *)name;
	child->attr = xml_nil;
	child->txt = "";

	return xml_insert(child, xml, off);
}

/*
 * called when parser finds start of new tag
 */
static void xml_open_tag(struct xml_root * root, char * name, char ** attr)
{
	struct xml * xml = root->cur;

	if(xml->name)
		xml = xml_add_child(xml, name, strlen((const x_s8 *)xml->txt));
	else														 /* first open tag */
		xml->name = name;

	xml->attr = attr;
	root->cur = xml;											/* update tag insertion point */
}

/*
 * sets a flag for the given tag and returns the tag
 */
struct xml * xml_set_flag(struct xml * xml, x_s16 flag)
{
	if(xml)
		xml->flags |= flag;

	return xml;
}

/*
 * sets the character content for the given tag and returns the tag
 */
struct xml * xml_set_txt(struct xml * xml, const char * txt)
{
	if(! xml)
		return NULL;

	if(xml->flags & XML_TXT_MALLOC)								/* existing txt was malloced */
		free(xml->txt);

	xml->flags &= ~XML_TXT_MALLOC;
	xml->txt = (char *)txt;

	return xml;
}

/*
 * sets the given tag attribute or adds a new attribute if not found. a value
 * of NULL will remove the specified attribute. Returns the tag given.
 */
struct xml * xml_set_attr(struct xml * xml, const char * name, const char * value)
{
	x_s32 l = 0, c;

	if(!xml)
		return NULL;

	while(xml->attr[l] && strcmp((const x_s8 *)xml->attr[l], (const x_s8 *)name))
		l += 2;

	if(!xml->attr[l])													/* not found, add as new attribute */
	{
		if(!value)
			return xml;

		if(xml->attr == xml_nil)										/* first attribute */
		{
			xml->attr = malloc(4 * sizeof(char *));
			xml->attr[1] = (char *)strdup((const x_s8 *)"");			/* empty list of malloced names,vals */
		}
		else
			xml->attr = realloc(xml->attr, (l + 4) * sizeof(char *));

		xml->attr[l] = (char *)name;									/* set attribute name */
		xml->attr[l + 2] = NULL;										/* null terminate attribute list */
		xml->attr[l + 3] = realloc(xml->attr[l + 1], (c = strlen((const x_s8 *)(xml->attr[l + 1]))) + 2);
		strcpy((x_s8 *)(xml->attr[l + 3] + c), (const x_s8 *)" ");		/* set name/value as not malloced */

		if(xml->flags & XML_DUP)
			xml->attr[l + 3][c] = XML_NAME_MALLOC;
	}
	else if(xml->flags & XML_DUP)
		free((char *)name);										/* name was strduped */

	for(c = l; xml->attr[c]; c += 2);							/* find end of attribute list */

	if(xml->attr[c + 1][l / 2] & XML_TXT_MALLOC)
		free(xml->attr[l + 1]);

	if(xml->flags & XML_DUP)
		xml->attr[c + 1][l / 2] |= XML_TXT_MALLOC;
	else
		xml->attr[c + 1][l / 2] &= ~XML_TXT_MALLOC;

	if(value)
		xml->attr[l + 1] = (char *)value;						/* set attribute value */
	else														/* remove attribute */
	{
		if(xml->attr[c + 1][l / 2] & XML_NAME_MALLOC)
			free(xml->attr[l]);

		memmove(xml->attr + l, xml->attr + l + 2, (c - l + 2) * sizeof(char*));
		xml->attr = realloc(xml->attr, (c + 2) * sizeof(char *));
		memmove(xml->attr[c + 1] + (l / 2), xml->attr[c + 1] + (l / 2) + 1, (c / 2) - (l / 2));
	}

	xml->flags &= ~XML_DUP;										/* clear strdup() flag */
	return xml;
}

/*
 * called when parser finds character content between open and closing tag
 */
static void xml_char_content(struct xml_root * root, char * s, x_size len, char t)
{
	struct xml * xml = root->cur;
	char *m = s;
	x_size l;

	if(! xml || ! xml->name || ! len)
		return;													/* sanity check */

	s[len] = '\0';												/* null terminate text (calling functions anticipate this) */
	len = strlen((const x_s8 *)(s = xml_decode(s, root->ent, t))) + 1;

	if(! *(xml->txt))
		xml->txt = s;											/* initial character content */
	else														/* allocate our own memory and make a copy */
	{
		xml->txt = (xml->flags & XML_TXT_MALLOC)				/* allocate some space */
				   ? realloc(xml->txt, (l = strlen((const x_s8 *)(xml->txt))) + len)
				   : strcpy(malloc((l = strlen((const x_s8 *)(xml->txt))) + len), (const x_s8 *)xml->txt);
		strcpy((x_s8 *)(xml->txt + l), (const x_s8 *)s);		/* add new char content */
		if(s != m)
			free(s);											/* free s if it was malloced by xml_decode() */
	}

	if(xml->txt != m)
		xml_set_flag(xml, XML_TXT_MALLOC);
}

/*
 * called when parser finds closing tag
 */
static struct xml * xml_close_tag(struct xml_root * root, char * name, char * s)
{
	if(! root->cur || ! root->cur->name || strcmp((const x_s8 *)name, (const x_s8 *)root->cur->name))
		return xml_err(root, s, "unexpected closing tag </%s>", name);

	root->cur = root->cur->parent;
	return NULL;
}

/*
 * checks for circular entity references, returns non-zero if no circular
 * references are found, zero otherwise
 */
static x_s32 xml_ent_ok(char * name, char * s, char ** ent)
{
	x_s32 i;

	for(; ; s++)
	{
		while(*s && *s != '&')									/* find next entity reference */
			s++;

		if(! *s)
			return 1;

		if(! strncmp((x_s8 *)(s + 1), (const x_s8 *)name, strlen((const x_s8 *)name)))
			return 0;

		for(i = 0; ent[i] && strncmp((x_s8 *)ent[i], (const x_s8 *)(s + 1), strlen((const x_s8 *)ent[i])); i += 2);

		if(ent[i] && ! xml_ent_ok(name, ent[i + 1], ent))
			return 0;
	}
}

/*
 * return parser error message or empty string if none
 */
const char * xml_error(struct xml * xml)
{
	while(xml && xml->parent)									/* find root tag */
		xml = xml->parent;

	return(xml) ? ((struct xml_root *)xml)->err : "";
}

/*
 * removes a tag along with its subtags without freeing its memory
 */
struct xml * xml_cut(struct xml * xml)
{
	struct xml * cur;

	if(! xml)
		return NULL;

	if(xml->next)
		xml->next->sibling = xml->sibling;						/* patch sibling list */

	if(xml->parent)												/* not root tag */
	{
		cur = xml->parent->child;								/* find head of subtag list */
		if(cur == xml)
			xml->parent->child = xml->ordered;					/* first subtag */
		else													/* not first subtag */
		{
			while (cur->ordered != xml)
				cur = cur->ordered;
			cur->ordered = cur->ordered->ordered;				/* patch ordered list */

			cur = xml->parent->child;							/* go back to head of subtag list */
			if(strcmp((const x_s8 *)cur->name, (const x_s8 *)xml->name))	/* not in first sibling list */
			{
				while (strcmp((const x_s8 *)cur->sibling->name, (const x_s8 *)xml->name))
					cur = cur->sibling;
				if(cur->sibling == xml)							/* first of a sibling list */
				{
					cur->sibling = (xml->next) ? xml->next : cur->sibling->sibling;
				}
				else
					cur = cur->sibling;							/* not first of a sibling list */
			}

			while(cur->next && cur->next != xml)
				cur = cur->next;
			if(cur->next)
				cur->next = cur->next->next;					/* patch next list */
		}
	}

	xml->ordered = xml->sibling = xml->next = NULL;
	return xml;
}

/*
 * called when the parser finds a processing instruction
 */
static void xml_proc_inst(struct xml_root * root, char * s, x_size len)
{
	x_s32 i = 0, j = 1;
	char * target = s;

	s[len] = '\0';												/* null terminate instruction */
	if(*(s += strcspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE))))
	{
		*s = '\0';												/* null terminate target */
		s += strspn((const x_s8 *)(s + 1), (const x_s8 *)(XML_WHITESPACE)) + 1;				/* skip whitespace after target */
	}

	if(!strcmp((const x_s8 *)target, (const x_s8 *)"xml"))									/* <?xml ... ?> */
	{
		if((s = (char *)strstr((const x_s8 *)s, (const x_s8 *)"standalone")) && ! strncmp((x_s8 *)(s + strspn((const x_s8 *)(s + 10), (const x_s8 *)(XML_WHITESPACE "='\"")) + 10), (const x_s8 *)"yes", 3))
			root->standalone = 1;
		return;
	}

	if(!root->pi[0])
		*(root->pi = malloc(sizeof(char **))) = NULL;			/* first pi */

	while(root->pi[i] && strcmp((const x_s8 *)target, (const x_s8 *)root->pi[i][0]))
		i++;													/* find target */

	if(!root->pi[i])											/* new target */
	{
		root->pi = realloc(root->pi, sizeof(char **) * (i + 2));
		root->pi[i] = malloc(sizeof(char *) * 3);
		root->pi[i][0] = target;
		root->pi[i][1] = (char *)(root->pi[i + 1] = NULL);		/* terminate pi list */
		root->pi[i][2] = (char *)strdup((const x_s8 *)"");		/* empty document position list */
	}

	while(root->pi[i][j])
		j++;													/* find end of instruction list for this target */

	root->pi[i] = realloc(root->pi[i], sizeof(char *) * (j + 3));
	root->pi[i][j + 2] = realloc(root->pi[i][j + 1], j + 1);
	strcpy((x_s8 *)(root->pi[i][j + 2] + j - 1), (const x_s8 *)((root->xml.name) ? ">" : "<"));
	root->pi[i][j + 1] = NULL;									/* null terminate pi list for this target */
	root->pi[i][j] = s;											/* set instruction */
}

/*
 * called when the parser finds an internal doctype subset
 */
static x_s16 xml_internal_dtd(struct xml_root * root, char * s, x_size len)
{
	char q, *c, *t, *n = NULL, *v, **ent, **pe;
	x_s32 i, j;

	pe = memcpy(malloc(sizeof(xml_nil)), xml_nil, sizeof(xml_nil));

	for(s[len] = '\0'; s; )
	{
		while (*s && *s != '<' && *s != '%')					/* find next declaration */
			s++;

		if(! *s)
			break;
		else if(! strncmp((x_s8 *)s, (const x_s8 *)"<!ENTITY", 8))							/* parse entity definitions */
		{
			c = s += strspn((const x_s8 *)(s + 8), (const x_s8 *)(XML_WHITESPACE)) + 8; 	/* skip white space separator */
			n = s + strspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE "%"));			/* find name */
			*(s = n + strcspn((const x_s8 *)n, (const x_s8 *)(XML_WHITESPACE))) = ';'; 		/* append ; to name */

			v = s + strspn((const x_s8 *)(s + 1), (const x_s8 *)(XML_WHITESPACE)) + 1; 		/* find value */
			if((q = *(v++)) != '"' && q != '\'')				/* skip externals */
			{
				s = (char *)strchr((const x_s8 *)s, '>');
				continue;
			}

			for(i = 0, ent = (*c == '%') ? pe : root->ent; ent[i]; i++);
			ent = realloc(ent, (i + 3) * sizeof(char *)); // space for next ent
			if(*c == '%')
				pe = ent;
			else
				root->ent = ent;

			*(++s) = '\0';										/* null terminate name */
			if((s = (char *)strchr((const x_s8 *)v, q)))
				*(s++) = '\0';									/* null terminate value */

			ent[i + 1] = xml_decode(v, pe, '%'); 				/* set value */
			ent[i + 2] = NULL;									/* null terminate entity list */

			if(! xml_ent_ok(n, ent[i + 1], ent))				/* circular reference */
			{
				if(ent[i + 1] != v) free(ent[i + 1]);
					xml_err(root, v, "circular entity declaration &%s", n);
				break;
			}
			else
				ent[i] = n; 									/* set entity name */
		}
		else if(! strncmp((x_s8 *)s, (const x_s8 *)"<!ATTLIST", 9))							/* parse default attributes */
		{
			t = s + strspn((const x_s8 *)(s + 9), (const x_s8 *)(XML_WHITESPACE)) + 9; 		/* skip whitespace separator */
			if(! *t)
			{
				xml_err(root, t, "unclosed <!ATTLIST");
				break;
			}

			if(*(s = t + strcspn((const x_s8 *)t, (const x_s8 *)(XML_WHITESPACE ">"))) == '>')
				continue;
			else
				*s = '\0'; 										/* null terminate tag name */

			for(i = 0; root->attr[i] && strcmp((const x_s8 *)n, (const x_s8 *)root->attr[i][0]); i++);

			while(*(n = ++s + strspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE))) && *n != '>')
			{
				if(*(s = n + strcspn((const x_s8 *)n, (const x_s8 *)(XML_WHITESPACE))))
					*s = '\0'; 									/* attr name */
				else
				{
					xml_err(root, t, "malformed <!ATTLIST");
					break;
				}

				s += strspn((const x_s8 *)(s + 1), (const x_s8 *)(XML_WHITESPACE)) + 1; 		/* find next token */
				c = (strncmp((x_s8 *)s, (const x_s8 *)"CDATA", 5)) ? "*" : " "; 				/* is it cdata? */
				if(! strncmp((x_s8 *)s, (const x_s8 *)"NOTATION", 8))
					s += strspn((const x_s8 *)(s + 8), (const x_s8 *)(XML_WHITESPACE)) + 8;
				s = (*s == '(') ? (char *)(strchr((const x_s8 *)s, ')')) : s + strcspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE));
				if(! s)
				{
					xml_err(root, t, "malformed <!ATTLIST");
					break;
				}

				s += strspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE ")")); 			/* skip white space separator */
				if(! strncmp((x_s8 *)s, (const x_s8 *)"#FIXED", 6))
					s += strspn((const x_s8 *)(s + 6), (const x_s8 *)(XML_WHITESPACE)) + 6;
				if(*s == '#')									/* no default value */
				{
					s += strcspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE ">")) - 1;
					if (*c == ' ')								/* cdata is default, nothing to do */
						continue;
					v = NULL;
				}
				else if((*s == '"' || *s == '\'')  && (s = (char *)strchr((const x_s8 *)(v = s + 1), *s)))
					*s = '\0';
				else
				{
					xml_err(root, t, "malformed <!ATTLIST");
					break;
				}

				if(!root->attr[i])								/* new tag name */
				{
					root->attr = (! i) ? malloc(2 * sizeof(char **)) : realloc(root->attr, (i + 2) * sizeof(char **));
					root->attr[i] = malloc(2 * sizeof(char *));
					root->attr[i][0] = t; 						/* set tag name */
					root->attr[i][1] = (char *)(root->attr[i + 1] = NULL);
				}

				for(j = 1; root->attr[i][j]; j += 3);			/* find end of list */
				root->attr[i] = realloc(root->attr[i], (j + 4) * sizeof(char *));

				root->attr[i][j + 3] = NULL; 					/* null terminate list */
				root->attr[i][j + 2] = c;						/* is it cdata? */
				root->attr[i][j + 1] = (v) ? xml_decode(v, root->ent, *c) : NULL;
				root->attr[i][j] = n;							/* attribute name */
			}
		}
		else if(! strncmp((x_s8 *)s, (const x_s8 *)"<!--", 4))
			s = (char *)strstr((const x_s8 *)(s + 4), (const x_s8 *)"-->");					/* comments */
		else if(! strncmp((x_s8 *)s, (const x_s8 *)"<?", 2))								/* processing instructions */
		{
			if((s = (char *)strstr((const x_s8 *)(c = s + 2), (const x_s8 *)"?>")))
				xml_proc_inst(root, c, s++ - c);
		}
		else if(*s == '<')
			s = (char *)strchr((const x_s8 *)s, '>');										/* skip other declarations */
		else if(*(s++) == '%' && ! root->standalone)
			break;
	}

	free(pe);
	return ! *root->err;
}

/*
 * converts a UTF-16 string to UTF-8. returns a new string that must be freed
 * or NULL if no conversion was needed.
 */
static char * xml_str2utf8(char ** s, x_size * len)
{
	char * u;
	x_size l = 0, sl, max = *len;
	x_s32 c, d;
	x_s32 b, be = (**s == '\xFE') ? 1 : (**s == '\xFF') ? 0 : -1;

	if(be == -1)
		return NULL; 											/* not UTF-16 */

	u = malloc(max);
	for(sl = 2; sl < *len - 1; sl += 2)
	{
		c = (be) ? (((*s)[sl] & 0xFF) << 8) | ((*s)[sl + 1] & 0xFF)		/* UTF-16BE */
				: (((*s)[sl + 1] & 0xFF) << 8) | ((*s)[sl] & 0xFF);		/* UTF-16LE */
		if(c >= 0xD800 && c <= 0xDFFF && (sl += 2) < *len - 1)			/* high-half */
		{
			d = (be) ? (((*s)[sl] & 0xFF) << 8) | ((*s)[sl + 1] & 0xFF)
					: (((*s)[sl + 1] & 0xFF) << 8) | ((*s)[sl] & 0xFF);
			c = (((c & 0x3FF) << 10) | (d & 0x3FF)) + 0x10000;
		}

		while (l + 6 > max) u = realloc(u, max += XML_BUFFER_SIZE);

		if (c < 0x80)
			u[l++] = c;													/* US-ASCII subset */
		else															/* multi-byte UTF-8 sequence */
		{
			for(b = 0, d = c; d; d /= 2)								/* bits in c */
				b++;
			b = (b - 2) / 5;											/* bytes in payload */
			u[l++] = (0xFF << (7 - b)) | (c >> (6 * b));				/* head */
			while(b) u[l++] = 0x80 | ((c >> (6 * --b)) & 0x3F);			/* payload */
		}
	}

	return *s = realloc(u, *len = l);
}

/*
 * frees a tag attribute list
 */
static void xml_free_attr(char ** attr)
{
	x_s32 i = 0;
	char * m;

	if(! attr || attr == xml_nil)
		return;

	while(attr[i])
		i += 2;															/* find end of attribute list */

	m = attr[i + 1];													/* list of which names and values are malloced */

	for(i = 0; m[i]; i++)
	{
		if(m[i] & XML_NAME_MALLOC)
			free(attr[i * 2]);
		if(m[i] & XML_TXT_MALLOC)
			free(attr[(i * 2) + 1]);
	}

	free(m);
	free(attr);
}

/*
 * returns a new empty xml structure with the given root tag name
 */
struct xml * xml_new(const char * name)
{
    static char *ent[] = { "lt;", "&#60;", "gt;", "&#62;", "quot;", "&#34;",
						"apos;", "&#39;", "amp;", "&#38;", NULL };
	struct xml_root * root = (struct xml_root *) memset(malloc(sizeof(struct xml_root)), '\0', sizeof(struct xml_root));

	root->xml.name = (char *)name;
	root->cur = &root->xml;
	strcpy((x_s8 *)root->err, (const x_s8 *)(root->xml.txt = ""));
	root->ent = memcpy(malloc(sizeof(ent)), ent, sizeof(ent));
	root->attr = root->pi = (char ***) (root->xml.attr = xml_nil);

	return &root->xml;
}

/*
 * free the memory allocated for the xml structure
 */
void xml_free(struct xml * xml)
{
	struct xml_root * root = (struct xml_root *)xml;
	x_s32 i, j;
	char **a, *s;

	if(!xml)
		return;

	xml_free(xml->child);
	xml_free(xml->ordered);

	if(!xml->parent)													/* free root tag allocations */
	{
		for(i = 10; root->ent[i]; i += 2)								/* 0 - 9 are default entites (<>&"') */
			if((s = root->ent[i + 1]) < root->s || s > root->e)
				free(s);
		free(root->ent);												/* free list of general entities */

		for(i = 0; (a = root->attr[i]); i++)
		{
			for(j = 1; a[j++]; j += 2) 									/* free malloced attribute values */
				if(a[j] && (a[j] < root->s || a[j] > root->e))
					free(a[j]);
			free(a);
		}

		if(root->attr[0])
			free(root->attr);											/* free default attribute list */

		for(i = 0; root->pi[i]; i++)
		{
			for(j = 1; root->pi[i][j]; j++);
			free(root->pi[i][j + 1]);
			free(root->pi[i]);
		}

		if(root->pi[0])
			free(root->pi);												/* free processing instructions */

		if(root->len == -1)
			free(root->m);												/* malloced xml data */

		if(root->u)
			free(root->u);												/* utf8 conversion */
	}

	xml_free_attr(xml->attr);											/* tag attributes */

	if((xml->flags & XML_TXT_MALLOC))
		free(xml->txt);													/* character content */

	if((xml->flags & XML_NAME_MALLOC))
		free(xml->name);												/* tag name */

	free(xml);
}

/*
 * parse the given xml string and return an xml structure
 */
struct xml * xml_parse_str(char * s, x_size len)
{
	struct xml_root * root = (struct xml_root *)xml_new(NULL);
	char q, e, *d, **attr, **a = NULL;									/* initialize a to avoid compile warning */
	x_s32 l, i, j;

	root->m = s;
	if(!len)
		return xml_err(root, NULL, "root tag missing");

	root->u = xml_str2utf8(&s, &len);									/* convert utf-16 to utf-8 */
	root->e = (root->s = s) + len;										/* record start and end of work area */

	e = s[len - 1];														/* save end char */
	s[len - 1] = '\0';													/* turn end char into null terminator */

	while(*s && *s != '<')
		s++;															/* find first tag */
	if(!*s)
		return xml_err(root, s, "root tag missing");

	for(;;)
	{
		attr = (char **)xml_nil;
		d = ++s;

		if((isalpha(*s)) || (*s == '_') || (*s == ':') || ((signed char)(*s) < '\0'))			 		/* new tag */
		{
			if(!root->cur)
				return xml_err(root, d, "markup outside of root element");

			s += strcspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE "/>"));
			while(isspace(*s))
				*(s++) = '\0';																			/* null terminate tag name */

			if(*s && *s != '/' && *s != '>')															/* find tag in default attr list */
				for(i = 0; (a = root->attr[i]) && strcmp((const x_s8 *)a[0], (const x_s8 *)d); i++);

			for(l = 0; *s && *s != '/' && *s != '>'; l += 2)			 								/* new attrib */
			{
				attr = (l) ? realloc(attr, (l + 4) * sizeof(char *)) : malloc(4 * sizeof(char *));		/* allocate space */
				attr[l + 3] = (l) ? realloc(attr[l + 1], (l / 2) + 2) : malloc(2);						/* mem for list of maloced vals */
				strcpy((x_s8 *)(attr[l + 3] + (l / 2)), (const x_s8 *)" ");								/* value is not malloced */
				attr[l + 2] = NULL;																		/* null terminate list */
				attr[l + 1] = "";																		/* temporary attribute value */
				attr[l] = s;																			/* set attribute name */

				s += strcspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE "=/>"));
				if(*s == '=' || isspace(*s))
				{
					*(s++) = '\0';										/* null terminate tag attribute name */
					q = *(s += strspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE "=")));
					if(q == '"' || q == '\'')							/* attribute value */
					{
						attr[l + 1] = ++s;
						while(*s && *s != q)
							s++;
						if(*s)
							*(s++) = '\0'; 								/* null terminate attribute val */
						else
						{
							xml_free_attr(attr);
							return xml_err(root, d, "missing %c", q);
						}

						for(j = 1; a && a[j] && strcmp((const x_s8 *)a[j], (const x_s8 *)attr[l]); j +=3);
						attr[l + 1] = xml_decode(attr[l + 1], root->ent, (a && a[j]) ? *a[j + 2] : ' ');

						if(attr[l + 1] < d || attr[l + 1] > s)
							attr[l + 3][l / 2] = XML_TXT_MALLOC;		/* value malloced */
					}
				}
				while(isspace(*s))
					s++;
			}

			if(*s == '/')												 /* self closing tag */
			{
				*(s++) = '\0';
				if((*s && *s != '>') || (! *s && e != '>'))
				{
					if(l)
						xml_free_attr(attr);
					return xml_err(root, d, "missing >");
				}
				xml_open_tag(root, d, attr);
				xml_close_tag(root, d, s);
			}
			else if((q = *s) == '>' || (! *s && e == '>'))				/* open tag */
			{
				*s = '\0';												/* temporarily null terminate tag name */
				xml_open_tag(root, d, attr);
				*s = q;
			}
			else
			{
				if(l)
					xml_free_attr(attr);
				return xml_err(root, d, "missing >");
			}
		}
		else if(*s == '/')												 /* close tag */
		{
			s += strcspn((const x_s8 *)(d = s + 1), (const x_s8 *)(XML_WHITESPACE ">")) + 1;
			if(!(q = *s) && e != '>')
				return xml_err(root, d, "missing >");
			*s = '\0';													/* temporarily null terminate tag name */
			if(xml_close_tag(root, d, s))
				return &root->xml;
			if(isspace(*s = q))
				s += strspn((const x_s8 *)s, (const x_s8 *)(XML_WHITESPACE));
		}
		else if(! strncmp((x_s8 *)s, (const x_s8 *)"!--", 3))									 /* xml comment */
		{
			if(! (s = (char *)strstr((const x_s8 *)(s + 3), (const x_s8 *)"--")) || (*(s += 2) != '>' && *s) || (! *s && e != '>'))
				return xml_err(root, d, "unclosed <!--");
		}
		else if(! strncmp((const x_s8 *)s, (const x_s8 *)"![CDATA[", 8))
		{
			if((s = (char *)strstr((const x_s8 *)s, (const x_s8 *)"]]>")))
				xml_char_content(root, d + 8, (s += 2) - d - 10, 'c');
			else
				return xml_err(root, d, "unclosed <![CDATA[");
		}
		else if(! strncmp((const x_s8 *)s, (const x_s8 *)"!DOCTYPE", 8))
		{
			for (l = 0; *s && ((! l && *s != '>') || (l && (*s != ']' || *(s + strspn((const x_s8 *)(s + 1), (const x_s8 *)(XML_WHITESPACE)) + 1) != '>')));
			l = (*s == '[') ? 1 : l) s += strcspn((const x_s8 *)(s + 1), (const x_s8 *)"[]>") + 1;
			if(! *s && e != '>')
				return xml_err(root, d, "unclosed <!DOCTYPE");
			d = (l) ? (char *)(strchr((const x_s8 *)d, '[') + 1) : d;
			if(l && ! xml_internal_dtd(root, d, s++ - d))
				return &root->xml;
		}
		else if(*s == '?')												 /* <?...?> processing instructions */
		{
			do{
				s = (char *)strchr((const x_s8 *)s, '?');
			} while(s && *(++s) && *s != '>');

			if(! s || (! *s && e != '>'))
				return xml_err(root, d, "unclosed <?");
			else xml_proc_inst(root, d + 1, s - d - 2);
		}
		else
			return xml_err(root, d, "unexpected <");

		if(! s || ! *s)
			break;

		*s = '\0';
		d = ++s;
		if(*s && *s != '<')												 /* tag character content */
		{
			while(*s && *s != '<')
				s++;
			if(*s)
				xml_char_content(root, d, s - d, '&');
			else
				break;
		}
		else if(! *s)
			break;
	}

	if(! root->cur)
		return &root->xml;
	else if(! root->cur->name)
		return xml_err(root, d, "root tag missing");
	else
		return xml_err(root, d, "unclosed tag <%s>", root->cur->name);
}

/*
 * Encodes ampersand sequences appending the results to *dst, reallocating *dst
 * if length excedes max. a is non-zero for attribute encoding. Returns *dst
 */
static char * xml_ampencode(const char *s, x_size len, char **dst, x_size *dlen, x_size *max, x_s16 a)
{
	const char * e;

	for(e = s + len; s != e; s++)
	{
		while(*dlen + 10 > *max) *dst = realloc(*dst, *max += XML_BUFFER_SIZE);

		switch(*s)
		{
		case '\0':
			return *dst;
		case '&':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)"&amp;");
			break;
		case '<':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)"&lt;");
			break;
		case '>':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)"&gt;");
			break;
		case '"':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)((a) ? "&quot;" : "\""));
			break;
		case '\n':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)((a) ? "&#xA;" : "\n"));
			break;
		case '\t':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)((a) ? "&#x9;" : "\t"));
			break;
		case '\r':
			*dlen += sprintf((x_s8 *)(*dst + *dlen), (const x_s8 *)"&#xD;");
			break;
		default:
			(*dst)[(*dlen)++] = *s;
		}
	}
	return *dst;
}

/*
 * recursively converts each tag to xml appending it to *s. reallocates *s if
 * its length excedes max. start is the location of the previous tag in the
 * parent tag's character content. Returns *s.
 */
static char *xml_toxml_r(struct xml * xml, char **s, x_size *len, x_size *max, x_size start, char ***attr)
{
	x_s32 i, j;
	char *txt = (xml->parent) ? xml->parent->txt : "";
	x_size off = 0;

	/* parent character content up to this tag */
	*s = xml_ampencode(txt + start, xml->off - start, s, len, max, 0);

	while(*len + strlen((const x_s8 *)xml->name) + 4 > *max)						/* reallocate s */
	*s = realloc(*s, *max += XML_BUFFER_SIZE);

	*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)"<%s", xml->name);			/* open tag */
	for(i = 0; xml->attr[i]; i += 2)												/* tag attributes */
	{
		if(xml_attr(xml, xml->attr[i]) != xml->attr[i + 1])
			continue;
		while(*len + strlen((const x_s8 *)xml->attr[i]) + 7 > *max)					/* reallocate s */
		*s = realloc(*s, *max += XML_BUFFER_SIZE);

		*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)" %s=\"", xml->attr[i]);
		xml_ampencode(xml->attr[i + 1], -1, s, len, max, 1);
		*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)"\"");
	}

	for(i = 0; attr[i] && strcmp((const x_s8 *)attr[i][0], (const x_s8 *)xml->name); i++);
	for(j = 1; attr[i] && attr[i][j]; j += 3)							 /* default attributes */
	{
		if(! attr[i][j + 1] || xml_attr(xml, attr[i][j]) != attr[i][j + 1])
			continue;													/* skip duplicates and non-values */
		while(*len + strlen((const x_s8 *)attr[i][j]) + 7 > *max)						/* reallocate s */
		*s = realloc(*s, *max += XML_BUFFER_SIZE);

		*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)" %s=\"", attr[i][j]);
		xml_ampencode(attr[i][j + 1], -1, s, len, max, 1);
		*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)"\"");
	}
	*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)">");

	*s = (xml->child) ? xml_toxml_r(xml->child, s, len, max, 0, attr) : xml_ampencode(xml->txt, -1, s, len, max, 0);

	while(*len + strlen((const x_s8 *)xml->name) + 4 > *max)							/* reallocate s */
	*s = realloc(*s, *max += XML_BUFFER_SIZE);

	*len += sprintf((x_s8 *)(*s + *len), (const x_s8 *)"</%s>", xml->name);						/* close tag */

	while(txt[off] && off < xml->off)
		off++;															/* make sure off is within bounds */
	return(xml->ordered) ? xml_toxml_r(xml->ordered, s, len, max, off, attr) : xml_ampencode(txt + off, -1, s, len, max, 0);
}

/*
 * converts an xml structure back to xml. returns a string of xml data that
 * must be freed.
 */
char * xml_toxml(struct xml * xml)
{
	struct xml * p = (xml) ? xml->parent : NULL, * o = (xml) ? xml->ordered : NULL;
	struct xml_root * root = (struct xml_root *)xml;
	x_size len = 0, max = XML_BUFFER_SIZE;
	char *s = (char *)strcpy((x_s8 *)(malloc(max)), (const x_s8 *)""), *t, *n;
	x_s32 i, j, k;

	if(! xml || ! xml->name)
		return realloc(s, len + 1);
	while(root->xml.parent)
		root = (struct xml_root *)root->xml.parent;						/* root tag */

	for(i = 0; ! p && root->pi[i]; i++)									/* pre-root processing instructions */
	{
		for(k = 2; root->pi[i][k - 1]; k++);
		for(j = 1; (n = root->pi[i][j]); j++)
		{
			if(root->pi[i][k][j - 1] == '>')							/* not pre-root */
				continue;
			while(len + strlen((const x_s8 *)(t = root->pi[i][0])) + strlen((const x_s8 *)n) + 7 > max)
				s = realloc(s, max += XML_BUFFER_SIZE);
			len += sprintf((x_s8 *)(s + len), (const x_s8 *)"<?%s%s%s?>\n", t, *n ? " " : "", n);
		}
	}

	xml->parent = xml->ordered = NULL;
	s = xml_toxml_r(xml, &s, &len, &max, 0, root->attr);
	xml->parent = p;
	xml->ordered = o;

	for(i = 0; ! p && root->pi[i]; i++)									/* post-root processing instructions */
	{
		for(k = 2; root->pi[i][k - 1]; k++);
		for(j = 1; (n = root->pi[i][j]); j++)
		{
			if(root->pi[i][k][j - 1] == '<')							/* not post-root */
				continue;
			while(len + strlen((const x_s8 *)(t = root->pi[i][0])) + strlen((const x_s8 *)n) + 7 > max)
				s = realloc(s, max += XML_BUFFER_SIZE);
			len += sprintf((x_s8 *)(s + len), (const x_s8 *)"\n<?%s%s%s?>", t, *n ? " " : "", n);
		}
	}

	return realloc(s, len + 1);
}

/*
 * a wrapper for xml_parse_str() that accepts a file stream. reads the entire
 * stream into memory and then parses it.
 */
struct xml * xml_parse_file(const char * name)
{
    struct xml_root * root;
    struct xml * xml;
    x_s32 fd;
    x_size l, len = 0;
	char * s;

	fd = open(name, O_RDONLY, (S_IRUSR|S_IRGRP|S_IROTH));
	if(fd < 0)
		return NULL;

	if(! (s = malloc(XML_BUFFER_SIZE)))
		return NULL;

	do {
        len += (l = read(fd, (s + len), XML_BUFFER_SIZE));
        if(l == XML_BUFFER_SIZE)
        	s = realloc(s, len + XML_BUFFER_SIZE);
    } while (s && l == XML_BUFFER_SIZE);

	if(!s)
		return NULL;

    root = (struct xml_root *)xml_parse_str(s, len);
    root->len = -1;
    xml = &root->xml;

	close(fd);

	return xml;
}
