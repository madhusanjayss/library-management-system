#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define STR_LEN        100
#define NAME_LEN       50
#define MAX_MEMBERS    50
#define MAX_BORROW     5
#define INITIAL_TABLE  11
#define MAX_LOAD       0.75
#define PERF_LOOKUPS   2000
#define DATA_FILE      "library.txt"

typedef struct Book {
    int  id;
    char title[STR_LEN];
    char author[STR_LEN];
    int  total;
    int  available;
    struct Book *next;
} Book;

typedef struct Member {
    int  id;
    char name[NAME_LEN];
    int  borrowed[MAX_BORROW];
    int  count;
} Member;

typedef struct BSTNode {
    int  id;
    Book *book;
    struct BSTNode *left, *right;
} BSTNode;

typedef struct HNode {
    int  id;
    Book *book;
    struct HNode *next;
} HNode;

typedef struct {
    HNode **buckets;
    int size;
    int count;
} HashTable;

typedef struct Reservation {
    int bookId;
    int memberId;
    struct Reservation *next;
} Reservation;

typedef struct {
    Reservation *front, *rear;
    int size;
} Queue;

typedef enum { ACT_ADD, ACT_DELETE, ACT_BORROW, ACT_RETURN } ActType;

typedef struct Action {
    ActType type;
    int bookId;
    int memberId;
    Book snap;
    struct Action *next;
} Action;

static Book      *head = NULL;
static BSTNode   *root = NULL;
static HashTable  ht;
static Queue      resQ = {NULL, NULL, 0};
static Action    *undoTop = NULL;
static Member     members[MAX_MEMBERS];
static int        memberCount = 0;

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    return p;
}

static void readLine(const char *prompt, char *buf, int n)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, n, stdin)) {
        printf("\nInput closed. Exiting.\n");
        exit(0);
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int readInt(const char *prompt, int lo, int hi)
{
    char buf[64], *end;
    for (;;) {
        readLine(prompt, buf, sizeof buf);
        if (buf[0] == '\0') { puts("Input cannot be empty."); continue; }
        long v = strtol(buf, &end, 10);
        while (isspace((unsigned char)*end)) end++;
        if (*end != '\0') { puts("Invalid number. Digits only, please."); continue; }
        if (v < lo || v > hi) { printf("Enter a value between %d and %d.\n", lo, hi); continue; }
        return (int)v;
    }
}

static void readText(const char *prompt, char *buf, int n)
{
    for (;;) {
        readLine(prompt, buf, n);
        if (buf[0] == '\0') { puts("Input cannot be empty."); continue; }
        for (char *p = buf; *p; p++)
            if (*p == '|') *p = '-';
        return;
    }
}

static int askYesNo(const char *prompt)
{
    char buf[16];
    for (;;) {
        readLine(prompt, buf, sizeof buf);
        if (buf[0] == 'y' || buf[0] == 'Y') return 1;
        if (buf[0] == 'n' || buf[0] == 'N') return 0;
        puts("Please answer y or n.");
    }
}

static int ciCompare(const char *a, const char *b)
{
    while (*a && *b) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int ciContains(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl) return 1;
    }
    return 0;
}

static unsigned long randBelow(unsigned long m)
{
    return ((unsigned long)rand() * 32768UL + (unsigned long)rand()) % m;
}

static void printHeader(void)
{
    printf("\n%-8s %-30s %-22s %5s %5s\n", "ID", "Title", "Author", "Total", "Avail");
    printf("---------------------------------------------------------------------\n");
}

static void printBook(const Book *b)
{
    printf("%-8d %-30.30s %-22.22s %5d %5d\n",
           b->id, b->title, b->author, b->total, b->available);
}

static Book *listFindIn(Book *h, int id, long *cmp)
{
    for (Book *p = h; p; p = p->next) {
        if (cmp) (*cmp)++;
        if (p->id == id) return p;
    }
    return NULL;
}

static void listAppend(Book *b)
{
    b->next = NULL;
    if (!head) { head = b; return; }
    Book *p = head;
    while (p->next) p = p->next;
    p->next = b;
}

static Book *listRemove(int id)
{
    Book *prev = NULL, *p = head;
    while (p && p->id != id) { prev = p; p = p->next; }
    if (!p) return NULL;
    if (prev) prev->next = p->next; else head = p->next;
    p->next = NULL;
    return p;
}

static BSTNode *bstInsert(BSTNode *r, Book *b)
{
    if (!r) {
        BSTNode *n = xmalloc(sizeof *n);
        n->id = b->id; n->book = b; n->left = n->right = NULL;
        return n;
    }
    if (b->id < r->id)      r->left  = bstInsert(r->left, b);
    else if (b->id > r->id) r->right = bstInsert(r->right, b);
    return r;
}

static BSTNode *bstSearch(BSTNode *r, int id, long *cmp)
{
    while (r) {
        if (cmp) (*cmp)++;
        if (id == r->id) return r;
        r = (id < r->id) ? r->left : r->right;
    }
    return NULL;
}

static BSTNode *bstMin(BSTNode *r)
{
    while (r && r->left) r = r->left;
    return r;
}

