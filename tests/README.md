Run the shared-phase snapshot regression on the guarded WSL build host:

```sh
WOW_PROJECT_ROOT='/absolute/path/to/wow private server' python3 -B tests/test_phase_snapshot.py
```

The test acquires the shared build lock before compiling the actual callback and getter with stubbed configuration and hashing. It checks startup, reloads, invalid profiles, and concurrent readers; it does not establish atomic configuration reads or runtime behavior.
