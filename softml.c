/*
  softml_dom.c — Forgiving SoftML parser to an internal DOM
  Build: cc -std=c99 -Wall -Wextra -O2 softml_dom.c -o softml_dom
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========== Core data structures ========== */
typedef struct Attr {
    char *key;
    char *val; /* NULL means bare attribute -> true */
} Attr;

typedef enum { NODE_ELEM, NODE_TEXT } NodeType;

typedef struct Node {
    NodeType type;
    char *name;              /* tag name for elements; NULL for text */
    Attr *attrs;
    size_t attr_count;
    struct Node **children;
    size_t child_count;
    char *text;              /* for text nodes; for elements, optional accumulated text */
} Node;

typedef struct Document {
    Node **roots;
    size_t root_count;
} Document;

/* ========== Utilities ========== */
static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "Out of memory\n"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}
static int is_name_char(int c) {
    return isalnum(c) || c=='_' || c=='-' || c==':' || c=='.';
}
static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* ========== DOM management ========== */
static Node *new_elem(const char *name) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) { fprintf(stderr, "OOM\n"); exit(1); }
    n->type = NODE_ELEM;
    n->name = xstrdup(name);
    return n;
}
static Node *new_text(const char *s, size_t len) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) { fprintf(stderr, "OOM\n"); exit(1); }
    n->type = NODE_TEXT;
    n->text = (char *)xmalloc(len+1);
    memcpy(n->text, s, len);
    n->text[len] = '\0';
    return n;
}
static void add_child(Node *parent, Node *child) {
    parent->children = (Node **)realloc(parent->children, (parent->child_count+1)*sizeof(Node *));
    if (!parent->children) { fprintf(stderr, "OOM\n"); exit(1); }
    parent->children[parent->child_count++] = child;
}
static void add_attr(Node *n, const char *key, const char *val) {
    n->attrs = (Attr *)realloc(n->attrs, (n->attr_count+1)*sizeof(Attr));
    if (!n->attrs) { fprintf(stderr, "OOM\n"); exit(1); }
    n->attrs[n->attr_count].key = xstrdup(key);
    n->attrs[n->attr_count].val = val ? xstrdup(val) : NULL;
    n->attr_count++;
}

/* ========== Stack of open elements ========== */
typedef struct {
    Node **stk;
    size_t sp;
} Stack;

static void st_push(Stack *s, Node *n) {
    s->stk = (Node **)realloc(s->stk, (s->sp+1)*sizeof(Node *));
    if (!s->stk) { fprintf(stderr, "OOM\n"); exit(1); }
    s->stk[s->sp++] = n;
}
static Node *st_top(Stack *s) {
    return s->sp ? s->stk[s->sp-1] : NULL;
}
static Node *st_pop(Stack *s) {
    return s->sp ? s->stk[--s->sp] : NULL;
}

/* ========== Lexer helpers ========== */
static char *read_name(const char **pp) {
    const char *p = *pp;
    if (!is_name_char(*p)) return NULL;
    const char *start = p;
    while (*p && is_name_char(*p)) p++;
    size_t len = (size_t)(p - start);
    char *s = (char *)xmalloc(len+1);
    memcpy(s, start, len);
    s[len] = '\0';
    *pp = p;
    return s;
}

static char *read_attr_value(const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p=='"' || *p=='\'') {
        char quote = *p++;
        const char *start = p;
        while (*p && *p != quote) p++;
        size_t len = (size_t)(p - start);
        char *s = (char *)xmalloc(len+1);
        memcpy(s, start, len);
        s[len] = '\0';
        if (*p==quote) p++;
        *pp = p;
        return s;
    }
    const char *start = p;
    while (*p && !isspace((unsigned char)*p) && *p!='>' && *p!='/') p++;
    size_t len = (size_t)(p - start);
    if (len == 0) return NULL;
    char *s = (char *)xmalloc(len+1);
    memcpy(s, start, len);
    s[len] = '\0';
    *pp = p;
    return s;
}

/* ========== Parser core ========== */
typedef struct {
    Document *doc;
    Stack open;
    int warnings;
} ParseCtx;

static void push_root(Document *doc, Node *n) {
    doc->roots = (Node **)realloc(doc->roots, (doc->root_count+1)*sizeof(Node *));
    if (!doc->roots) { fprintf(stderr, "OOM\n"); exit(1); }
    doc->roots[doc->root_count++] = n;
}

static void push_text(ParseCtx *cx, const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    if (len == 0) return;
    Node *t = new_text(start, len);
    Node *parent = st_top(&cx->open);
    if (parent) add_child(parent, t);
    else push_root(cx->doc, t);
}

static void soft_close_until(ParseCtx *cx, const char *name) {
    if (cx->open.sp == 0) { cx->warnings++; return; }
    size_t i = cx->open.sp;
    while (i > 0) {
        Node *n = cx->open.stk[i-1];
        if (n->type == NODE_ELEM && n->name && strcmp(n->name, name) == 0) {
            while (cx->open.sp >= i) st_pop(&cx->open);
            return;
        }
        i--;
    }
    st_pop(&cx->open);
    cx->warnings++;
}

