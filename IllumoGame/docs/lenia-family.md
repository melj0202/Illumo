# Lenia Family

## Objective

Add Bert Chan's Lenia, a continuous-state cellular automaton with a large
smooth ring kernel and a growth mapping, to CSim as a first-class family with
shipped species that glide, rotate, and hold their shape.

## Research basis

- Lenia definitions: `A' = clip(A + G(K * A) / T, 0, 1)`, ring kernel
  `K_S(r) = beta[floor(Br)] K_C(Br mod 1)` normalized to unit sum, polynomial
  `(4r(1-r))^4`, exponential `exp(4 - 1/(r(1-r)))`, and step cores; growth
  `2(1-(u-mu)^2/9sigma^2)^4 - 1`, `2exp(-(u-mu)^2/2sigma^2) - 1`, and step.
  State resolution `P` quantizes values to multiples of `1/P`.
  https://en.wikipedia.org/wiki/Lenia; B. W.-C. Chan, *Lenia: Biology of
  Artificial Life*, Complex Systems 28 (2019).
- Reference implementation and species catalogue (MIT):
  https://github.com/Chakazul/Lenia (`Python/LeniaND.py`,
  `Python/animals.json`). `kn`/`gn` index 1 is the polynomial form; cells use
  Lenia's RLE with values 0..255.

## Design

The sparse domain stays bytes. A `lenia` family of `n <= 256` states stores
intensity levels (background state 1 is level 0, states 2..n-1 are levels
1..n-2, state 0 is the full level), which is Lenia's own quantization with
`P = n - 1`. A new `RuleSet::NeighborhoodKind::WeightedKernel` contract exposes
integer kernel taps, per-state levels, and `nextStateFromPotential`.

Compilation (in `RuleSetRegistry`) quantizes the kernel to integer weights
(4096 per unit peak) and tabulates the growth delta `(n-1) G(u) / T` in
1/256-level fixed point over 16,384 potential bins, using only correctly
rounded IEEE operations and a deterministic series `exp`. The evaluator's
potential is an exact integer sum, so serial, lane, toroidal, infinite, and
native-oracle results are bit-identical. Rules whose growth raises empty space
are rejected so the infinite canvas stays sparse. Radius is bounded to 16, the
existing extended-range and lane-halo limit.

`SparseCellGrid::advanceWeightedKernel` mirrors the extended-range kernel:
targets are source chunks expanded by one chunk, each read through a radius-R
halo window converted to levels once, and published through the transactional
result/change-journal machinery.

Rejected alternatives: a floating-point cell domain would fork storage, saves,
presentation, editing, and lanes; FFT convolution needs a dense bounded world
and does not fit sparse chunks; platform `exp` risks last-bit divergence
between the WASM lanes and the native oracle.

## Scope

In scope: the `lenia` model, JSON parameters (`radius`, `time_steps`, `mu`,
`sigma`, `peaks`, `kernel_core`, `growth`), the `lenia_rle` starter, a
256-level family with a gradient palette, six species (Orbium, Orbium
bicaudatus, Gyrorbium, Scutium, Discutium, Paraptera), workshop label/preview,
documentation, and tests.

Non-goals: species with `R > 16` (Hydrogeminium, Kronium, larger Scutium
variants), multi-channel or multi-kernel Lenia, FFT, parallel worker-pool
evaluation of the kernel, exact floating-point states, and in-workshop editing
of Lenia parameters (JSON import/export only).

## Verification

- `IllumoGame.Rules.LeniaFamily`: encoding round trip; each shipped species
  compiles with a quiescent background; exact decay/growth values; Lenia RLE
  decoding and rejection; sparse kernel equals direct per-cell evaluation;
  Orbium mass after 50 generations equals an independent numpy model of the
  same quantized update (19,600 -> 18,728 levels) on both a 128-cell torus and
  the infinite canvas; Orbium's centroid travels; validation rejects
  space-filling growth, radius 17, and an empty kernel; catalog and lane rule
  packages round-trip to identical compiled tables.
- `IllumoGame.Wasm.LaneParity` sweeps every catalog rule, including the six
  Lenia species, across lane counts and topologies.
- A numpy screen of the six species over 400 quantized generations kept mass
  within 4% of the starting mass.
