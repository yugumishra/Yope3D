# articleAnalysis

Plot generators for the devlog series (articlePlan.txt). One script per
article-measurement pair; CSVs land in `data/` (committed — they are the
numbers the articles quote), rendered plots land in `plots/` (gitignored,
regenerable).

## Article 3 — The Archetype ECS

```bash
cmake --build build/mac-release --config Release --target yope_ecs_bench
./build/mac-release/yope_ecs_bench tools/articleAnalysis/data
python3 tools/articleAnalysis/plot_ecs_bench.py
```

`yope_ecs_bench` (`tests/ecs_bench.cpp`) sweeps four storage layouts (engine
view / shuffled `get()` / pointer-chase OOP / fat contiguous AoS) over the same
seeded 7-component population, 1k → 1M entities. The workload is a three-pass
frame (integrate → bounds → snapshot) run in two modes: a hot loop and a
cache-evicted mode that models the rest of the frame competing for cache. It
also times archetype migration vs. the `Hull.asleep` flag flip, and refuses to
run from a debug build. The plot script draws the two-panel working-set sweep
with L1d/L2 cliff markers and the tag-vs-flag bar chart.
