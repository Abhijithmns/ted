# Gap Buffers: 

## What is a Gap Buffer

A gap buffer is a dynamic array with a hole in it. That hole is called the gap, and it sits at the cursor position. The entire data structure is just one contiguous block of memory, split conceptually into three regions: the text before the cursor, the gap itself, and the text after the cursor. Everything is stored in a single flat array, and the gap is represented by two indices — where the gap starts and where it ends.

```
[ H e l l o _ _ _ _ _ _ W o r l d ]
  0 1 2 3 4 5 6 7 8 9 10 11 ...
            ↑ gap_start  ↑ gap_end
```

The text the user sees is everything before `gap_start` concatenated with everything from `gap_end` onward. The bytes inside the gap are garbage — they don't mean anything, they're just unused space.

---

## The Core Insight

The fundamental observation that motivates gap buffers is that text editing is almost always local. You open a file, navigate somewhere, and then type for a while. Then you navigate somewhere else and type again. You rarely jump around constantly, typing only one character per location. This pattern — bursts of typing at a position — is exactly what gap buffers are optimized for. The data structure is essentially designed around human behavior rather than theoretical worst cases.

---

## Internal Representation

The gap buffer is implemented with:

- A flat array of characters (or bytes, or unicode codepoints depending on encoding)
- `gap_start`: index of the first byte of the gap
- `gap_end`: index of the first byte after the gap
- `capacity`: total size of the array

The gap size at any moment is `gap_end - gap_start`. The total text length is `capacity - (gap_end - gap_start)`.

```c
typedef struct {
    char *buf;
    int gap_start;
    int gap_end;
    int capacity;
} GapBuffer;
```

This is the entire data structure. There is nothing else. Its simplicity is a large part of why it's attractive.

---

## Initialization

When you create a gap buffer for a new empty file, you allocate some initial array and set the gap to span the entire thing. The whole buffer is one big gap waiting to be filled.

```
[ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ ]
  ↑ gap_start                     ↑ gap_end
```

When you open an existing file, you load the text into the buffer, place the text at the start, and put the gap at the end — or wherever the initial cursor position is. A common approach is to put the text before the gap and leave the gap at the end, since that means no movement is needed until the user actually moves the cursor.

```
[ H e l l o   W o r l d _ _ _ _ _ ]
                          ↑ gap here, cursor at end
```

---

## Moving the Cursor (Moving the Gap)

This is the most important operation to understand deeply, because every other operation depends on the gap being in the right place.

When the user moves the cursor from position A to position B, you need to physically relocate the gap in the array. The gap must always sit at the cursor. There are two cases:

**Moving right:** The cursor moves right by k positions. You copy k characters from just after the gap to just before the gap, and advance both `gap_start` and `gap_end` by k.

```
Before: [ A B C _ _ _ D E F G ]
                       ↑ copy 'D' leftward

After:  [ A B C D _ _ _ E F G ]
```

**Moving left:** The cursor moves left by k positions. You copy k characters from just before the gap to just after the gap, and retreat both `gap_start` and `gap_end` by k.

```
Before: [ A B C _ _ _ D E F G ]
              ↑ copy 'C' rightward

After:  [ A B _ _ _ C D E F G ]
```

In both cases the operation is a `memmove` of k bytes, which is O(k) in complexity. Since this is a contiguous block copy, the CPU handles it extremely efficiently — modern hardware with SIMD instructions can move 32 or 64 bytes per clock cycle, so even moving thousands of characters is imperceptibly fast in wall clock terms.

The worst case is jumping from one end of the document to the other — say from position 0 to position 1,000,000 — which copies every character in the buffer once. For a 1MB file that still takes well under a millisecond. For a 100MB file it might become noticeable, and this is where gap buffers start to show their limits.

---

## Insertion

Once the gap is positioned at the cursor, insertion is trivial. To insert a character, you write it into `buf[gap_start]` and increment `gap_start` by one. The gap shrinks by one. That's a single array write. O(1).

```
Gap here, type 'X':

Before: [ A B _ _ _ C D ]
                gap_start = 2

After:  [ A B X _ _ C D ]
                gap_start = 3
```

Inserting a string of k characters is k such operations in sequence — O(k) total, but with no overhead per character beyond the write itself. No shifting, no allocation, no pointer chasing.

The only catch is that the gap must be big enough. If the gap has shrunk to zero — you've typed enough characters to fill it — you need to grow the buffer.