static BSTNode *bstDelete(BSTNode *r, int id)
{
    if (!r) return NULL;
    if (id < r->id)      r->left  = bstDelete(r->left, id);
    else if (id > r->id) r->right = bstDelete(r->right, id);
    else {
        if (!r->left)  { BSTNode *t = r->right; free(r); return t; }
        if (!r->right) { BSTNode *t = r->left;  free(r); return t; }
        BSTNode *m = bstMin(r->right);
        r->id = m->id;
        r->book = m->book;
        r->right = bstDelete(r->right, m->id);
    }
    return r;
}

static void bstFree(BSTNode *r)
{
    if (!r) return;
    bstFree(r->left);
    bstFree(r->right);
    free(r);
}

static int bstHeight(BSTNode *r)
{
    if (!r) return 0;
    int l = bstHeight(r->left), h = bstHeight(r->right);
    return 1 + (l > h ? l : h);
}

static void bstInorder(BSTNode *r)
{
    if (!r) return;
    bstInorder(r->left);
    printf("  %-8d %s\n", r->id, r->book->title);
    bstInorder(r->right);
}

static void bstPreorder(BSTNode *r)
{
    if (!r) return;
    printf("  %-8d %s\n", r->id, r->book->title);
    bstPreorder(r->left);
    bstPreorder(r->right);
}

static void bstPostorder(BSTNode *r)
{
    if (!r) return;
    bstPostorder(r->left);
    bstPostorder(r->right);
    printf("  %-8d %s\n", r->id, r->book->title);
}

static void bstPrintTree(BSTNode *r, int depth)
{
    if (!r) return;
    bstPrintTree(r->right, depth + 1);
    printf("%*s%d\n", depth * 6, "", r->id);
    bstPrintTree(r->left, depth + 1);
}

static int isPrime(int n)
{
    if (n < 2) return 0;
    for (int i = 2; i * i <= n; i++)
        if (n % i == 0) return 0;
    return 1;
}

static int nextPrime(int n)
{
    while (!isPrime(n)) n++;
    return n;
}

static void htInit(HashTable *t, int size)
{
    t->size = size;
    t->count = 0;
    t->buckets = calloc((size_t)size, sizeof(HNode *));
    if (!t->buckets) { fprintf(stderr, "Out of memory.\n"); exit(1); }
}

static void htRehash(HashTable *t)
{
    int newSize = nextPrime(t->size * 2);
    HNode **nb = calloc((size_t)newSize, sizeof(HNode *));
    if (!nb) { fprintf(stderr, "Out of memory.\n"); exit(1); }
    for (int i = 0; i < t->size; i++) {
        HNode *p = t->buckets[i];
        while (p) {
            HNode *nx = p->next;
            int idx = p->id % newSize;
            p->next = nb[idx];
            nb[idx] = p;
            p = nx;
        }
    }
    free(t->buckets);
    t->buckets = nb;
    t->size = newSize;
}

static int htInsert(HashTable *t, Book *b)
{
    if ((double)(t->count + 1) / t->size > MAX_LOAD) {
        int old = t->size;
        htRehash(t);
        if (t == &ht)
            printf("[Hash table rehashed: size %d -> %d]\n", old, t->size);
    }
    int idx = b->id % t->size;
    HNode *n = xmalloc(sizeof *n);
    n->id = b->id; n->book = b;
    n->next = t->buckets[idx];
    t->buckets[idx] = n;
    t->count++;
    return idx;
}

static Book *htSearch(HashTable *t, int id, long *cmp)
{
    for (HNode *p = t->buckets[id % t->size]; p; p = p->next) {
        if (cmp) (*cmp)++;
        if (p->id == id) return p->book;
    }
    return NULL;
}

static int htDelete(HashTable *t, int id)
{
    HNode **pp = &t->buckets[id % t->size];
    while (*pp && (*pp)->id != id) pp = &(*pp)->next;
    if (!*pp) return 0;
    HNode *dead = *pp;
    *pp = dead->next;
    free(dead);
    t->count--;
    return 1;
}

static void htFree(HashTable *t)
{
    for (int i = 0; i < t->size; i++) {
        HNode *p = t->buckets[i];
        while (p) { HNode *nx = p->next; free(p); p = nx; }
    }
    free(t->buckets);
    t->buckets = NULL;
    t->size = t->count = 0;
}

static void htDisplay(HashTable *t)
{
    int maxChain = 0;
    printf("\nHash table  (size = %d, entries = %d, load factor = %.2f)\n",
           t->size, t->count, (double)t->count / t->size);
    printf("index = bookID %% %d\n\n", t->size);
    for (int i = 0; i < t->size; i++) {
        int len = 0;
        printf("[%3d] ", i);
        for (HNode *p = t->buckets[i]; p; p = p->next) {
            printf("%d%s", p->id, p->next ? " -> " : "");
            len++;
        }
        if (len == 0) printf("(empty)");
        printf("\n");
        if (len > maxChain) maxChain = len;
    }
    printf("\nLongest chain: %d\n", maxChain);
}

