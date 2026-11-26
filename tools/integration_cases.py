"""Declarative existing workloads; assertions belong to native Google Test."""
def job(label: str, args: list[str], screenshot: str | None = None,
        contains: list[str] | None = None,
        forbids: list[str] | None = None, benchmark: bool = False,
        forbid_examples: list[tuple[str, bool]] | None = None) -> dict:
    return {
        "label": label,
        "benchmark": benchmark,
        "args": args,
        "screenshot": screenshot,
        "contains": contains or [],
        "forbids": forbids or [],
        "forbid_examples": forbid_examples or [],
    }


# Mid-length JIT/GL smoke shared by all three OS jobs.
CORE_JOBS = [
    job("scene-30", ["--headless", "--frames", "30", "--scene-demo"]),
    job("scene-shot", ["--headless", "--frames", "40", "--scene-demo",
                       "--screenshot", "scene.png"],
        screenshot="scene.png"),
    job("text-shot", ["--headless", "--frames", "60", "--text-demo",
                      "--screenshot", "text.png", "--shot-min-colors", "12"],
        screenshot="text.png"),
    job("bench", ["--benchmark_filter=SpriteBatchTrace"], benchmark=True),
    job("tile", ["--headless", "--frames", "120", "--tile-demo"]),
    job("map", ["--headless", "--frames", "120", "--map-demo"]),
    job("platformer", ["--headless", "--ai", "--frames", "180", "--platformer-demo"]),
    job("mario", ["--headless", "--ai", "--frames", "180", "--mario-demo"]),
    job("slope", ["--headless", "--ai", "--frames", "120", "--slope-demo"]),
    job("path", ["--headless", "--frames", "120", "--path-demo"]),
    job("rt", ["--headless", "--frames", "120", "--rt-demo"]),
    job("ui", ["--headless", "--frames", "120", "--ui-demo"]),
    job("ui2", ["--headless", "--frames", "120", "--ui2-demo"]),
    job("ui3", ["--headless", "--frames", "120", "--ui3-demo"]),
    job("post", ["--headless", "--frames", "120", "--post-demo"]),
    job("fx", ["--headless", "--frames", "120", "--fx-demo"]),
    job("fog", ["--headless", "--frames", "120", "--fog-demo"]),
    job("audio", ["--headless", "--frames", "120", "--audio-demo"]),
    job("spatial", ["--headless", "--frames", "120", "--spatial-demo"]),
    job("phys-bench", ["--benchmark_filter=PhysicsTrace"], benchmark=True),
    job("phys-lua", ["--headless", "--fixed-sim-dt", "--frames", "200",
                     "--entry", "scripts/phys_test.lua"],
        contains=["[script] 0"],
        # Escape both literal brackets: libc++ ECMAScript rejects a bare ']'.
        # Keep the old Python search semantics, including unanchored/prefix hits.
        forbids=[r"\[script\] (10[0-9][0-9]|200[0-9])"],
        forbid_examples=[
            ("[script] 1000", True), ("[script] 1099", True),
            ("[script] 2000", True), ("[script] 2009", True),
            ("prefix [script] 1001\n", True), ("[script] 10000", True),
            ("[script] 20090", True),
            ("", False), ("[script] 0", False), ("[script] 999", False),
            ("[script] 1100", False), ("[script] 1999", False),
            ("[script] 2010", False), ("[scripts] 1000", False),
            ("[script]\n1000", False), ("[script] -1000", False),
        ]),
]

# macOS/Windows do not run the long Linux milestone gates; keep a JIT compile
# of those game scripts here at modest length. Attract-mode milestones stay
# Linux-only.
EXTENDED_EXTRA = [
    job("angry-birds", ["--headless", "--frames", "120", "--angry-birds-demo"]),
    job("limbo", ["--headless", "--frames", "120", "--limbo-demo"]),
    job("skeleton", ["--headless", "--frames", "120", "--skeleton-demo"]),
    job("light", ["--headless", "--frames", "120", "--light-demo"]),
    job("light-bench", ["--benchmark_filter=LightingTrace"], contains=["[light-bench] PASS"], benchmark=True),
    job("beat-em-up", ["--headless", "--ai", "--frames", "180", "--beat-em-up-demo"]),
    job("shmup", ["--headless", "--ai", "--frames", "180", "--shmup-demo"]),
    job("snooker", ["--headless", "--ai", "--frames", "180", "--snooker-demo"]),
    job("contra", ["--headless", "--ai", "--frames", "180", "--contra-demo"]),
    job("city", ["--headless", "--ai", "--frames", "180", "--city-demo"]),
    job("rts", ["--headless", "--ai", "--frames", "180", "--rts-demo"]),
    job("vn", ["--headless", "--ai", "--frames", "180", "--vn-demo"]),
]
