# STABBING: Search Tool, All Biome-Based, Including Numerous Generosities

Measures the biome touching 0, 0 in 1:4 cells and Blocks^2 for every seed in order and writes each one to a text file.

## Build

Settings to adjust in the #defines before compiling:

    START_SEED    Starting seed for a new run
    THREADS       Worker threads, adjust for your PC
    LB            1 for Large Biomes
    Y_LEVEL       Y-level to sample at, 256 for the surface

MSYS2/MinGW on Windows, or Linux:

    make

## Usage

    ./STABBING.exe                  start from the checkpoint, or the START_SEED #define if there isn't one
    ./STABBING.exe <seed>           start from this seed instead
    ./STABBING.exe single <seed>    measure one seed and print it like measure.exe

## Output

Every seed goes to stabbing_results.txt, one per line. LB and other Y-levels get their own results and checkpoint files (stabbing_LB_results.txt, stabbing_y0_results.txt, stabbing_LB_y-64_results.txt...) so different results won't be confused.

Lines are written in seed order no matter which thread finishes first. stabbing_checkpoint.txt is saved every 30 seconds and on Ctrl+C,
so stopping with Ctrl+C never skips/repeats a seed.

The status line every 20 seconds shows seeds/s, the seed it's on, and the biggest measurement so far.

## Example Output
    Seed | Biome | 1:4 Size | Blocks^2 Size
    0 River 7672 122752
    1 Deep_Ocean 41046 656736
    2 Forest 72589 1161424