static void qEnqueue(Queue *q, int bookId, int memberId)
{
    Reservation *r = xmalloc(sizeof *r);
    r->bookId = bookId; r->memberId = memberId; r->next = NULL;
    if (q->rear) q->rear->next = r; else q->front = r;
    q->rear = r;
    q->size++;
}

static Reservation *qTakeFirstForBook(Queue *q, int bookId)
{
    Reservation *prev = NULL, *p = q->front;
    while (p && p->bookId != bookId) { prev = p; p = p->next; }
    if (!p) return NULL;
    if (prev) prev->next = p->next; else q->front = p->next;
    if (q->rear == p) q->rear = prev;
    q->size--;
    p->next = NULL;
    return p;
}

static void qRemoveBook(Queue *q, int bookId)
{
    Reservation *r;
    while ((r = qTakeFirstForBook(q, bookId)) != NULL) free(r);
}

static int qContains(Queue *q, int bookId, int memberId)
{
    for (Reservation *p = q->front; p; p = p->next)
        if (p->bookId == bookId && p->memberId == memberId) return 1;
    return 0;
}

static void qClear(Queue *q)
{
    Reservation *p = q->front;
    while (p) { Reservation *nx = p->next; free(p); p = nx; }
    q->front = q->rear = NULL;
    q->size = 0;
}

static void pushAction(ActType t, int bookId, int memberId, const Book *snap)
{
    Action *a = xmalloc(sizeof *a);
    a->type = t; a->bookId = bookId; a->memberId = memberId;
    if (snap) a->snap = *snap; else memset(&a->snap, 0, sizeof a->snap);
    a->next = undoTop;
    undoTop = a;
}

static Action *popAction(void)
{
    Action *a = undoTop;
    if (a) { undoTop = a->next; a->next = NULL; }
    return a;
}

static void clearStack(void)
{
    Action *a;
    while ((a = popAction()) != NULL) free(a);
}

static const char *actionName(ActType t)
{
    switch (t) {
        case ACT_ADD:    return "ADD BOOK";
        case ACT_DELETE: return "DELETE BOOK";
        case ACT_BORROW: return "BORROW";
        case ACT_RETURN: return "RETURN";
    }
    return "?";
}

static void addBookEverywhere(Book *b)
{
    listAppend(b);
    root = bstInsert(root, b);
    htInsert(&ht, b);
}

static int removeBookEverywhere(int id)
{
    Book *b = listRemove(id);
    if (!b) return 0;
    root = bstDelete(root, id);
    htDelete(&ht, id);
    qRemoveBook(&resQ, id);
    free(b);
    return 1;
}

static void clearAll(void)
{
    Book *p = head;
    while (p) { Book *nx = p->next; free(p); p = nx; }
    head = NULL;
    bstFree(root);
    root = NULL;
    htFree(&ht);
    htInit(&ht, INITIAL_TABLE);
    qClear(&resQ);
    clearStack();
    memberCount = 0;
}

static Member *findMember(int id)
{
    for (int i = 0; i < memberCount; i++)
        if (members[i].id == id) return &members[i];
    return NULL;
}

static int memberHolds(const Member *m, int bookId)
{
    for (int i = 0; i < m->count; i++)
        if (m->borrowed[i] == bookId) return i;
    return -1;
}

static void issueBook(Member *m, Book *b)
{
    b->available--;
    m->borrowed[m->count++] = b->id;
}

static void takeBack(Member *m, Book *b)
{
    int i = memberHolds(m, b->id);
    for (; i < m->count - 1; i++) m->borrowed[i] = m->borrowed[i + 1];
    m->count--;
    b->available++;
}

static void serveReservations(Book *b)
{
    while (b->available > 0) {
        Reservation *r = qTakeFirstForBook(&resQ, b->id);
        if (!r) break;
        Member *m = findMember(r->memberId);
        if (!m) { free(r); continue; }
        if (m->count >= MAX_BORROW || memberHolds(m, b->id) >= 0) {
            printf("Reservation for '%s' by %s skipped (limit reached / already holds it).\n",
                   b->title, m->name);
            free(r);
            continue;
        }
        issueBook(m, b);
        pushAction(ACT_BORROW, b->id, m->id, NULL);
        printf("Reservation served: '%s' issued to %s (member %d).\n",
               b->title, m->name, m->id);
        free(r);
    }
}

static void processAllReservations(void)
{
    if (resQ.size == 0) { puts("The reservation queue is empty."); return; }
    int before = resQ.size;
    for (Book *p = head; p; p = p->next)
        if (p->available > 0) serveReservations(p);
    printf("Queue processed. %d reservation(s) served or cleared, %d still waiting.\n",
           before - resQ.size, resQ.size);
}

static void addMember(void)
{
    if (memberCount >= MAX_MEMBERS) { puts("Member limit reached."); return; }
    int maxId = 1000;
    for (int i = 0; i < memberCount; i++)
        if (members[i].id > maxId) maxId = members[i].id;
    Member *m = &members[memberCount++];
    memset(m, 0, sizeof *m);
    m->id = maxId + 1;
    readText("Member name: ", m->name, NAME_LEN);
    printf("Member added. Member ID = %d\n", m->id);
}

