# Library Management System

A high-performance, console-based Library Management System implemented in C utilizing core data structures (Singly Linked List, Binary Search Tree, Hash Table with Separate Chaining, FIFO Queue, and Undo Stack).

---

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Data Structures Architecture](#data-structures-architecture)
- [Search & Performance Analysis](#search--performance-analysis)
- [Compilation & Execution](#compilation--execution)
- [Usage & Navigation](#usage--navigation)
- [File Persistence Format](#file-persistence-format)

---

## Overview

This application provides a comprehensive solution for managing a library's book inventory, member database, circulation (borrowing, returning, reservations), and system operations. Designed for efficiency and educational clarity, it integrates multiple distinct data structures that are kept synchronized through a unified consistency model:

- **Single Source of Truth**: The primary record for each book is stored in a singly linked list.
- **Fast Lookups**: Auxiliary index structures (Binary Search Tree and Hash Table) maintain references/pointers to the primary linked list records.
- **Operations & Queues**: Reservations use a FIFO linked queue, while transactions/changes are logged in an undo stack.

---

## Key Features

1. **Book Management**:
   - Add new books with ID, title, author, and number of copies.
   - Display catalog with real-time total and available counts.
   - Update book metadata or copy counts (with checks against active loans).
   - Delete books (ensures all borrowed copies are returned prior to deletion).

2. **Circulation & Member System**:
   - Register library members with distinct member IDs.
   - Borrow books with per-member loan limits (up to 5 books concurrently).
   - Return books with automatic re-stocking and allocation to queued reservations.
   - Reserve out-of-stock books.

3. **FIFO Reservation Queue**:
   - Pending reservations for checked-out books are maintained in a linked queue.
   - As soon as a book is returned, waiting reservations are served in first-come, first-served order.

4. **Multi-Algorithm Search & Sorting**:
   - Search books by ID via:
     - Linear search on Linked List: $\mathcal{O}(n)$
     - Binary search on sorted array: $\mathcal{O}(\log n)$
     - Binary Search Tree lookup: $\mathcal{O}(\log n)$ average
     - Hash table lookup: $\mathcal{O}(1)$ average
   - Keyword search across book titles and authors (case-insensitive substring match).
   - In-memory sorting by ID, Title, or Author using `qsort`.

5. **Binary Search Tree Inspection**:
   - Visual ASCII rendering of tree structure and height calculation.
   - Inorder (sorted by ID), Preorder, and Postorder tree traversals.

6. **Dynamic Hash Table**:
   - Hash table with separate chaining for collision resolution.
   - Automatic rehashing to the next prime size when the load factor exceeds 0.75.
   - Visual inspection tool showing bucket distribution and chain lengths.

7. **Undo & History**:
   - Operation stack recording recent actions (Book Add, Delete, Borrow, Return).
   - One-step undo functionality restoring prior states.

8. **Built-in Performance Benchmark**:
   - Benchmarking suite generating 100 to 100,000 synthetic records.
   - Compares real-world search timings and comparison counts across all supported lookup methods.

9. **File Persistence**:
   - Save and load complete system state (books, members, reservations) to/from disk (`library.txt`).

---

## Data Structures Architecture

| Data Structure | Implementation | Purpose & Complexity |
|---|---|---|
| **Singly Linked List** | Pointer-linked `Book` nodes | Primary record storage for all catalogue entries ($\mathcal{O}(n)$ traversal). |
| **Binary Search Tree** | `BSTNode` storing `Book*` pointer | Secondary index ordered by `id` ($\mathcal{O}(\log n)$ average search/insert/delete). |
| **Hash Table** | Array of buckets with separate chaining (`HNode`) | Direct-access index by `id` ($\mathcal{O}(1)$ average lookup). Rehashes dynamically when load $> 0.75$. |
| **FIFO Queue** | Linked list with `front` and `rear` pointers | Handles book reservations in first-come, first-served order ($\mathcal{O}(1)$ enqueue). |
| **Stack** | Singly linked `Action` nodes (`undoTop`) | Tracks transactional history for undoing recent operations ($\mathcal{O}(1)$ push/pop). |
| **Dynamic Array** | Contiguous `Book*` pointer array | Dynamic snapshots used for multi-field sorting (`qsort`) and binary search. |

---

## Search & Performance Analysis

The built-in benchmark (Menu Option 7) generates random book catalogues and measures the time (in milliseconds and microseconds per search) across 2,000 queries:

| Search Method | Underlying Structure | Time Complexity | Notes |
|---|---|---|---|
| **Linear Search** | Singly Linked List | $\mathcal{O}(n)$ | Traverses node-by-node from head |
| **Linear Search** | Array | $\mathcal{O}(n)$ | Cache-friendly sequential scan |
| **Binary Search** | Sorted Array | $\mathcal{O}(\log n)$ | Requires sorted array; uses divide-and-conquer |
| **BST Search** | Binary Search Tree | $\mathcal{O}(\log n)$ average | Fast hierarchical tree lookup |
| **Hash Table** | Hash Table with Chaining | $\mathcal{O}(1)$ average | Constant-time lookup via `id % tableSize` |

---

## Compilation & Execution

### Prerequisites

- GCC, Clang, or any C99-compliant C compiler.

### Linux / macOS

```bash
# Compile with warnings enabled
gcc -Wall -Wextra -O2 -o library library_management.c

# Run
./library
```

### Windows (MinGW / MSVC)

```powershell
# Using GCC (MinGW)
gcc -Wall -Wextra -O2 -o library.exe library_management.c

# Run
.\library.exe
```

---

## Usage & Navigation

On launch, the system automatically loads data from `library.txt` if present. The interactive main menu provides access to all modules:

```text
=========== MAIN MENU ===========
1. Book management (add/display/update/delete)
2. Search books
3. Sort books
4. Borrow / Return / Reserve
5. BST operations and traversals
6. Hash table operations
7. Data structure performance test
8. Recent operations
9. Undo last operation
10. Save / Load records
0. Exit
```

---

## File Persistence Format

Data is serialized to `library.txt` using a structured pipe-delimited format:

```text
BOOKS <count>
<id>|<title>|<author>|<total_copies>|<available_copies>
...
MEMBERS <count>
<id>|<name>|<borrowed_count>|<book_id_1> <book_id_2> ...
...
RESERVATIONS <count>
<book_id> <member_id>
...
```
