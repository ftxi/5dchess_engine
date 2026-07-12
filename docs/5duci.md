# 5D Universal Chess Interface (5DUCI) (Draft)

**Version 0.3.3**

<sup>*This file is created by [ftxi](https://github.com/ftxi). The protocal described is used experimentally in [https://github.com/ftxi/5dchess_engine](https://github.com/ftxi/5dchess_engine).*</sup>

The **5D Universal Chess Interface (5DUCI)** is a text-based communication protocol between a **5D Chess engine** (hereinafter referred to as the **engine**) and a **5D Chess user interface** (hereinafter referred to as the **UI**).

Its goals are:

1. To allow engines and UIs to be developed independently, enabling frontend/backend separation.
2. To allow different engines to compete against each other through a common interface.

5DUCI is derived from the **Universal Chess Interface (UCI)** standard and references both the **Universal Chess Interface (UCI)** and the **Universal Chinese Chess Interface (UCCI)**.

During the draft phase, since most 5D Chess engines are still in the early stages of development, this specification defines only the fundamental syntax to simplify implementation for engine developers.

For the original UCI and UCCI specifications, see:

* [https://www.wbec-ridderkerk.nl/html/UCIProtocol.html](https://www.wbec-ridderkerk.nl/html/UCIProtocol.html)
* [https://www.xqbase.com/protocol/cchess_ucci.htm](https://www.xqbase.com/protocol/cchess_ucci.htm)


---

# Design Goals

Priority (highest to lowest):

1. Satisfy communication requirements between engines and UIs.
2. Ensure reliable data transmission.
3. Make parsing and implementation easy for engine developers.
4. Keep the protocol human-readable.

---

# Commands

| UI Command                                                                             | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                 | Engine Response                         |
| -------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| `5duci`                                                                                | Notify the engine to begin accepting 5DUCI commands.                                                                                                                                                                                                                                                                                                                                                                                                        | `5duciok`                               |
| `setoption name <key> [value <value>]`                                                 | Set an internal engine option. `<key>` is a single word. `<value>` may be a boolean (`true`/`false`), integer, double-precision floating-point number, string, or omitted.                                                                                                                                                                                                                                                                                  | *(none)*                                |
| `isready`                                                                              | Check whether the engine is ready to receive commands.                                                                                                                                                                                                                                                                                                                                                                                                      | `readyok`                               |
| `5ducinewgame`                                                                         | Notify the engine that subsequent commands belong to a new game.                                                                                                                                                                                                                                                                                                                                                                                            | *(none)*                                |
| `position [[size <m>x<n>] [odd\|even] fen <5dfen-string> \| startpos] [moves <move>*]` | Set the current position. If `position fen` is used, a 5DFEN string specifies the starting position. `size <m>x<n>` specifies the board dimensions. `odd` or `even` specifies whether the initial game begins with an odd or even number of timelines. If `position startpos` is used, the default opening is **Standard – Turn Zero**. If the position is not the starting position, all moves played since the beginning of the game must follow `moves`. | *(none)*                                |
| `go`                                                                                   | Start calculating the best move.                                                                                                                                                                                                                                                                                                                                                                                                                            | `bestmove <move>` or `nobestmove`       |
| `stop`                                                                                 | Immediately stop searching.                                                                                                                                                                                                                                                                                                                                                                                                                                 | Immediately output the best move found. |
| `quit`                                                                                 | Shut down the engine and release resources.                                                                                                                                                                                                                                                                                                                                                                                                                 | `bye`                                   |

---

# Syntax

## 1. 5DFEN

5DFEN (5D Forsyth–Edwards Notation) describes the complete multiverse position in a 5D Chess game.

### 1.1 Basic Format

A 5DFEN string consists of one or more blocks of the form

```text
[<FEN>:<timeline>:<turn>:<color>]
```

Blocks may be separated by spaces.

### 1.2 `<FEN>` (Board Layout)

`<FEN>` describes the layout of a single chessboard.

#### 1.2.1 Piece Symbols

- Lowercase letters (`pnbrqk`, etc.) represent Black pieces.
- Uppercase letters (`PNBRQK`, etc.) represent White pieces.

#### 1.2.2 Empty Squares

Digits `1–8` represent consecutive empty squares.

#### 1.2.3 Rank Separator

A slash (`/`) separates adjacent board ranks.

### 1.3 Board Size

The number of ranks and files in `<FEN>` must match the board size specified by

```text
size <m>x<n>
```

If the `size` field is omitted from the `position` command, the board size is assumed to be **8×8**.

### 1.4 Unmoved Pieces

Certain pieces may be followed by an asterisk (`*`) to indicate that they have **never moved**.

The following pieces support this marker:

```text
p*  P*
r*  R*
k*  K*
w*  W*
```

The unmoved marker determines whether certain special moves are still legal, including:

1. Castling.
2. A pawn's initial two-square advance (which also affects the legality of en passant).

### 1.5 Supported Piece Types

The following piece symbols are currently defined:

| Symbol | Piece |
|--------|-------|
| `P` | Pawn |
| `W` | Brawn |
| `K` | King |
| `C` | Common King |
| `Q` | Queen |
| `Y` | Queen |
| `S` | Princess |
| `N` | Knight |
| `R` | Rook |
| `B` | Bishop |
| `U` | Unicorn |
| `D` | Dragon |

### 1.6 Timeline Numbering

Timeline identifiers are generally integers.

If the game begins with an **even** number of timelines:

- `0` represents **+0**.
- `-1` represents **−0**.

In other words, positive timeline numbers remain unchanged, while negative timeline numbers are shifted downward by one.

### 1.7 Turn Number

The turn value is an unsigned integer.

### 1.8 Color

The color field consists of a single character:

- `w` — White
- `b` — Black

### 1.9 Difference from Standard Chess FEN

Unlike standard chess FEN, 5DFEN does **not** encode the side to move.

This information can always be inferred from the turn numbers and color fields of all timeline blocks.

---

## 2. Move Format

A move is either

```text
<lan>
```

or

```text
submit
```

### 2.1 Long Algebraic Notation (LAN)

The syntax is

```text
(<l0>T<t0>)<x0><y0>(<l1>T<t1>)<x1><y1>[<promotion>]
```

This represents moving a piece from

```text
(<l0>, <t0>, <x0>, <y0>)
```

to

```text
(<l1>, <t1>, <x1>, <y1>)
```

optionally followed by promotion.

If the promotion field is omitted, the piece is promoted to a **Queen** by default.

#### 2.1.1 Timeline Numbers

`<l0>` and `<l1>` follow the same numbering rules as those used by 5DFEN.

- If the game begins with an odd number of timelines, they are the actual timeline numbers.
- If the game begins with an even number of timelines, negative timeline numbers are shifted downward by one.

#### 2.1.2 Turn Numbers

`<t0>` and `<t1>` follow the same rules as the turn field in 5DFEN.

#### 2.1.3 Files

`<x0>` and `<x1>` are lowercase letters in the range

```text
a b c d e f g h
```

#### 2.1.4 Ranks

`<y0>` and `<y1>` are digits in the range

```text
1 2 3 4 5 6 7 8
```

#### 2.1.5 Promotion

`<promotion>` is the symbol of the piece to which the moving piece is promoted (for example, `Q`), or another piece permitted by the game variant.

### 2.2 `submit`

The keyword

```text
submit
```

indicates that the player submits their move.

### 2.3 Move History

Each `position` command sent by the UI shall include **the complete move history since the initial position**, not merely the moves played since the previous `position` command.

---

# Engine States

## Initialization State

When started, the engine enters the **Initialization State**.

After receiving

```text
5duci
```

it replies

```text
5duciok
```

and enters the **Idle State**.

## Idle State

While idle:

* The engine should consume minimal CPU resources.
* It accepts all commands.
* Upon receiving `isready`, it should immediately respond with

```text
readyok
```

* Upon receiving `go`, it enters the **Thinking State**.

## Thinking State

In the Thinking State:

* The engine searches using all available resources.
* When the search finishes, it immediately returns

```text
bestmove <move>
```

or

```text
nobestmove
```

and returns to the Idle State.

If a `stop` command is received:

* The engine should terminate the search as quickly as possible.
* It should output the current best move (or `nobestmove`).
* Then return to the Idle State.

If a `isready` command is received: the engine should defer the response `readyok` until the search is complete.

---

# Notes

1. The engine may **only** perform search after receiving a `go` command.
2. Unknown commands must be ignored, and the engine should continue waiting for the next command.

---

# Example

### UI

# Example Session

| Sender | Message |
|--------|---------|
| UI | `5duci` |
| Engine | `5duciok` |
| UI | `5ducinewgame` |
| UI | `position startpos` |
| UI | `go` |
| Engine | `bestmove (0T1)e2e3` |
| UI | `position startpos moves (0T1)e2e3 (0T1)g8f6 submit` |
| UI | `go` |
| Engine | *(searching...)* |
| UI | `isready` |
| Engine | *(reply deferred while searching)* |
| UI | `stop` |
| Engine | `bestmove (0T1)g1f3` |
| Engine | `readyok` |