static void listMembers(void)
{
    if (memberCount == 0) { puts("No members registered."); return; }
    printf("\n%-8s %-25s %s\n", "ID", "Name", "Books held");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < memberCount; i++) {
        printf("%-8d %-25.25s ", members[i].id, members[i].name);
        if (members[i].count == 0) printf("-");
        for (int j = 0; j < members[i].count; j++) {
            Book *b = htSearch(&ht, members[i].borrowed[j], NULL);
            printf("%s%d(%.15s)", j ? ", " : "", members[i].borrowed[j], b ? b->title : "?");
        }
        printf("\n");
    }
}

static void reserveFor(Member *m, Book *b)
{
    if (memberHolds(m, b->id) >= 0) { puts("This member already holds the book."); return; }
    if (qContains(&resQ, b->id, m->id)) { puts("This member has already reserved the book."); return; }
    qEnqueue(&resQ, b->id, m->id);
    printf("Reserved '%s' for %s. Position in queue overall: %d\n", b->title, m->name, resQ.size);
}

static void borrowBook(void)
{
    int mid = readInt("Member ID: ", 1, 999999);
    Member *m = findMember(mid);
    if (!m) { puts("No such member."); return; }
    int id = readInt("Book ID: ", 1, 999999);
    Book *b = htSearch(&ht, id, NULL);
    if (!b) { puts("No such book."); return; }
    if (memberHolds(m, id) >= 0) { puts("This member already holds this book."); return; }
    if (m->count >= MAX_BORROW) { printf("Borrow limit (%d) reached.\n", MAX_BORROW); return; }
    if (b->available == 0) {
        puts("No copies available right now.");
        if (askYesNo("Reserve it instead? (y/n): ")) reserveFor(m, b);
        return;
    }
    issueBook(m, b);
    pushAction(ACT_BORROW, id, mid, NULL);
    printf("'%s' issued to %s. Copies left: %d\n", b->title, m->name, b->available);
}

static void returnBook(void)
{
    int mid = readInt("Member ID: ", 1, 999999);
    Member *m = findMember(mid);
    if (!m) { puts("No such member."); return; }
    int id = readInt("Book ID: ", 1, 999999);
    Book *b = htSearch(&ht, id, NULL);
    if (!b) { puts("No such book."); return; }
    if (memberHolds(m, id) < 0) { puts("This member has not borrowed that book."); return; }
    takeBack(m, b);
    pushAction(ACT_RETURN, id, mid, NULL);
    printf("'%s' returned by %s.\n", b->title, m->name);
    serveReservations(b);
}

static void reserveBook(void)
{
    int mid = readInt("Member ID: ", 1, 999999);
    Member *m = findMember(mid);
    if (!m) { puts("No such member."); return; }
    int id = readInt("Book ID: ", 1, 999999);
    Book *b = htSearch(&ht, id, NULL);
    if (!b) { puts("No such book."); return; }
    if (b->available > 0) {
        printf("%d copy(ies) are available. Please borrow the book instead.\n", b->available);
        return;
    }
    reserveFor(m, b);
}

static void showReservations(void)
{
    if (resQ.size == 0) { puts("The reservation queue is empty."); return; }
    printf("\nReservation queue (front -> rear)\n");
    printf("%-4s %-8s %-30s %-8s %s\n", "No.", "BookID", "Title", "Member", "Name");
    printf("------------------------------------------------------------------------\n");
    int n = 1;
    for (Reservation *r = resQ.front; r; r = r->next) {
        Book *b = htSearch(&ht, r->bookId, NULL);
        Member *m = findMember(r->memberId);
        printf("%-4d %-8d %-30.30s %-8d %s\n", n++, r->bookId,
               b ? b->title : "?", r->memberId, m ? m->name : "?");
    }
}