---

## Deletion

There are two kinds of deletion: backspace (delete character before cursor) and delete (delete character after cursor).

**Backspace:** Decrement `gap_start` by one. The character that was just before the gap is now inside the gap, effectively deleted. O(1). Nothing is actually zeroed out or freed — the byte just becomes part of the gap and will be overwritten eventually.

```
Before: [ A B X _ _ C D ]   gap_start = 3
Backspace:
After:  [ A B _ _ _ C D ]   gap_start = 2
```

**Forward delete:** Increment `gap_end` by one. The character that was just after the gap is now inside the gap. O(1).

```
Before: [ A B _ _ _ C D ]   gap_end = 5
Delete:
After:  [ A B _ _ _ _ D ]   gap_end = 6
```

Both operations are just index adjustments. This is about as fast as any operation can be.

---

## Growing the Buffer

When the gap shrinks to zero and the user types another character, you need more space. The standard approach mirrors dynamic array growth — allocate a new buffer of roughly double the current capacity, copy the text before the gap, leave a new larger gap, then copy the text after the gap.

```
Old: [ A B C D E F ]   gap exhausted (gap_start == gap_end)

New: [ A B C _ _ _ _ _ _ _ _ _ D E F ]   new gap in the middle
```

This is O(n) where n is the total text length, because you copy everything. But you double the buffer each time, so the amortized cost per insertion over a sequence of insertions is O(1). This is the same analysis as a dynamic array / `std::vector` growth strategy.

The gap size after regrowth is typically set to some heuristic — either a fixed chunk size like 4KB, or proportional to the total buffer size, or based on recent typing speed. The idea is that if you're actively typing, you'll probably type more, so give yourself a comfortable gap to work into.

---

## Selecting Text

Selection is just two indices into the logical text — a selection start and a selection end. The gap buffer itself doesn't change when you select text. You simply track which region of logical text is selected.

