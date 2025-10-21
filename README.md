Sample output:

```
Populating map...
Initialized map (0.0447617 MB memory)
Populating 1048576 asteroids with seed 69420...  done
Time elapsed: 2580 ms for 1000 ticks (normal layout + /Ox+SSE2).
Populating 1048576 asteroids with seed 69420...  done
Time elapsed: 1170 ms for 1000 ticks (stride layout + /Ox+AVX2).
Validating 492944 asteroids... succeeded
```

Main differences from real Factorio implementation:
no targeter update, no trigger effect