static void addBook(void)
{
    int id = readInt("Book ID (1-999999): ", 1, 999999);
    if (htSearch(&ht, id, NULL)) { puts("A book with this ID already exists."); return; }
    Book *b = xmalloc(sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = id;
    readText("Title : ", b->title, STR_LEN);
    readText("Author: ", b->author, STR_LEN);
    b->total = readInt("Number of copies (1-1000): ", 1, 1000);
    b->available = b->total;
    addBookEverywhere(b);
    pushAction(ACT_ADD, id, 0, NULL);
    puts("Book added to the linked list, BST and hash table.");
}

static void displayBooks(void)
{
    if (!head) { puts("No books in the catalogue."); return; }
    printHeader();
    int n = 0;
    for (Book *p = head; p; p = p->next) { printBook(p); n++; }
    printf("Total titles: %d\n", n);
}

static void updateBook(void)
{
    int id = readInt("Book ID to update: ", 1, 999999);
    Book *b = htSearch(&ht, id, NULL);
    if (!b) { puts("No such book."); return; }
    printHeader(); printBook(b);
    puts("\n1. Title\n2. Author\n3. Total copies");
    int c = readInt("What to update? ", 1, 3);
    if (c == 1) {
        readText("New title : ", b->title, STR_LEN);
    } else if (c == 2) {
        readText("New author: ", b->author, STR_LEN);
    } else {
        int out = b->total - b->available;
        int nt = readInt("New total copies (1-1000): ", 1, 1000);
        if (nt < out) {
            printf("Cannot go below %d: that many copies are on loan.\n", out);
            return;
        }
        b->total = nt;
        b->available = nt - out;
        serveReservations(b);
    }
    puts("Record updated.");
}

static void deleteBook(void)
{
    int id = readInt("Book ID to delete: ", 1, 999999);
    Book *b = htSearch(&ht, id, NULL);
    if (!b) { puts("No such book."); return; }
    if (b->available != b->total) {
        printf("Cannot delete: %d copy(ies) are currently borrowed.\n", b->total - b->available);
        return;
    }
    Book snap = *b;
    if (!askYesNo("Delete this book from all structures? (y/n): ")) return;
    removeBookEverywhere(id);
    pushAction(ACT_DELETE, id, 0, &snap);
    puts("Book deleted from the linked list, BST and hash table.");
}

static Book **buildArray(int *n)
{
    int c = 0;
    for (Book *p = head; p; p = p->next) c++;
    Book **arr = xmalloc(sizeof(Book *) * (size_t)(c ? c : 1));
    int i = 0;
    for (Book *p = head; p; p = p->next) arr[i++] = p;
    *n = c;
    return arr;
}

static int cmpId(const void *a, const void *b)
{
    const Book *x = *(Book *const *)a, *y = *(Book *const *)b;
    return (x->id > y->id) - (x->id < y->id);
}

static int cmpTitle(const void *a, const void *b)
{
    return ciCompare((*(Book *const *)a)->title, (*(Book *const *)b)->title);
}

static int cmpAuthor(const void *a, const void *b)
{
    return ciCompare((*(Book *const *)a)->author, (*(Book *const *)b)->author);
}

static void sortBooks(void)
{
    if (!head) { puts("No books to sort."); return; }
    puts("\n1. Sort by ID\n2. Sort by Title\n3. Sort by Author");
    int c = readInt("Choice: ", 1, 3);
    int n;
    Book **arr = buildArray(&n);
    qsort(arr, (size_t)n, sizeof(Book *), c == 1 ? cmpId : c == 2 ? cmpTitle : cmpAuthor);
    printHeader();
    for (int i = 0; i < n; i++) printBook(arr[i]);
    free(arr);
}

static void searchBooks(void)
{
    puts("\n1. Linear search by ID (linked list)");
    puts("2. Linear search by title (keyword)");
    puts("3. Linear search by author (keyword)");
    puts("4. Binary search by ID (sorted array)");
    puts("5. BST search by ID");
    puts("6. Hash table lookup by ID");
    int c = readInt("Choice: ", 1, 6);
    long cmp = 0;
    clock_t s;

    if (c == 2 || c == 3) {
        char key[STR_LEN];
        readText("Keyword: ", key, STR_LEN);
        int found = 0;
        for (Book *p = head; p; p = p->next) {
            if (ciContains(c == 2 ? p->title : p->author, key)) {
                if (!found) printHeader();
                printBook(p);
                found++;
            }
        }
        if (!found) puts("No matching books found.");
        else printf("%d match(es) found.\n", found);
        return;
    }

    int id = readInt("Book ID: ", 1, 999999);
    Book *b = NULL;
    s = clock();
    if (c == 1) {
        b = listFindIn(head, id, &cmp);
    } else if (c == 4) {
        int n;
        Book **arr = buildArray(&n);
        qsort(arr, (size_t)n, sizeof(Book *), cmpId);
        int lo = 0, hi = n - 1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            cmp++;
            if (arr[mid]->id == id) { b = arr[mid]; break; }
            if (arr[mid]->id < id) lo = mid + 1; else hi = mid - 1;
        }
        free(arr);
    } else if (c == 5) {
        BSTNode *nd = bstSearch(root, id, &cmp);
        b = nd ? nd->book : NULL;
    } else {
        b = htSearch(&ht, id, &cmp);
    }
    double us = 1e6 * (double)(clock() - s) / CLOCKS_PER_SEC;

    if (b) { printHeader(); printBook(b); }
    else puts("Book not found.");
    printf("Comparisons made: %ld   (time %.1f microseconds)\n", cmp, us);
    if (c == 4) puts("(binary search needs sorted data, so the array was built and sorted first)");
}

