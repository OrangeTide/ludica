# tridrop

A triangle-based falling block puzzle inspired by a 1989 DOS game by
Daniel Singer that replaced rectangular blocks with right triangles.

## Game Modes

**NEO** — Single-cell pieces with triangle sliding physics. Pieces are
mostly two-triangle pairs, with occasional solid squares, single
triangles, and X destructors (which clear a cross-shaped area on
landing).

**TRIDROP** — Paired two-triangle pieces with traditional falling block
mechanics.

Both modes use a 10-column, 20-row board. Completed rows are cleared
and award points. Speed increases every 10 lines.

## Triangle Cell Model

Each cell holds one of six states:

```
  EMPTY    TRI_UL    TRI_UR    TRI_BL    TRI_BR    FULL
            ◤         ◥         ◣         ◢        ■
```

Two diagonally opposite triangles are *complementary* — placing one on
top of the other merges the cell into a solid FULL block:

```
  ◤ + ◢ = ■       ◥ + ◣ = ■
```

## NEO Sliding Physics

Each triangle has two solid sides (adjacent to its corner) and two open
sides (along the hypotenuse). When a falling triangle meets a triangle
already on the board, the solid and open sides determine what happens.

**Vertical interaction** — A falling piece lands on a cell below:

- Solid bottom meets solid top: the piece stops and locks.
- Open bottom meets open top, and the two triangles are complementary:
  the piece slides sideways off the ramp. It shifts left or right
  depending on which side of the landing triangle is open.
- If the slide destination is blocked, the pieces merge into a FULL
  block instead.

**Horizontal interaction** — A piece is pushed left or right into an
occupied cell:

- The moving piece's side toward movement is open, and the destination
  cell's facing side is also open, and the two are complementary: the
  piece enters the cell, overlapping the existing triangle.
- The piece does not lock immediately. Gravity continues to apply. If
  the cell below is empty, the piece falls out on the next tick. If
  blocked, the overlapping pair merges into FULL and the piece locks.

**Multi-cell groups** — When a two-triangle piece interacts with the
board, all cells are checked. Slide directions must agree across the
group. If cells disagree (e.g. a hook-shaped piece landing on a peak),
all complementary pairs merge simultaneously.

**Lock delay** — After a piece comes to rest, there is a one-tick grace
period before it locks, allowing a last-moment horizontal slide.

## Controls

| Key         | Action     |
|-------------|------------|
| Left/Right  | Move       |
| Up          | Rotate     |
| Down        | Soft drop  |
| Space       | Hard drop  |
| P / Escape  | Pause      |

## Build

```sh
make tridrop
```

Run from the project root:

```sh
_out/x86_64-linux-gnu/bin/tridrop
```