static void parse_softml(ParseCtx *cx, const char *src) {
    const char *p = src;
    const char *text_start = p;

    while (*p) {
        if (*p != '<') { p++; continue; }
        push_text(cx, text_start, p);
        p++;
        if (*p == '/') {
            p = skip_ws(p);
            char *name = read_name(&p);
            while (*p && *p != '>') p++;
            if (*p=='>') p++;
            if (name) { soft_close_until(cx, name); free(name); }
            else { st_pop(&cx->open); cx->warnings++; }
            text_start = p;
            continue;
        }

        p = skip_ws(p);
        char *name = read_name(&p);
        if (!name) { cx->warnings++; push_text(cx, "<", "<"+1); text_start = p; continue; }

        Node *elem = new_elem(name);
        free(name);

        for (;;) {
            p = skip_ws(p);
            if (!*p || *p=='>' || *p=='/') break;
            char *akey = read_name(&p);
            if (!akey) { while (*p && !isspace((unsigned char)*p) && *p!='>' && *p!='/') p++; cx->warnings++; continue; }
            p = skip_ws(p);
            char *aval = NULL;
            if (*p == '=') { p++; p = skip_ws(p); aval = read_attr_value(&p); }
            add_attr(elem, akey, aval);
            free(akey); if (aval) free(aval);
        }

        int selfclose = 0;
        if (*p == '/') { selfclose = 1; while (*p && *p!='>') p++; }
        if (*p == '>') p++;

        Node *parent = st_top(&cx->open);
        if (parent) add_child(parent, elem);
        else push_root(cx->doc, elem);

        if (!selfclose) st_push(&cx->open, elem);

        text_start = p;
    }

    push_text(cx, text_start, p);
    while (cx->open.sp > 0) st_pop(&cx->open);
}

/* ========== Public API ========== */
Document *softml_parse(const char *src) {
    Document *doc = (Document *)calloc(1, sizeof(Document));
    if (!doc) { fprintf(stderr, "OOM\n"); exit(1); }
    ParseCtx cx = {0};
    cx.doc = doc;
    parse_softml(&cx, src);
    return doc;
}

static int name_eq(const Node *n, const char *tag) {
    return n && n->type == NODE_ELEM && n->name && tag && strcmp(n->name, tag) == 0;
}

Node *softml_find_child(const Node *parent, const char *tag) {
    if (!parent || parent->type != NODE_ELEM) return NULL;
    for (size_t i=0;i<parent->child_count;i++) {
        Node *c = parent->children[i];
        if (name_eq(c, tag)) return c;
    }
    return NULL;
}

Node *softml_find_child_n(const Node *parent, const char *tag, size_t n) {
    if (!parent || parent->type != NODE_ELEM) return NULL;
    size_t k=0;
    for (size_t i=0;i<parent->child_count;i++) {
        Node *c=parent->children[i];
        if (name_eq(c, tag)) { if(k==n) return c; k++; }
    }
    return NULL;
}

const char *softml_get_attr(const Node *elem, const char *key) {
    if (!elem || elem->type!=NODE_ELEM || !key) return NULL;
    for(size_t i=0;i<elem->attr_count;i++) {
        if(elem->attrs[i].key && strcmp(elem->attrs[i].key,key)==0)
            return elem->attrs[i].val ? elem->attrs[i].val : "true";
    }
    return NULL;
}

size_t softml_child_count(const Node *elem) { return elem && elem->type==NODE_ELEM ? elem->child_count : 0; }
Node *softml_child_at(const Node *elem, size_t i) { return (elem && elem->type==NODE_ELEM && i<elem->child_count) ? elem->children[i] : NULL; }
size_t softml_root_count(const Document *doc) { return doc ? doc->root_count : 0; }
Node *softml_root_at(const Document *doc, size_t i) { return (doc && i<doc->root_count) ? doc->roots[i] : NULL; }
int softml_is_text(const Node *n) { return n && n->type==NODE_TEXT; }
const char *softml_text(const Node *n) { return (n && n->type==NODE_TEXT) ? n->text : NULL; }
const char *softml_tag(const Node *n) { return (n && n->type==NODE_ELEM) ? n->name : NULL; }

static void free_node(Node *n) {
    if(!n) return;
    if(n->type==NODE_ELEM) {
        for(size_t i=0;i<n->attr_count;i++) { free(n->attrs[i].key); free(n->attrs[i].val); }
        free(n->attrs);
        for(size_t i=0;i<n->child_count;i++) free_node(n->children[i]);
        free(n->children); free(n->name);
    } else free(n->text);
    free(n);
}
void softml_free(Document *doc) {
    if(!doc) return;
    for(size_t i=0;i<doc->root_count;i++) free_node(doc->roots[i]);
    free(doc->roots); free(doc);
}