static void bstMenu(void)
{
    for (;;) {
        puts("\n--- BST (Book-ID index) ---");
        puts("1. Inorder traversal (sorted IDs)");
        puts("2. Preorder traversal");
        puts("3. Postorder traversal");
        puts("4. Search an ID");
        puts("5. Show tree shape and height");
        puts("0. Back");
        puts("(Insert and delete happen automatically via Add Book / Delete Book.)");
        int c = readInt("Choice: ", 0, 5);
        if (c == 0) return;
        if (!root && c != 4) { puts("The BST is empty."); continue; }
        if (c == 1) { puts("Inorder:");   bstInorder(root); }
        else if (c == 2) { puts("Preorder:");  bstPreorder(root); }
        else if (c == 3) { puts("Postorder:"); bstPostorder(root); }
        else if (c == 4) {
            int id = readInt("Book ID: ", 1, 999999);
            long cmp = 0;
            BSTNode *n = bstSearch(root, id, &cmp);
            if (n) { printHeader(); printBook(n->book); }
            else puts("Not found in the BST.");
            printf("Comparisons: %ld\n", cmp);
        } else {
            printf("Height = %d\n\n(root is at the left, right subtree on top)\n\n", bstHeight(root));
            bstPrintTree(root, 0);
        }
    }
}

static void hashMenu(void)
{
    for (;;) {
        puts("\n--- Hash table (index = ID % size) ---");
        puts("1. Display table");
        puts("2. Search an ID");
        puts("3. Rehash now (grow the table)");
        puts("0. Back");
        puts("(Insert and delete happen automatically via Add Book / Delete Book.)");
        int c = readInt("Choice: ", 0, 3);
        if (c == 0) return;
        if (c == 1) htDisplay(&ht);
        else if (c == 2) {
            int id = readInt("Book ID: ", 1, 999999);
            long probes = 0;
            Book *b = htSearch(&ht, id, &probes);
            printf("Bucket index = %d %% %d = %d\n", id, ht.size, id % ht.size);
            if (b) { printHeader(); printBook(b); }
            else puts("Not found in the hash table.");
            printf("Chain nodes checked: %ld\n", probes);
        } else {
            int old = ht.size;
            htRehash(&ht);
            printf("Rehashed: size %d -> %d\n", old, ht.size);
        }
    }
}

static double msSince(clock_t s)
{
    return 1000.0 * (double)(clock() - s) / CLOCKS_PER_SEC;
}

static void report(const char *name, const char *bigO, double ms, int k, long found)
{
    printf("%-28s %-9s %10.3f %14.3f %8ld\n", name, bigO, ms, ms * 1000.0 / k, found);
}

static void performanceTest(void)
{
    int n = readInt("Number of records to generate (100-100000): ", 100, 100000);
    int k = PERF_LOOKUPS;

    Book *pool = xmalloc(sizeof(Book) * (size_t)n);
    int *ids = xmalloc(sizeof(int) * (size_t)n);
    int *keys = xmalloc(sizeof(int) * (size_t)k);
    Book **arr = xmalloc(sizeof(Book *) * (size_t)n);

    for (int i = 0; i < n; i++) ids[i] = i + 1;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)randBelow((unsigned long)i + 1);
        int t = ids[i]; ids[i] = ids[j]; ids[j] = t;
    }

    Book *lhead = NULL, *ltail = NULL;
    BSTNode *broot = NULL;
    HashTable t;
    htInit(&t, INITIAL_TABLE);

    memset(pool, 0, sizeof(Book) * (size_t)n);
    for (int i = 0; i < n; i++) {
        Book *b = &pool[i];
        b->id = ids[i];
        snprintf(b->title, STR_LEN, "Book %d", b->id);
        if (!lhead) lhead = ltail = b; else { ltail->next = b; ltail = b; }
    }

    clock_t s = clock();
    for (Book *p = lhead; p; p = p->next) broot = bstInsert(broot, p);
    double tBst = msSince(s);

    s = clock();
    for (Book *p = lhead; p; p = p->next) htInsert(&t, p);
    double tHash = msSince(s);

    for (int i = 0; i < n; i++) arr[i] = &pool[i];
    s = clock();
    qsort(arr, (size_t)n, sizeof(Book *), cmpId);
    double tSort = msSince(s);

    for (int j = 0; j < k; j++) keys[j] = (int)randBelow((unsigned long)n) + 1;

    long f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0;
    double m1, m2, m3, m4, m5;

    s = clock();
    for (int j = 0; j < k; j++) if (listFindIn(lhead, keys[j], NULL)) f1++;
    m1 = msSince(s);

    s = clock();
    for (int j = 0; j < k; j++)
        for (int i = 0; i < n; i++)
            if (pool[i].id == keys[j]) { f2++; break; }
    m2 = msSince(s);

    s = clock();
    for (int j = 0; j < k; j++) {
        int lo = 0, hi = n - 1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            if (arr[mid]->id == keys[j]) { f3++; break; }
            if (arr[mid]->id < keys[j]) lo = mid + 1; else hi = mid - 1;
        }
    }
    m3 = msSince(s);

    s = clock();
    for (int j = 0; j < k; j++) if (bstSearch(broot, keys[j], NULL)) f4++;
    m4 = msSince(s);

    s = clock();
    for (int j = 0; j < k; j++) if (htSearch(&t, keys[j], NULL)) f5++;
    m5 = msSince(s);

    printf("\nPerformance test: %d records, %d random searches per method\n", n, k);
    printf("\nBuild times : BST insert %.3f ms | hash insert (with rehashing) %.3f ms | sort by ID %.3f ms\n",
           tBst, tHash, tSort);
    printf("BST height  : %d   Hash table size: %d (load %.2f)\n\n",
           bstHeight(broot), t.size, (double)t.count / t.size);
    printf("%-28s %-9s %10s %14s %8s\n", "Method", "Expected", "Total ms", "us / search", "Found");
    printf("-----------------------------------------------------------------------\n");
    report("Linear search (linked list)", "O(n)",     m1, k, f1);
    report("Linear search (array)",       "O(n)",     m2, k, f2);
    report("Binary search (array)",       "O(log n)", m3, k, f3);
    report("BST search",                  "O(log n)", m4, k, f4);
    report("Hash table lookup",           "O(1)",     m5, k, f5);
    puts("\n(Times depend on the machine; the ordering of the methods is what matters.)");

    bstFree(broot);
    htFree(&t);
    free(pool); free(ids); free(keys); free(arr);
}

