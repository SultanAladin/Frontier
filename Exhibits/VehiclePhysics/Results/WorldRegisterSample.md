# World Register · Default

Every object of `(unsaved level)` and the part it takes in the physics,
written by the physics extension when the simulation starts. Generated: edits are overwritten.

## Physics parts

| Part     | Objects |
|----------|--------:|
| Moving   |       1 |
| Fixed    |       0 |
| Held     |       0 |
| Fracture |       1 |
| Vehicle  |       1 |
| Tyre     |       0 |
| Skipped  |       0 |
| None     |       1 |
| Total    |       4 |

## Objects

| #   | Object                 | Type         | Physics  | Body  | Copies | Position (m)       | Size (m)         |
|-----|------------------------|--------------|----------|-------|--------|--------------------|------------------|
| 1   | Control vehicle | Vehicle | Vehicle  | 3     | 1      | -40.0, 0.0, 0.0 | — |
| 2   | Crate | Geometry | Moving   | 1     | 1      | 0.0, 1.0, 0.0 | — |
| 3   | Glass wall | Geometry | Fracture | 2     | 1      | 0.0, 1.0, 0.0 | 0.2 × 4.0 × 2.0 |
| 4   | Sun | Solar | —      | —   | 1      | — | — |

## Remarks

- **Control vehicle**: Project Drive on a Jolt chassis, carcass Spring, 0 tyre records fitted
- **Glass wall**: fracture deferred: a fixed collider over its bounds