The complication is that the selection indices are in logical space (as if the gap didn't exist), but the actual data lives in physical space (with the gap in the middle). Converting between logical and physical positions requires accounting for the gap:

```
physical_index(logical_pos) =
    if logical_pos < gap_start:
        logical_pos
    else:
        logical_pos + (gap_end - gap_start)
```

Whenever you render the text or operate on a selection, you need to be careful about whether you're working in logical or physical coordinates. This is one of the small but real sources of bugs in gap buffer implementations.

---

## Rendering / Reading the Text

To display the text, you read two slices of the array and concatenate them visually:

- Everything from index 0 to `gap_start - 1` (text before cursor)
- Everything from index `gap_end` to `capacity - 1` (text after cursor)

You never expose the gap contents to the renderer. Most rendering systems just treat this as two separate buffers to draw in sequence. Since both slices are contiguous memory, passing them to a renderer is very cache-friendly — no pointer chasing, no fragmented reads.

---

## Undo and Redo

Undo is not handled by the gap buffer itself — it's a layer on top. The common approaches are:

**Command pattern:** Each editing operation (insert, delete, move) is stored as a reversible command object. Undo pops the command stack and calls each command's inverse. The gap buffer just executes whatever the undo system tells it to.

**Snapshot approach:** Periodically save the entire buffer state. Cheap to implement but memory-intensive.

**Piece tables as an alternative:** Some editors use a piece table specifically because its append-only nature makes undo history essentially free — you just keep all the original pieces and new pieces, and undo means pointing back to an earlier configuration. Gap buffers don't have this property natively.

In practice, most gap buffer based editors implement a simple command stack with the gap buffer as the underlying store.

---

## Handling Multiple Cursors

Multiple cursors are a significant complication for gap buffers. The gap buffer is inherently designed around one cursor, since the gap sits at one position. With multiple cursors, you have a few options:

**One gap buffer, virtual cursors:** Keep one real gap at the primary cursor, and track secondary cursors as logical indices. Insertions at secondary cursor positions require moving the gap each time, which is expensive if cursors are far apart. This is manageable if secondary cursors are few and close together.

**Multiple gap buffers:** Split the document into multiple gap buffers, one per cursor region. This gets complicated quickly and is rarely done.

**Switch to a different structure:** Many editors that support multiple cursors well (like VS Code) use a piece table instead, which handles arbitrary concurrent edit points more naturally.

---

## Unicode and Encoding

A gap buffer of bytes works fine for ASCII, but real text is Unicode. There are a few considerations:

**UTF-8:** The buffer stores raw UTF-8 bytes. Characters are variable width — 1 to 4 bytes each. This means "move cursor one character" is not the same as "move gap one byte." You need to scan forward or backward to find character boundaries. Moving left by one character means scanning backward to find the start of the previous UTF-8 sequence, which is O(bytes per character) but bounded by 4.

**UTF-16 / UTF-32:** Some editors store UTF-16 or UTF-32 internally to make character-level indexing simpler. UTF-32 gives you fixed-width codepoints so cursor movement in codepoints is always O(1), at the cost of 4x memory usage for ASCII-heavy text.

**Grapheme clusters:** Even codepoints aren't the same as what users perceive as "one character." An emoji with a skin tone modifier is two codepoints but one visual character. Handling this correctly requires a Unicode segmentation library on top of whatever your buffer stores.

Most practical gap buffer implementations store UTF-8 bytes and handle unicode boundaries carefully at the cursor movement layer.

---

## Line Tracking

Gap buffers have no built-in concept of lines. To jump to line N, you'd have to scan through the buffer counting newline characters — O(n). For large files this is unacceptable.

The solution is to maintain a separate line index: an array of offsets (in logical coordinates) where each line starts. When you insert a newline character, you update the line index. When you delete one, same. This index lets you jump to any line in O(log n) with binary search, at the cost of maintaining it on every edit.

Some editors use a more sophisticated structure like a piece table with a line-span tree on top, but even a simple sorted array of newline positions works well for moderate file sizes.

---

## Comparison with Other Structures

**Plain array:** Simple but O(n) per insertion anywhere except the end. Fine for tiny files.

**Linked list of characters:** O(1) insert and delete at any position once you have a pointer there, but terrible cache behavior due to pointer chasing, and O(n) to find any position. Basically never used.

**Rope:** A balanced binary tree of string chunks. O(log n) for insert, delete, and cursor movement regardless of position or pattern. Better than a gap buffer for very large files or heavy random access, but more complex to implement and has higher constant factors due to tree overhead and worse cache locality.

**Piece table:** Stores the original file and an append-only add buffer, with a table of "pieces" describing how to reconstruct the current text. O(1) insert and delete (appending to the add buffer and updating the piece table), O(log n) for random access with a tree-structured piece table. Naturally supports undo, handles multiple cursors well, and is used in VS Code. More complex than a gap buffer.

**Gap buffer sweet spot:** Simple to implement correctly, excellent cache behavior, O(1) amortized inserts for typical editing patterns, and fast enough for files up to tens of megabytes. The right choice for most editors that don't need to handle enormous files or exotic editing patterns.

---

## Where Gap Buffers Are Used

Emacs uses a gap buffer as its core buffer representation and has since the 1970s. It's one of the longest-running real-world validations of the data structure. Vim historically used a similar structure internally. Many smaller editors and editor components use gap buffers for exactly the reasons above — simplicity and performance for the common case.

---

## Summary of Operation Complexities

| Operation | Complexity | Notes |
|---|---|---|
| Insert at cursor | O(1) | Single array write |
| Delete at cursor | O(1) | Single index adjustment |
| Move cursor by k | O(k) | memcpy of k bytes |
| Move cursor far | O(n) worst case | n = document length |
| Grow buffer | O(n) amortized O(1) | Doubles capacity |
| Jump to line N | O(log n) | With line index, else O(n) |
| Read/render text | O(n) | Two contiguous slices |
| Find position | O(n) | No random access in logical space |

---

## The Honest Tradeoff

The gap buffer is not the theoretically optimal solution for every operation. It is the practically optimal solution for the specific pattern of how humans actually edit text — and that is exactly the right tradeoff for a text editor to make.

The O(1) insertion claim is somewhat misleading in isolation. The full cost of "move cursor somewhere new and type there" is O(n) dominated by the gap move, and the O(1) insertions only matter once the gap is already positioned. The real argument is amortization: you pay O(n) once to move the gap, then get O(1) for every subsequent keystroke at that location. If you type k characters after jumping, the total cost is O(n + k) rather than the O(n*k) you'd pay with a plain array.

If you're jumping around constantly and only typing one character each time, a gap buffer gives you almost no benefit over a plain array. The assumption baked into the design is that real editing doesn't look like that — and for most users, it doesn't.