static void showRecent(void)
{
    if (!undoTop) { puts("No recent operations."); return; }
    puts("\nRecent operations (most recent first, up to 10):");
    int n = 0;
    for (Action *a = undoTop; a && n < 10; a = a->next, n++) {
        printf("  %2d. %-12s book %d", n + 1, actionName(a->type), a->bookId);
        if (a->type == ACT_BORROW || a->type == ACT_RETURN) printf("  member %d", a->memberId);
        printf("\n");
    }
}

static void undoLast(void)
{
    Action *a = popAction();
    if (!a) { puts("Nothing to undo."); return; }
    Book *b = htSearch(&ht, a->bookId, NULL);
    Member *m = findMember(a->memberId);

    printf("Undoing: %s (book %d)\n", actionName(a->type), a->bookId);
    switch (a->type) {
    case ACT_ADD:
        if (b && b->available == b->total) {
            removeBookEverywhere(a->bookId);
            puts("The added book has been removed again.");
        } else puts("Cannot undo: the book is missing or has copies on loan.");
        break;
    case ACT_DELETE:
        if (!b) {
            Book *nb = xmalloc(sizeof *nb);
            *nb = a->snap;
            nb->next = NULL;
            addBookEverywhere(nb);
            puts("The deleted book has been restored (its reservations are not restored).");
        } else puts("Cannot undo: a book with that ID exists now.");
        break;
    case ACT_BORROW:
        if (b && m && memberHolds(m, b->id) >= 0) {
            takeBack(m, b);
            printf("'%s' taken back from %s.\n", b->title, m->name);
            serveReservations(b);
        } else puts("Cannot undo: the member no longer holds this book.");
        break;
    case ACT_RETURN:
        if (b && m && b->available > 0 && m->count < MAX_BORROW && memberHolds(m, b->id) < 0) {
            issueBook(m, b);
            printf("'%s' re-issued to %s.\n", b->title, m->name);
        } else puts("Cannot undo: the copy was reissued or the limit would be exceeded.");
        break;
    }
    free(a);
}

static int saveData(const char *fname)
{
    FILE *f = fopen(fname, "w");
    if (!f) { printf("Could not open '%s' for writing.\n", fname); return 0; }

    int nb = 0;
    for (Book *p = head; p; p = p->next) nb++;
    fprintf(f, "BOOKS %d\n", nb);
    for (Book *p = head; p; p = p->next)
        fprintf(f, "%d|%s|%s|%d|%d\n", p->id, p->title, p->author, p->total, p->available);

    fprintf(f, "MEMBERS %d\n", memberCount);
    for (int i = 0; i < memberCount; i++) {
        fprintf(f, "%d|%s|%d|", members[i].id, members[i].name, members[i].count);
        for (int j = 0; j < members[i].count; j++) fprintf(f, "%d ", members[i].borrowed[j]);
        fprintf(f, "\n");
    }

    fprintf(f, "RESERVATIONS %d\n", resQ.size);
    for (Reservation *r = resQ.front; r; r = r->next)
        fprintf(f, "%d %d\n", r->bookId, r->memberId);

    if (fclose(f) != 0) { puts("Error while writing the file."); return 0; }
    return 1;
}

static int loadData(const char *fname, int quiet)
{
    FILE *f = fopen(fname, "r");
    if (!f) {
        if (!quiet) printf("Could not open '%s'.\n", fname);
        return 0;
    }

    char line[512], title[STR_LEN], author[STR_LEN], name[NAME_LEN];
    int nb = 0, nm = 0, nr = 0;

    clearAll();

    if (!fgets(line, sizeof line, f) || sscanf(line, "BOOKS %d", &nb) != 1 || nb < 0) goto bad;
    for (int i = 0; i < nb; i++) {
        int id, total, avail;
        if (!fgets(line, sizeof line, f) ||
            sscanf(line, "%d|%99[^|]|%99[^|]|%d|%d", &id, title, author, &total, &avail) != 5 ||
            id < 1 || total < 1 || avail < 0 || avail > total || htSearch(&ht, id, NULL))
            goto bad;
        Book *b = xmalloc(sizeof *b);
        b->id = id; b->total = total; b->available = avail; b->next = NULL;
        strcpy(b->title, title);
        strcpy(b->author, author);
        addBookEverywhere(b);
    }

    if (!fgets(line, sizeof line, f) || sscanf(line, "MEMBERS %d", &nm) != 1 ||
        nm < 0 || nm > MAX_MEMBERS) goto bad;
    for (int i = 0; i < nm; i++) {
        int id, cnt, pos = 0;
        if (!fgets(line, sizeof line, f) ||
            sscanf(line, "%d|%49[^|]|%d|%n", &id, name, &cnt, &pos) != 3 ||
            pos == 0 || cnt < 0 || cnt > MAX_BORROW)
            goto bad;
        Member *m = &members[memberCount++];
        memset(m, 0, sizeof *m);
        m->id = id;
        strcpy(m->name, name);
        char *p = line + pos;
        for (int j = 0; j < cnt; j++) {
            char *end;
            long v = strtol(p, &end, 10);
            if (end == p || !htSearch(&ht, (int)v, NULL)) goto bad;
            m->borrowed[m->count++] = (int)v;
            p = end;
        }
    }

    if (!fgets(line, sizeof line, f) || sscanf(line, "RESERVATIONS %d", &nr) != 1 || nr < 0) goto bad;
    for (int i = 0; i < nr; i++) {
        int bid, mid;
        if (!fgets(line, sizeof line, f) || sscanf(line, "%d %d", &bid, &mid) != 2 ||
            !htSearch(&ht, bid, NULL) || !findMember(mid))
            goto bad;
        qEnqueue(&resQ, bid, mid);
    }

    fclose(f);
    printf("Loaded %d book(s), %d member(s), %d reservation(s) from '%s'.\n", nb, nm, nr, fname);
    return 1;

bad:
    fclose(f);
    clearAll();
    printf("'%s' is not a valid library file. Nothing was loaded.\n", fname);
    return 0;
}

static void bookMenu(void)
{
    for (;;) {
        puts("\n--- Book management ---");
        puts("1. Add book");
        puts("2. Display all books");
        puts("3. Update book");
        puts("4. Delete book");
        puts("0. Back");
        switch (readInt("Choice: ", 0, 4)) {
            case 1: addBook(); break;
            case 2: displayBooks(); break;
            case 3: updateBook(); break;
            case 4: deleteBook(); break;
            case 0: return;
        }
    }
}

static void circulationMenu(void)
{
    for (;;) {
        puts("\n--- Borrow / Return / Reserve ---");
        puts("1. Borrow a book");
        puts("2. Return a book");
        puts("3. Reserve a book");
        puts("4. Show reservation queue");
        puts("5. Process reservation queue");
        puts("6. Add member");
        puts("7. List members and their books");
        puts("0. Back");
        switch (readInt("Choice: ", 0, 7)) {
            case 1: borrowBook(); break;
            case 2: returnBook(); break;
            case 3: reserveBook(); break;
            case 4: showReservations(); break;
            case 5: processAllReservations(); break;
            case 6: addMember(); break;
            case 7: listMembers(); break;
            case 0: return;
        }
    }
}

static void fileMenu(void)
{
    puts("\n1. Save records to file");
    puts("2. Load records from file (replaces current data)");
    puts("0. Back");
    int c = readInt("Choice: ", 0, 2);
    if (c == 1) {
        if (saveData(DATA_FILE)) printf("Saved to '%s'.\n", DATA_FILE);
    } else if (c == 2) {
        if (askYesNo("This replaces all current data. Continue? (y/n): "))
            loadData(DATA_FILE, 0);
    }
}

int main(void)
{
    srand((unsigned)time(NULL));
    htInit(&ht, INITIAL_TABLE);

    puts("==============================================");
    puts("        LIBRARY MANAGEMENT SYSTEM");
    puts("==============================================");
    loadData(DATA_FILE, 1);

    for (;;) {
        puts("\n=========== MAIN MENU ===========");
        puts("1. Book management (add/display/update/delete)");
        puts("2. Search books");
        puts("3. Sort books");
        puts("4. Borrow / Return / Reserve");
        puts("5. BST operations and traversals");
        puts("6. Hash table operations");
        puts("7. Data structure performance test");
        puts("8. Recent operations");
        puts("9. Undo last operation");
        puts("10. Save / Load records");
        puts("0. Exit");
        switch (readInt("Enter choice: ", 0, 10)) {
            case 1:  bookMenu(); break;
            case 2:  searchBooks(); break;
            case 3:  sortBooks(); break;
            case 4:  circulationMenu(); break;
            case 5:  bstMenu(); break;
            case 6:  hashMenu(); break;
            case 7:  performanceTest(); break;
            case 8:  showRecent(); break;
            case 9:  undoLast(); break;
            case 10: fileMenu(); break;
            case 0:
                if (askYesNo("Save records before exiting? (y/n): "))
                    if (saveData(DATA_FILE)) printf("Saved to '%s'.\n", DATA_FILE);
                clearAll();
                htFree(&ht);
                puts("Goodbye!");
                return 0;
        }
    }
}
